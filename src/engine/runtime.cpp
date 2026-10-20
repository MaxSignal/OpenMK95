#include "engine/runtime.h"
#include <cstdio>
#include "engine/display.h"
#include "core/fsys.h"
#include "engine/game.h"
#include "platform/platform.h"
#include "core/sjis.h"

namespace rt {

int messageSpeed() { return data::gd.messageSpeed(); }

// ------------------------------------------------------------------ notifications
static void onSelected(const ui::Notif& n) {
    if (n.byMouse) {
        ui::haveSelMouse = true;
        plat::mousePos(ui::selMouseX, ui::selMouseY);
        // game-screen coordinates 0..639, 0..479 (06 §8.4 step 1)
        ui::selMouseX = clampi(ui::selMouseX, 0, 639);
        ui::selMouseY = clampi(ui::selMouseY, 0, 479);
    } else ui::haveSelMouse = false;
    if (n.slot == 0 && ui::exists(6)) return;
    if (n.slot == 10) return;
    ui::selectedIndex = n.index;
    G.confirm = true;
    flushKeysWait();
    G.confirm = true;
    if (!G.menuRequest && !G.inBattle) return;
    if (G.inBattle) battle::onSelected(n.slot, n.index, n.byMouse);
    else menu::onSelected(n.slot, n.index, n.byMouse);
}

static void onCancelled(const ui::Notif& n) {
    if ((n.slot == 0 && ui::exists(6)) || n.slot == 1) return;
    G.confirm = true;
    if (n.slot == 0 && menu::subMode == 2) return;  // sub-menu mode persists, also in battle (07 §1.3)
    if (!G.menuRequest && !G.inBattle) return;
    if (G.inBattle) battle::onCancelled(n.slot);
    else menu::onCancelled(n.slot);
}

static void handleNotif(const ui::Notif& n) {
    switch (n.type) {
        case ui::N_SELECTED: onSelected(n); break;
        case ui::N_CANCELLED: onCancelled(n); break;
        case ui::N_PAGE:
            if (G.inBattle) battle::pageChanged(n.slot, n.page);
            else menu::pageChanged(n.slot, n.page);
            break;
        default: ui::handleWindowNotif(n); break;
    }
}

// ------------------------------------------------------------------ input
static void handleKeyDown(const plat::InputEvent& e) {
    processMessages();  // (quirk) a full pass first
    int vk = e.key;
    if (vk == VK_F3_) disp::realiseCommitted();
    if (vk == VK_CANCEL_ || (vk == VK_PAUSE_ && e.ctrl)) { G.quit = true; G.passAbort = true; return; }
    if (vk == VK_F4_ || (vk == VK_RETURN_ && e.alt)) { screenModeToggle(); return; }
    bool conf = vk == VK_RETURN_ || vk == VK_SPACE_;
    if (conf && G.releaseLatch) G.confirm = true;
    Chara* h = hero();
    if (h && h->moveType == MT_AUTOWALK) h->moveType = MT_PLAYER;
    if (!G.menuRequest && !G.eventRunning && !G.inBattle && vk != VK_ESCAPE_) {
        if (!conf || G.lastKey == -1) G.lastKey = vk;
        return;
    }
    if ((vk == VK_ESCAPE_ || conf) && G.releaseLatch) G.confirm = true;
    if (vk == VK_ESCAPE_ && !ui::exists(2)) {
        if (G.inBattle && battle::autoBattle) { battle::autoBattle = false; plat::setBusyCursor(false); }
        if (!G.menuRequest && !G.inBattle && !G.eventRunning && h && h->substep == 0) G.menuRequest = true;
        else { G.menuRequest = false; ui::destroy(2); }
    }
    G.releaseLatch = false;
    if (!ui::exists(ui::focused)) {
        if (!conf && !G.eventRunning && ui::exists(0) && !G.inBattle) ui::destroy(0);
        G.releaseLatch = false;
    } else {
        ui::Notif a; a.type = ui::N_ACTIVATE; a.slot = ui::focused;
        ui::post(a);
        ui::keyToWindow(ui::focused, vk);
    }
}

static void gameLeftDown(int x, int y) {
    G.confirm = true;
    if (!G.inBattle) {
        if (G.menuRequest) {
            if (menu::subMode == 2 && ui::exists(0)) {
                ui::destroy(0);
                ui::Notif a; a.type = ui::N_ACTIVATE; a.slot = 11; ui::post(a);
            } else menu::closeMenu();
        }
        Chara* h = hero();
        if (h && h->moveType == MT_AUTOWALK) h->moveType = MT_PLAYER;
        if (!G.drag) { G.dragX = x; G.dragY = y; G.drag = true; plat::captureMouse(true); }
        else G.sameDirCount = 0;
    }
}

static void gameActionOrAlight() {
    Chara* h = hero();
    if (!h || h->motion != -1) return;
    if (G.riding && h->moveType != MT_ALIGHTING) mapx::alight();
    else mapx::actionButton();
}

static void gameRightDown() {
    Chara* h = hero();
    if (h && h->moveType == MT_AUTOWALK) h->moveType = MT_PLAYER;
    if (G.inBattle) { battle::autoBattle = false; plat::setBusyCursor(false); return; }
    if (G.eventRunning) return;
    if (plat::mouseButtonHeld(plat::BTN_LEFT) && h && h->motion == -1) { gameActionOrAlight(); return; }
    if (!G.menuRequest && h && h->moveType == MT_PLAYER) { G.menuRequest = true; return; }
    if (ui::focused >= 0) { ui::Notif c; c.type = ui::N_CANCELLED; c.slot = ui::focused; ui::post(c); }
}

static void handleInput(const plat::InputEvent& e) {
    using namespace plat;
    switch (e.type) {
        case EV_KEYDOWN: handleKeyDown(e); break;
        case EV_KEYUP: G.releaseLatch = true; break;
        case EV_MOUSEMOVE: {
            // Movement over an active window is handled by that window only (06 §8.7 hover); everything
            // else, including movement over an inactive window and any movement while the mouse is
            // captured by a drag (01 §4.9), is game-area movement, which re-arms the release latch.
            ui::Window* w = G.drag ? nullptr : ui::windowAt(e.x, e.y);
            if (w && w->active) ui::mouseToWindow(w, 0, e.x, e.y);
            else {
                G.releaseLatch = true;
                mapx::mouseDragMove(e.x, e.y);
            }
            break;
        }
        case EV_MOUSEDOWN:
        case EV_DBLCLICK: {
            // the middle button and the wheel have no function (01 §4.9)
            if (e.button != BTN_LEFT && e.button != BTN_RIGHT) break;
            ui::Window* w = G.drag ? nullptr : ui::windowAt(e.x, e.y);
            if (w && w->active) {
                ui::mouseToWindow(w, e.button == BTN_RIGHT ? 2 : 1, e.x, e.y);
                break;
            }
            if (e.button == BTN_LEFT) {
                if (e.type == EV_DBLCLICK) {
                    if (!G.eventRunning && !G.inBattle) gameActionOrAlight();
                } else gameLeftDown(e.x, e.y);
            } else gameRightDown();
            break;
        }
        case EV_MOUSEUP:
            if (e.button == BTN_LEFT) {
                // a release over an active window goes to that window (no effect) unless captured
                ui::Window* w = G.drag ? nullptr : ui::windowAt(e.x, e.y);
                if (w && w->active) break;
                G.drag = false; G.sameDirCount = 0; plat::captureMouse(false);
            }
            break;
        case EV_CLOSE:
            if (e.key == 1) { G.quit = true; G.passAbort = true; }
            else askQuit();
            break;
    }
}

void askQuit() {
    bool yes = plat::messageBox(sj::toUtf8(sj::S("ゲームを終了しますか")), fsys::gameDirNameUtf8(), plat::MB_QUESTION, true);
    if (yes) G.quit = true;
}

void processMessages() {
    plat::pumpOS();
    audio::poll();
    for (;;) {
        if (!ui::notifs.empty()) {
            ui::Notif n = ui::notifs.front();
            ui::notifs.pop_front();
            handleNotif(n);
            continue;
        }
        plat::InputEvent e;
        if (plat::popInput(e)) { handleInput(e); continue; }
        break;
    }
}

// ------------------------------------------------------------------ waits
void wait(int n) {
    G.confirm = false;
    uint32_t t0 = plat::now();
    int elapsed = 0;
    for (;;) {
        if (G.confirm) return;
        if (n != 0 && n <= elapsed) return;
        if (G.inBattle && plat::keyHeld(VK_SHIFT_)) return;
        processMessages();
        if (G.quit) return;
        if (n > 0) elapsed = (int)((plat::now() - t0) / 100);
        if (!G.confirm) plat::idle();
    }
}

void waitMovie(bool (*playing)(), void (*requestClose)()) {
    G.confirm = false;
    for (;;) {
        if (!playing()) return;
        if (G.confirm) { requestClose(); G.confirm = false; return; }
        processMessages();
        if (G.quit) { requestClose(); return; }
        plat::idle();
    }
}

void flushKeysNoWait() {
    while (plat::keyHeld(VK_ESCAPE_)) plat::idle();
}

static bool relevantHeld() {
    static const int keys[] = {VK_UP_, VK_DOWN_, VK_LEFT_, VK_RIGHT_, VK_RETURN_, VK_SPACE_, VK_ESCAPE_,
                               VK_NUMPAD2_, VK_NUMPAD4_, VK_NUMPAD6_, VK_NUMPAD8_};
    for (int k : keys) if (plat::keyHeld(k)) return true;
    return false;
}

void flushKeysWait() {
    flushKeysNoWait();
    while (!G.releaseLatch && !G.quit) {
        processMessages();
        if (!relevantHeld()) break;
        plat::idle();
    }
    plat::discardKeyboardEvents();
}

// ------------------------------------------------------------------ messages
int messageWaitTenths(int f) {
    int s = messageSpeed();
    int n;
    switch (f) {
        case 1: n = s / 10; break;
        case 2: n = s / 5; break;
        case 7: n = 7 * s / 10; if (s > 0 && s % 10 == 0) n--; break;
        default: n = s * f / 10; break;
    }
    if (f > 0 && n == 0) n = 1;
    return n;
}

void message(const sjis& text, int f, bool bottom) {
    wait(1);
    if (!ui::exists(0)) ui::create(0, 92, bottom ? 360 : 32, 456, 92, false, 0, 0, "", 0);
    ui::setText(0, 1, text);
    wait(messageWaitTenths(f));
    if (!G.menuRequest && !G.eventRunning && !G.inBattle) ui::destroy(0);
}

void popup(int ownerSlot, const sjis& text) {
    ui::Window* ow = ui::get(ownerSlot);
    int ox = ow ? ow->x : 0, oy = ow ? ow->y : 0;
    int x = ox + 60;
    int y = ui::haveSelMouse ? ui::selMouseY : oy + 10;
    int w = sj::charCount(text) * ui::W;
    int h = ui::LineH + 14;
    ui::Notif d; d.type = ui::N_DEACTIVATE; d.slot = ownerSlot; ui::post(d);
    ui::create(12, x, y, w, h, false, 0, 0, "", 0);
    ui::setText(12, 1, text);
    wait(15);
    ui::destroy(12);
    ui::Notif a; a.type = ui::N_ACTIVATE; a.slot = ownerSlot; ui::post(a);
}

void versionBox() {
    bool prev = G.eventRunning;
    G.eventRunning = true;
    ui::Window* w = ui::create(12, 125, 70, 358, 189, false, 0, 0, "", 0);
    if (w) {
        w->labels[1].fmt = ui::CENTRE_WRAP;
        w->labels[1].r = Rect(0, 12, w->cw(), w->ch());
        ui::setText(12, 1, sj::S("ＲＰＧツクール９５\n\nRuntime Routine  ver 1.23\n\n"
                                 "Copyright (c) 1997 by ASCII Corporation\nCopyright (c) 1997 by You && Tsune\n"
                                 "All Rights Reserved.\n\n[ OK ]"));
    }
    wait(0);
    ui::destroy(12);
    flushKeysWait();
    G.eventRunning = prev;
}

// ------------------------------------------------------------------ misc
bool partyDown() {
    if (G.partySize <= 0) return true;
    for (int i = 0; i < G.partySize; i++)
        if (G.party[i].hp > 0 && !(G.party[i].status & 0x02)) return false;
    return true;
}

void showMap() {
    if (G.screenState == SS_MAP) return;
    if (G.screenState == SS_PICTURE_ON_BLACK || G.screenState == SS_PICTURE_BEFORE_MAP) {
        disp::fadeScreenToBlack(5);
        disp::reloadMapPaletteWithTint(G.inBattle);
    } else if (G.screenState == SS_ERASED || G.screenState == SS_BEFORE_START) {
        disp::realiseCommitted();
    }
    mapx::fullTileRedraw();
    mapx::interimRedraw(false);
    disp::show(G.trans[0]);
    G.screenState = SS_MAP;
    disp::haveRememberedMapPal = false;
}

void screenModeToggle() {
    if (G.inBattle || G.eventRunning || G.menuRequest) return;
    G.displaySwitch = true;
    // the current music is remembered locally (not in the shared "saved music" slot used by battles,
    // inns and movies), stopped, and restarted from its beginning after the switch (quirk)
    BgmRec remembered = audio::cur;
    audio::stopAllMusic();
    audio::recreateDevice();
    plat::setFullscreen(!plat::isFullscreen());
    if (remembered.has) {
        int32_t ab[2] = {remembered.a, remembered.b};
        audio::playBGM(remembered.name, ab, true);
    }
    disp::markDirty();
    G.displaySwitch = false;
}

static void gameOverCheck() {
    if (!(G.battleLost || partyDown())) return;
    ev::runEvent(99999, nullptr);
    if (G.quit) return;
    if (G.battleLost && G.screenState == SS_ERASED) {
        audio::playFieldMusic();
        mapx::fullTileRedraw();
        mapx::tickRender(-1, 0);  // characters with animation, presented only in MAP (state is ERASED)
        disp::show(G.trans[1]);
        G.screenState = SS_MAP;
    }
    G.battleLost = false;
}

// ------------------------------------------------------------------ main loop (01 §8)
extern int g_mapGeneration;

void mainLoop() {
    flushKeysNoWait();
    G.scrollDir = -1;
    G.lastKey = -1;
    for (;;) {
        if (G.quit) break;
        G.passAbort = false;
        G.postPass = POST_NONE;
        // the hero is fetched once here and used for the whole pass (01 §8; 04 §3.1); boarding/alighting
        // completion and a party-order change update G.passHero themselves.
        G.passHero = hero();
        Chara* h = G.passHero;
        processMessages();
        if (G.quit) break;
        if (G.passAbort) goto POST;
        if (G.menuRequest) {
            flushKeysNoWait();
            menu::runFieldMenu();
            flushKeysNoWait();
            G.menuRequest = false;
            if (G.quit) break;
            if (G.passAbort) goto POST;
        }
        if (G.eventRunning || G.inBattle || G.displaySwitch) { plat::idle(); continue; }
        h = G.passHero;
        if (h->motion == -1 && !G.arrival) {
            mapx::heroControlStep();
            if (G.passAbort) goto POST;
        }
        G.lastKey = -1;
        {
            uint32_t now = plat::now();
            if ((int32_t)(now - G.lastTick) <= 20) { plat::idle(); goto END; }
            G.lastTick = now;
        }
        // ---- the tick
        h = G.passHero;
        h->speedCounter++;
        if (h->speedCounter >= h->speed) {
            h->speedCounter = 0;
            Chara* touched = mapx::advance(h);
            if (touched) {
                G.scrollDir = -1;
                mapx::bumpTouch(touched);
                if (G.passAbort) goto POST;
            } else {
                if (h->substep == 1) {
                    G.scrollDir = (h->motion >= 0 && h->motion <= 3) ? h->motion : -1;
                    // trail + followers
                    for (int i = 7; i >= 1; i--) G.trail[i] = G.trail[i - 1];
                    G.trail[0] = h->motion;
                    for (int i = G.shown - 1; i >= 1; i--) {
                        Chara* f = G.partyChar[i];
                        mapx::startMotion(f, G.trail[i]);
                        if ((h->moveType == MT_BOARDING && G.trail[i] == MOTION_WAIT) || (h->moveType == MT_ALIGHTING && G.trail[i] == -1))
                            f->display = DISP_INVISIBLE;
                        else if (h->moveType == MT_ALIGHTING && G.trail[i] != -1)
                            f->display = DISP_VISIBLE;
                    }
                    Chara* last = G.partyChar[G.shown - 1];
                    if (h->moveType == MT_BOARDING && last->motion == MOTION_WAIT) { G.postPass = POST_BOARDED; goto POST; }
                    if (h->moveType == MT_ALIGHTING && last->motion == MOTION_WAIT) { G.postPass = POST_ALIGHTED; goto POST; }
                }
                for (int i = 1; i < G.shown; i++) {
                    Chara* f = G.partyChar[i];
                    mapx::advance(f);
                    if (h->moveType == MT_BOARDING && G.partyChar[i - 1]->display == DISP_INVISIBLE) f->facing = h->facing;
                }
            }
        }
        // vehicles and events
        if (!G.arrival) {
            for (int k = mapx::vehicleLoopStart(); k < 10 && mapx::vehicleUpdatable(k); k++)
                mapx::updateObject(G.vehicles[k].obj.get(), false);
            int gen = g_mapGeneration;
            std::vector<Chara*> list;
            for (auto& e : G.events) list.push_back(e.get());
            for (Chara* e : list) {
                if (gen != g_mapGeneration) break;
                bool still = false;
                for (auto& x : G.events) if (x.get() == e) { still = true; break; }
                if (!still) continue;
                mapx::updateObject(e, true);
                if (G.passAbort) goto POST;
                if (gen != g_mapGeneration) goto POST;
            }
        }
        // redraw
        G.heroScrollSub = G.scrollDir == -1 ? 0 : h->substep;
        mapx::clearGrid();
        mapx::tickRender(G.scrollDir, G.heroScrollSub);
        // autorun
        {
            int gen = g_mapGeneration;
            std::vector<Chara*> list;
            for (auto& e : G.events) list.push_back(e.get());
            for (Chara* e : list) {
                if (gen != g_mapGeneration) goto POST;
                bool still = false;
                for (auto& x : G.events) if (x.get() == e) { still = true; break; }
                if (!still) continue;
                if (e->cooldown == 0 && e->page >= 0 && e->attr.trigger(e->page) == 2 && e->onScreen) {
                    ev::runEvent(e->eventId, e);
                    if (G.passAbort) goto POST;
                }
            }
            for (int k = mapx::vehicleLoopStart(); k < 10 && mapx::vehicleUpdatable(k); k++) {
                Chara* v = G.vehicles[k].obj.get();
                if (v->cooldown == 0 && v->page >= 0 && v->attr.trigger(v->page) == 2 && v->onScreen) {
                    ev::runEvent(G.vehicles[k].id ? G.vehicles[k].id : v->eventId, v);
                    if (G.passAbort) break;
                }
            }
        }
        G.arrival = false;
        // step completion
        if (G.heroScrollSub == 4) {
            G.scrollDir = -1;
            mapx::recomputeScrollFlags();
            flushKeysNoWait();
            G.heroScrollSub = 0;
            mapx::stepOnCheck();
            if (!G.passAbort) {
                mapx::poisonStep();
                if (partyDown()) goto END;
                h = G.passHero;
                if (h->display == DISP_VISIBLE && !h->hasRoute()) mapx::encounterStep();
                if (partyDown()) goto END;
            }
        }
    POST:
        if (G.quit) break;
        if (G.postPass == POST_BOARDED) mapx::finishBoarding();
        else if (G.postPass == POST_ALIGHTED) mapx::finishAlighting();
    END:
        gameOverCheck();
        if (G.quit) break;
        if (G.pageRefresh) { G.pageRefresh = false; ev::refreshAllPages(); }
        G.postPass = POST_NONE;
        G.lastKey = -1;
    }
}

// ------------------------------------------------------------------ title / new game
static void newGame() {
    G.screenState = SS_BEFORE_START;
    G.curMap = -1;
    mapx::teleport(data::gd.startMap, data::gd.startX, data::gd.startY);
    ev::runEvent(0, nullptr);
    audio::playFieldMusic();
    showMap();
    if (G.quit) return;
    flushKeysNoWait();
    G.lastKey = -1;
    mainLoop();
}

int g_testBattle = 0;  // developer option --test-battle N

static void continueGame() {
    int M = G.curMap;
    G.curMap = -2;
    Chara* h = hero();
    mapx::teleport(M, h->x, h->y);
    {
        BgmRec m = audio::cur;  // local copy: playBGM updates audio::cur while playing
        int32_t ab[2] = {m.a, m.b};
        audio::playBGM(m.name, ab, true);
    }
    disp::realiseCommitted();
    if (G.quit) return;
    if (g_testBattle > 0) battle::start(g_testBattle - 1, false);
    flushKeysNoWait();
    G.lastKey = -1;
    mainLoop();
}

void runTitle() {
    bool titleShown = false;
    for (;;) {
        int menuY = 250;
        std::vector<uint8_t> f;
        Bitmap tb;
        bool hasTitle = fsys::read("title.bmp", f) && decodeBmp(f, tb);
        if (hasTitle) {
            disp::back.fill(0);
            disp::loadPaletteFrom(tb, true);
            Surface img;
            convertBitmap(tb, disp::realised, img);
            int w = tb.w, h = tb.h;
            int w2 = std::min(w, 640), h2 = std::min(h, 480);
            int x = w < 640 ? (640 - w) / 2 : 0, y = h < 480 ? (480 - h) / 2 : 0;
            blitStretch(img, Rect(0, 0, w, h), disp::back, Rect::xywh(x, y, w2, h2), -1);
            disp::visible.px = disp::back.px;
            disp::markDirty();
            menuY = y + h2 - h2 / 4;
            if (!titleShown) wait(0);
            flushKeysWait();
            titleShown = true;
        }
        G.eventRunning = true;
        ui::listHelper(-1, menuY, 140, 2, sj::S("はじめから\n続きから"), 0, ui::CENTRE_WRAP, 6, 0, false);
        wait(0);
        int sel = ui::selectedIndex;
        ui::destroy(6);
        if (titleShown) {
            disp::fullPresent();
            disp::fadeScreenToBlack(5);
            disp::back.fill(0);
            disp::markDirty();
            disp::reloadMapPaletteWithTint(false);
        }
        G.eventRunning = false;
        if (G.quit) sel = -1;
        if (sel != 1) { newGame(); return; }
        if (save::loadDialog()) { continueGame(); return; }
    }
}

void startNewGameFlow() { runTitle(); }

}  // namespace rt
