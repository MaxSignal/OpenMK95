// Events: pages, the event runner and every event command (05-events.md).
#include <algorithm>
#include <cstdio>
#include "engine/display.h"
#include "core/fsys.h"
#include "engine/game.h"
#include "platform/platform.h"
#include "core/rng.h"
#include "engine/runtime.h"
#include "core/sjis.h"

namespace rt { extern int g_mapGeneration; }

namespace ev {

int messageTime = -1;

// ------------------------------------------------------------------ pages (05 §4)

static bool selectPageInt(Chara* c, const int* counts) {
    int chosen = 0;
    for (int p = 3; p >= 1; p--) {
        uint32_t cf = c->attr.cond(p);
        if (!(cf & 1) && !(cf & 2) && !(cf & 4) && !((cf & 8) && c->attr.condParty(p) != 0)) continue;
        if (cf & 1) { int it = c->attr.condItem(p); if (it >= 100 || counts[it] == 0) continue; }
        if (cf & 2) { if ((uint32_t)G.money < c->attr.condMoney(p)) continue; }
        if (cf & 4) { int sw = c->attr.condSwitch(p); if (sw >= 1024 || !switchOn(sw)) continue; }
        if (cf & 8) { int m = c->attr.condParty(p); if ((m & G.partyMask) != m) continue; }
        chosen = p;
        break;
    }
    if (chosen == c->page) return false;
    c->page = chosen;
    applyPage(c, false);
    return true;
}

void applyPage(Chara* c, bool partial) {
    int p = c->page;
    if (p < 0 || p > 3) return;
    if (!partial) {
        int g = c->attr.graphic(p);
        if (g < 0x100) { c->graphic = g & 0x3F; c->facing = g >> 6; c->facingOverride = g >> 6; }
        else c->graphic = g;
        c->cooldown = 1;
    }
    if (c != hero()) { c->layer = c->attr.layer(p); c->moveType = c->attr.moveType(p); }
    c->animType = c->attr.animType(p);
    c->display = c->attr.display(p);
    c->speed = c->attr.speed(p);
    c->vehicleType = c->attr.vehicle(p);
    c->resumeIndex = 0;
    if (!partial) {
        c->route.clear();
        if (!c->pageRoutes[p].empty()) {
            c->route = c->pageRoutes[p];
            c->routeMode = ROUTE_RUNNING;
            c->routePos = 0;
            c->routeStep = 1;
        } else {
            c->routeMode = ROUTE_NONE;
            c->routePos = 0;
            c->routeStep = 1;
        }
    }
}

void refreshAllPages() {
    int counts[100];
    for (int i = 0; i < 100; i++) counts[i] = G.itemCount[i];
    for (int i = 0; i < G.partySize; i++) {
        int a = G.partyActor[i];
        if (a < 0 || a > 7) continue;
        for (int k = 0; k < 5; k++) { int e = G.equip[a][k]; if (e >= 0 && e < 100) counts[e]++; }
    }
    auto consider = [&](Chara* c) {
        if (c->resumeIndex != 0) return;
        if (G.heroWait.obj == c && G.heroWait.index >= 1) return;
        selectPageInt(c, counts);
    };
    for (size_t i = 0; i < G.events.size(); i++) consider(G.events[i].get());
    if (!G.events.empty())
        for (int k = 0; k < 10; k++)
            if (G.vehicles[k].obj) consider(G.vehicles[k].obj.get());
}

void createMapEvents() {
    MapRec& m = data::gd.maps[G.curMap];
    for (int y = 0; y < m.h; y++)
        for (int x = 0; x < m.w; x++) {
            uint16_t& id = m.events[(size_t)y * m.w + x];
            if (!id) continue;
            bool inTable = false;
            for (int k = 0; k < 10 && G.vehicles[k].id != 0; k++)
                if (G.vehicles[k].id == id) { inTable = true; break; }
            if (inTable) continue;
            EventHeader h;
            if (!data::readEventHeader(id, h)) { id = 0; continue; }
            std::unique_ptr<Chara> c(new Chara());
            c->eventId = id;
            c->x = c->destX = x;
            c->y = c->destY = y;
            c->attr = h.attr;
            for (int p = 0; p < 4; p++) c->pageRoutes[p] = h.routes[p];
            c->moveType = 0;
            G.events.push_back(std::move(c));
        }
}

// ------------------------------------------------------------------ text codes (05 §8.3)
void expandText(const sjis& in, sjis& out, bool& ok) {
    messageTime = -1;
    out.clear();
    ok = true;
    static const char* sufA[4] = {"たち", "がた", "ども", "ら"};
    static const char* sufB[4] = {"達", "方", "共", "等"};
    static const char* kanji[9] = {"〇", "一", "二", "三", "四", "五", "六", "七", "八"};
    auto insert = [&](const sjis& s) -> bool {
        if ((long)in.size() + (long)s.size() - 2 > 1024) { ok = false; return false; }
        out += s;
        return true;
    };
    for (size_t i = 0; i < in.size();) {
        char b = in[i];
        if (b != '$') { out += b; i++; continue; }
        if (i + 1 >= in.size()) break;
        char x = in[i + 1];
        i += 2;
        switch (x) {
            case '$': out += '$'; break;
            case '0': if (!insert(battlerName(G.party[0]))) return; break;
            case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8':
                if (!insert(data::gd.actors[x - '1'].name)) return;
                break;
            case 'G': case 'g': if (!insert(fmt("%d", G.money))) return; break;
            case 'U': case 'u': if (!insert(data::gd.coin)) return; break;
            case 'R': case 'r': if (!insert("\n")) return; break;
            case 'W': case 'w': {
                if (i + 2 > in.size()) { i = in.size(); break; }
                char t[3] = {in[i], in[i + 1], 0};
                int v = c_atoi(t, 2);
                messageTime = clampi(v, 0, 99);
                i += 2;
                break;
            }
            case 'S': case 's': {
                if (i >= in.size()) break;
                int d = in[i] - '0';
                i++;
                if (G.partySize > 1 && d >= 1 && d <= 4)
                    if (!insert(sj::S(x == 'S' ? sufA[d - 1] : sufB[d - 1]))) return;
                break;
            }
            case 'N': {
                int n = clampi(G.partySize, 0, 9);
                out += (char)0x82;
                out += (char)(0x4F + n);
                break;
            }
            case 'n': out += sj::S(kanji[clampi(G.partySize, 0, 8)]); break;
            default: break;
        }
    }
}

// ------------------------------------------------------------------ helpers
static int rangeValue(int lo, int hi) {
    if (lo < hi) {
        int64_t n = (int64_t)hi - lo + 1;
        return (int)(lo + (int64_t)(rng::R() % (uint64_t)n));
    }
    return lo;
}

static int32_t change32(int32_t v, int a1, int a2, bool rel, int32_t mn, int32_t mx) {
    int r = rangeValue(a1, a2);
    int64_t nv = rel ? (int64_t)v + r : r;
    nv = (int32_t)nv;
    if (nv < mn) nv = mn; else if (nv > mx) nv = mx;
    return (int32_t)nv;
}

static int16_t change16(int16_t v, int a1, int a2, bool rel, int mn, int mx) {
    int16_t lo = s16(a1), hi = s16(a2), mn16 = s16(mn), mx16 = s16(mx);
    int16_t r = s16(rangeValue(lo, hi));
    int16_t nv = rel ? s16(v + r) : r;
    if (nv < mn16) nv = mn16; else if (nv > mx16) nv = mx16;
    return nv;
}

// targets (05 §7.3)
static std::vector<int> targets(int t) {
    std::vector<int> r;
    if (t == 0) { for (int i = 0; i < G.partySize; i++) r.push_back(i); return r; }
    for (int i = 0; i < 8; i++)
        if (G.party[i].actorId == t - 1) { r.push_back(i); break; }
    return r;
}

static void closeAllWindows() { ui::destroyAll(); G.menuRequest = false; }

static int itemPower(int item) { return (item >= 0 && item < 100) ? data::items[item].power : 0; }

// ------------------------------------------------------------------ pictures (03 §7.4)
static int posX(int ph, int s) {
    int p;
    if (ph == 0) p = (640 - s < 40) ? 0 : 20;
    else if (ph == 1) p = (640 - s) / 2;
    else p = 640 - s - ((640 - s < 40) ? 0 : 20);
    if (p + s > 640) p = 640 - s;
    if (p < 0) p = 0;
    return p;
}
static int posY(int pv, int s) {
    if (pv > 2) pv = 0;
    int p;
    if (pv == 0) p = (480 - s < 40) ? 0 : 20;
    else if (pv == 1) p = (480 - s) / 2;
    else p = 480 - s - ((480 - s < 40) ? 0 : 20);
    if (p + s > 480) p = 480 - s;
    if (p < 0) p = 0;
    return p;
}

static void showPicture(const sjis& name, int F) {
    std::vector<uint8_t> f;
    Bitmap bmp;
    if (!fsys::read(sj::baseName(name), f)) return;
    ui::destroy(0);
    if (!decodeBmp(f, bmp)) return;
    int w = bmp.w, h = bmp.h;
    if (!(F & 4)) {
        if (G.screenState == SS_MAP) G.screenState = SS_PICTURE_OVER_MAP;
    } else {
        if (G.screenState == SS_MAP || G.screenState == SS_PICTURE_OVER_MAP) disp::fadeScreenToBlack(5);
        disp::back.fill(0);
        if (G.screenState >= 1) G.screenState = SS_PICTURE_ON_BLACK;
        if (!disp::haveRememberedMapPal) { disp::rememberedMapPal = disp::committed; disp::haveRememberedMapPal = true; }
        disp::loadPaletteFrom(bmp, true);
    }
    Surface img;
    convertBitmap(bmp, disp::realised, img);
    if (w > 640 || h > 480) F |= 1;
    int key = (F & 2) && !img.px.empty() ? img.px[0] : -1;
    int p = (F >> 4) & 15;
    int ph = p % 3, pv = p / 3;
    Rect D;
    if (!(F & 1)) {
        int W2 = std::min(w, 640), H2 = std::min(h, 480);
        D = Rect::xywh(posX(ph, W2), posY(pv, H2), W2, H2);
    } else if (!(F & 2)) {
        D = Rect(0, 0, 640, 480);
    } else {
        if ((h * 4) / 3 < w) { int H2 = h * 640 / w; D = Rect::xywh(0, posY(pv, H2), 640, H2); }
        else { int W2 = w * 480 / h; D = Rect::xywh(posX(ph, W2), 0, W2, 480); }
    }
    blitStretch(img, Rect(0, 0, w, h), disp::back, D, key);
    disp::show((int16_t)(F >> 16), D);
    if (F & 8) rt::wait(0);
    if (G.screenState == SS_BEFORE_START) G.screenState = SS_PICTURE_BEFORE_MAP;
}

static void eraseScreen(int t) {
    ui::destroy(0);
    switch (G.screenState) {
        case SS_BEFORE_START: break;
        case SS_PICTURE_BEFORE_MAP: disp::hide(t, true); break;
        case SS_PICTURE_OVER_MAP:
            mapx::interimRedraw(false);
            disp::hide(t, false);
            if (t == 13) disp::show(13);
            G.screenState = SS_MAP;
            break;
        case SS_PICTURE_ON_BLACK:
            disp::hide(t, true);
            disp::reloadMapPaletteWithTint(G.inBattle);
            G.screenState = SS_ERASED;
            rt::showMap();
            break;
        default:
            disp::hide(t, true);
            G.screenState = SS_ERASED;
            break;
    }
}

// ------------------------------------------------------------------ party change (code 23)
static void changeParty(int a1, int mode) {
    int m = a1 & 0xFF;
    int cur = G.partyMask, nw;
    if (mode == 1) nw = cur & ~m;
    else if (mode == 2) nw = m;
    else nw = cur | m;
    if (nw == 0) return;
    std::vector<int> order;
    for (int i = 0; i < G.partySize; i++) if (nw & (1 << G.partyActor[i])) order.push_back(G.partyActor[i]);
    for (int a = 0; a < 8; a++)
        if ((nw & (1 << a)) && std::find(order.begin(), order.end(), a) == order.end()) order.push_back(a);
    // rearrange battler records
    Battler old[8];
    memcpy(old, G.party, sizeof old);
    auto recOf = [&](int a) -> Battler {
        for (int i = 0; i < 8; i++) if (old[i].actorId == a) return old[i];
        Battler b; memset(&b, 0, sizeof b); b.actorId = (int8_t)a; return b;
    };
    int k = 0;
    for (int a : order) G.party[k++] = recOf(a);
    for (int a = 0; a < 8; a++)
        if (!(nw & (1 << a))) G.party[k++] = recOf(a);
    if (!G.riding) {
        Chara oldLead = *G.partyChar[0];
        for (size_t i = 0; i < order.size(); i++) {
            Chara* c = G.actorChar[order[i]].get();
            c->x = oldLead.x;
            c->y = oldLead.y;
            c->facing = c->facingOverride = oldLead.facing;
            c->moveType = oldLead.moveType;
        }
        Chara* nl = G.actorChar[order[0]].get();
        int g = nl->graphic;
        *nl = oldLead;
        nl->graphic = g;
        for (size_t i = 1; i < order.size(); i++) {
            Chara* c = G.actorChar[order[i]].get();
            c->substep = 0;
            c->motion = -1;
            c->destX = oldLead.y;
        }
        int t0 = G.trail[0];
        mapx::resetTrail();
        if (oldLead.substep != 0) G.trail[0] = t0;
    } else {
        int t0 = G.trail[0];
        mapx::resetTrail();
        (void)t0;
    }
    G.partySize = (int)order.size();
    for (int i = 0; i < 8; i++) G.partyActor[i] = i < G.partySize ? order[i] : 0;
    if (!G.riding) {
        G.shown = G.partySize;
        for (int i = 0; i < 8; i++) G.partyChar[i] = i < G.partySize ? G.actorChar[order[i]].get() : nullptr;
    }
    G.partyMask = (uint8_t)nw;
    G.pageRefresh = true;
}

static void changeLevel(int idx, int a1, int a2, bool rel) {
    Battler& b = G.party[idx];
    int aid = b.actorId;
    if (aid < 0 || aid > 7) return;
    const Actor& A = data::gd.actors[aid];
    int L = rangeValue(a1, a2);
    if (rel) L += b.level;
    L = clampi(L, 1, 100);
    if (L > b.level) b.exp = (L - 1) * (L - 1) * 10;
    int oldPow = b.pow, oldVit = b.vit, oldInt = b.intl;
    b.level = (int16_t)L;
    b.pow = A.curvePow[L - 1];
    b.vit = A.curveVit[L - 1];
    b.intl = A.curveInt[L - 1];
    b.agi = A.curveAgi[L - 1];
    b.luck = A.curveLuck[L - 1];
    if (b.pow != oldPow) {
        int atk = b.pow + (G.equip[aid][0] >= 0 ? itemPower(G.equip[aid][0]) : 0);
        b.atk = s16(std::min(atk, 9999));
    }
    if (b.intl != oldInt) b.maxmp = s16((int)(rng::R() % 5) + (b.intl - 1) * 10);
    if (b.vit != oldVit) {
        b.maxhp = s16((int)(rng::R() % 5) + b.vit * 10);
        int def = b.vit;
        for (int k = 1; k <= 3; k++) if (G.equip[aid][k] >= 0) def += itemPower(G.equip[aid][k]);
        b.def = s16(std::min(def, 9999));
    }
    if (b.hp > b.maxhp) b.hp = b.maxhp;
    if (b.mp > b.maxmp) b.mp = b.maxmp;
}

// ------------------------------------------------------------------ choice (code 2)
static int showChoice(const Cmd& c) {
    rt::flushKeysNoWait();
    const sjis& s = data::eventString(c.a1);
    int count = clampi(c.a2, 1, 4);
    std::vector<sjis> fields;
    size_t p = 0;
    while (true) {
        size_t e = s.find(',', p);
        sjis f = s.substr(p, e == sjis::npos ? sjis::npos : e - p);
        size_t k = 0;
        while (k < f.size() && (f[k] == ' ' || f[k] == '\t')) k++;
        fields.push_back(f.substr(k));
        if (e == sjis::npos) break;
        p = e + 1;
    }
    sjis joined;
    int maxLen = 0;
    sjis last;
    for (int i = 0; i < count; i++) {
        sjis o;
        if (i < (int)fields.size()) { o = fields[i]; last = o; maxLen = std::max(maxLen, sj::charCount(o)); }
        else o = last;
        joined += o + "\n";
    }
    sjis ex;
    bool ok;
    expandText(joined, ex, ok);
    if (!ok) ex = joined;
    ui::listHelper(-1, 100, 140, count, ex, maxLen, ui::CENTRE_WRAP, 6, 0, false);
    rt::wait(2);
    ui::selectedIndex = -1;
    while (ui::selectedIndex == -1 && !G.quit) rt::wait(0);
    ui::destroy(6);
    int sidx = ui::selectedIndex;
    if (sidx < 0 || sidx > 3) return 0;
    return ((uint32_t)c.a3 >> (8 * (3 - sidx))) & 0xFF;
}

// ------------------------------------------------------------------ commands
static Chara* g_obj = nullptr;  // the running event's object
std::vector<std::vector<uint8_t>>* g_routes = nullptr;

static int execute(Cmd& c, int nextCode) {
    Chara* obj = g_obj;
    switch (c.code) {
        case 1: {
            int y = (c.a2 & 2) ? 360 : 32;
            if (ui::exists(0) && ui::get(0)->y != y) ui::destroy(0);
            if (!ui::exists(0)) ui::create(0, 92, y, 456, 92, false, 0, 0, "", 0);
            sjis ex;
            bool ok;
            expandText(data::eventString(c.a1), ex, ok);
            if (!ok) ex = data::eventString(c.a1);
            ui::setText(0, 1, ex);
            rt::processMessages();
            if (messageTime >= 0) rt::wait(messageTime);
            else if (nextCode == 2) {}
            else rt::wait((c.a2 & 1) ? 0 : rt::messageSpeed());
            return 0;
        }
        case 2: return showChoice(c);
        case 3: return 0;
        case 12: return -1;
        case 4: {
            G.battleEscapeForbidden = c.a3 != 0;
            if (c.a2 == 0) c.a2 = c.a1;
            int troop;
            if (c.a1 == c.a2) troop = c.a1 - 1;
            else {
                int64_t n = (int64_t)c.a2 - c.a1 + 1;
                if (n == 0) troop = c.a1 - 1;
                else troop = rng::rnd((int)n) + c.a1 - 1;
            }
            if (troop >= 0 && troop < 250) battle::start(troop, c.a3 != 0);
            return 0;
        }
        case 5:
            G.postPass = POST_TELEPORTED;
            mapx::teleport(c.a1, c.a2, c.a3);
            G.passAbort = true;
            return 0;
        case 6: { int32_t ab[2] = {c.a2, c.a3}; audio::playBGM(data::eventString(c.a1), ab, false); return 0; }
        case 10: G.money = change32(G.money, c.a1, c.a2, c.a3 != 0, 0, 100000000); G.pageRefresh = true; return 0;
        case 11:
            for (int i : targets((int)((uint32_t)c.a3 >> 1))) G.party[i].exp = change32(G.party[i].exp, c.a1, c.a2, c.a3 & 1, 0, 100000);
            return 0;
        case 13: ui::destroy(0); rt::wait(c.a1); return 0;
        case 14: mov::playMovie(data::eventString(c.a1), c.a2); return 0;
        case 15: {
            if (c.a1 < 0 || c.a1 >= 1024) return 0;
            uint32_t cur = switchOn(c.a1) ? (1u << (c.a1 % 32)) : 0;
            if (cur != (uint32_t)c.a2) { setSwitch(c.a1, c.a2 == 1); G.pageRefresh = true; }
            return 0;
        }
        case 16:
            for (int i : targets((int)((uint32_t)c.a3 >> 1))) {
                Battler& b = G.party[i];
                b.hp = change16(b.hp, c.a1, c.a2, c.a3 & 1, 0, b.maxhp);
                if (b.hp < 1) b.status = 0;
            }
            return 0;
        case 17:
            for (int i : targets((int)((uint32_t)c.a3 >> 1))) { Battler& b = G.party[i]; b.mp = change16(b.mp, c.a1, c.a2, c.a3 & 1, 0, b.maxmp); }
            return 0;
        case 18:
            if (c.a2 < 0 || c.a2 >= 100) return 0;
            if (c.a1 == 0) { for (int i = 0; i < G.partySize; i++) { int a = G.party[i].actorId; if (a >= 0 && a < 8) G.learn[a][c.a2] = 1; } }
            else if (c.a1 >= 1 && c.a1 <= 8) G.learn[c.a1 - 1][c.a2] = 1;
            return 0;
        case 19:
            for (int i : targets((int)((uint32_t)c.a3 >> 1))) changeLevel(i, c.a1, c.a2, c.a3 & 1);
            return 0;
        case 20: {
            int item = (int)((uint32_t)c.a3 >> 1);
            if (item < 100) G.itemCount[item] = change16(G.itemCount[item], c.a1, c.a2, c.a3 & 1, 0, data::gd.itemMax);
            G.pageRefresh = true;
            return 0;
        }
        case 21: { int32_t ab[2] = {c.a2, c.a3}; audio::playSE(data::eventString(c.a1), ab); return 0; }
        case 22:
            for (int i : targets(c.a1)) {
                Battler& b = G.party[i];
                if (b.hp <= 0) continue;
                if (c.a2 == 0) b.status = 0;
                else if (c.a2 == 1) b.status |= 1;
                else if (c.a2 == 2) b.status |= 2;
                else if (c.a2 == 3) { b.hp = 0; b.status = 0; }
            }
            return 0;
        case 23: changeParty(c.a1, c.a2); return 0;
        case 24:
            for (int i : targets(c.a1)) {
                Battler& b = G.party[i];
                b.hp = s16(b.hp + ((100 - 25 * c.a2) * b.maxhp) / 100);
                if (b.hp > b.maxhp) b.hp = b.maxhp;
                b.mp = s16(b.mp + ((100 - 25 * c.a2) * b.maxmp) / 100);
                if (b.mp > b.maxmp) b.mp = b.maxmp;
            }
            return 0;
        case 26: showPicture(data::eventString(c.a1), c.a2); return 0;
        case 27: menu::inn(data::eventString(c.a1), c.a2, c.a3); return 0;
        case 28: menu::shop(data::eventString(c.a1), data::eventString(c.a2), c.a3); return 0;
        case 29: {
            uint8_t col[4] = {(uint8_t)(c.a2 & 255), (uint8_t)((c.a2 >> 8) & 255), (uint8_t)((c.a2 >> 16) & 255), (uint8_t)((uint32_t)c.a2 >> 24)};
            rt::processMessages();
            ui::destroy(0);
            rt::processMessages();
            disp::paletteEffect(G.screenState == SS_ERASED ? 4 : c.a1, col, true);
            return 0;
        }
        case 30:
            audio::stopAllMusic();
            disp::fadeScreenToBlack(5);
            G.quit = true;
            G.passAbort = true;
            return 0;
        case 31: eraseScreen(c.a1); return 0;
        case 32: audio::playFieldMusic(); return 0;
        case 33:
        case 35: {
            ui::destroy(0);
            Chara* t = c.code == 33 ? hero() : obj;
            if (!t) return 0;
            t->route.clear();
            if (c.a1 >= 0 && c.a1 < 100000 && (size_t)c.a1 < g_routes->size()) t->route = (*g_routes)[c.a1];
            t->routeMode = ROUTE_RUNNING;
            t->routePos = 0;
            t->routeStep = 1;
            return 0;
        }
        case 34: G.escapeMap = s16(c.a1); return 0;
        case 36: G.moveControl = 2; return 0;
        case 37: if (obj) G.moveControl = 3; return 0;
        case 38: G.moveControl = 1; return 0;
        case 39: G.moveControl = 0; return 0;
        case 40: {
            int g = c.a2;
            int ng = g < 0x100 ? (g & 0x3F) : g;
            if (G.riding) hero()->graphic = ng;
            else for (int i = 0; i < G.partySize; i++) if (G.partyChar[i]) G.partyChar[i]->graphic = ng;
            if (G.screenState == SS_MAP) { closeAllWindows(); mapx::interimRedraw(true); }
            return 0;
        }
        case 41: {
            int v = rangeValue(c.a1, c.a2);
            Chara* h = hero();
            if (!(c.a3 & 1)) h->speed = v;
            else (obj ? obj : h)->speed += v;
            h->speed = clampi(h->speed, 1, 20);
            for (int a = 0; a < 8; a++) G.actorChar[a]->speed = h->speed;
            return 0;
        }
        case 42:
            if (obj) {
                obj->graphic = c.a2 < 0x100 ? (c.a2 & 0x3F) : c.a2;
                if (G.screenState == SS_MAP) { closeAllWindows(); mapx::interimRedraw(true); }
            }
            return 0;
        case 43:
            if (obj) {
                int v = rangeValue(c.a1, c.a2);
                obj->speed = clampi((c.a3 & 1) ? obj->speed + v : v, 1, 20);
            }
            return 0;
        case 44: if (obj) obj->moveType = c.a1; return 0;
        case 45: {
            int16_t* L = G.teleportList;
            if (c.a2 == 0) {
                for (int i = 0; i < 100; i++)
                    if (L[i] == c.a1) {
                        L[i] = 0;
                        for (int j = i; j + 1 < 100 && L[j + 1] > 0; j++) { L[j] = L[j + 1]; L[j + 1] = 0; }
                        break;
                    }
            } else {
                if (c.a1 < 0 || c.a1 >= 500 || data::gd.maps[c.a1].type != 2) return 0;
                int n = 0;
                while (n < 100 && L[n] > 0) { if (L[n] == c.a1) return 0; n++; }
                if (n < 100) L[n] = s16(c.a1);
            }
            return 0;
        }
        case 46: G.teleportForbidden = s16(c.a1); return 0;
        case 47: if (obj && !G.riding) mapx::boardVehicle(obj); return 0;
        case 48:
            if (G.riding) {
                closeAllWindows();
                if (!mapx::alight()) { G.heroWait.id = -1; G.suspend = false; }
            }
            return 0;
        case 49: {
            int s = (c.a3 >> 8) & 0xFF;
            if (s < 12) { G.slots[s].name = data::eventString(c.a1); G.slots[s].hasName = true; G.slots[s].a = c.a2; G.slots[s].b = c.a3; }
            return 0;
        }
        case 50: if (c.a1 >= 0 && c.a1 <= 2) G.trans[c.a1] = c.a2; return 0;
        case 51:
            if (!G.riding) {
                for (int i = 0; i < G.partySize; i++) {
                    int a = G.partyActor[i];
                    if (G.partyChar[i]) G.partyChar[i]->graphic = data::gd.actors[a].graphic;
                }
            } else {
                Chara* v = hero();
                if (v->page >= 0) { int g = v->attr.graphic(v->page); v->graphic = g < 0x100 ? (g & 0x3F) : g; }
            }
            if (G.screenState == SS_MAP) { closeAllWindows(); mapx::interimRedraw(true); }
            return 0;
        case 52:
            if (c.a1 == 11) G.saveEnabled = false;
            else if (c.a1 == 12) G.saveEnabled = true;
            else save::saveDialog();
            return 0;
        case 53: for (int a = 0; a < 8; a++) G.actorChar[a]->vehicleType = c.a1; return 0;
        case 54:
            for (int k = G.riding ? 1 : 0; k < 10; k++) {
                if (G.vehicles[k].obj) {
                    if (G.vehicles[k].obj.get() == g_obj) g_obj = nullptr;
                    if (G.heroWait.obj == G.vehicles[k].obj.get()) G.heroWait.obj = nullptr;
                    G.vehicles[k].obj.reset();
                    G.vehicles[k].id = 0;
                    G.vehicles[k].map = 0;
                }
            }
            G.escapeMap = s16(c.a1);
            return 0;
        default: return 0;
    }
}

// ------------------------------------------------------------------ runner (05 §6)

void runEvent(int id, Chara* obj) {
    G.eventRunning = true;
    G.suspend = false;
    EventScript S;
    if (!data::readEventScript(id, S)) { G.eventRunning = false; return; }
    std::vector<std::vector<uint8_t>>* savedRoutes = g_routes;
    g_routes = &S.routes;
    int page = 0, index = 0;
    HeroWait& hw = G.heroWait;
    if (id == 0) {
        if (hw.id == 0) { page = hw.page; index = hw.index; hw.id = -1; }
    } else if (id == 99999) {
        if (hw.id == 99999) { page = hw.page; index = hw.index; hw.id = -1; }
        else {
            Chara tmp;
            tmp.attr = S.hdr.attr;
            int counts[100];
            for (int i = 0; i < 100; i++) counts[i] = G.itemCount[i];
            tmp.page = -1;
            Chara* savedHeroCheck = &tmp;
            (void)savedHeroCheck;
            int chosen = 0;
            for (int p = 3; p >= 1; p--) {
                uint32_t cf = tmp.attr.cond(p);
                if (!(cf & 1) && !(cf & 2) && !(cf & 4) && !((cf & 8) && tmp.attr.condParty(p) != 0)) continue;
                if (cf & 1) { int it = tmp.attr.condItem(p); if (it >= 100 || counts[it] == 0) continue; }
                if (cf & 2) { if ((uint32_t)G.money < tmp.attr.condMoney(p)) continue; }
                if (cf & 4) { int sw = tmp.attr.condSwitch(p); if (sw >= 1024 || !switchOn(sw)) continue; }
                if (cf & 8) { int m = tmp.attr.condParty(p); if ((m & G.partyMask) != m) continue; }
                chosen = p;
                break;
            }
            page = chosen;
            index = 0;
        }
    } else if (!obj) {
        page = hw.page; index = hw.index; hw.id = -1;
    } else {
        page = obj->page;
        index = obj->resumeIndex;
        obj->resumeIndex = 0;
        if (obj->eventId == hw.id) hw.id = -1;
    }
    if (hw.obj == obj) hw.clear();
    G.runningEventId = id;
    g_obj = obj;
    if (page < 0 || page > 3) page = 0;
    std::vector<Cmd>& cmds = S.cmds[page];
    int cur = index;
    int gen = rt::g_mapGeneration;
    while (cur >= 0 && cur < (int)cmds.size() && cmds[cur].code != 0 && !G.quit) {
        Cmd& c = cmds[cur];
        int idx = cur + 1;
        int code = c.code;
        int next = cur + 1 < (int)cmds.size() ? cmds[cur + 1].code : 0;
        if ((code == 33 && next != 35) || code == 47 || code == 48) {
            hw.id = id; hw.page = page; hw.index = idx; hw.obj = code == 47 ? nullptr : g_obj;
            G.suspend = true;
            if (g_obj) g_obj->resumeIndex = 0;
        } else if (g_obj && code == 35 && next != 33) {
            g_obj->resumeIndex = idx;
            G.suspend = true;
        }
        int res = execute(c, next);
        if (gen != rt::g_mapGeneration) {
            // the map's events were deleted (map change): the object no longer exists
            bool alive = false;
            for (auto& v : G.vehicles) if (v.obj.get() == g_obj) alive = true;
            if (g_obj && !alive) g_obj = nullptr;
            gen = rt::g_mapGeneration;
        }
        if (res == -1) {
            int d = (int16_t)((uint32_t)c.a4 >> 16);
            int j = cur + 1;
            while (j < (int)cmds.size() && cmds[j].code != 0 && !(cmds[j].code == 3 && (int16_t)((uint32_t)cmds[j].a4 >> 16) == d - 1)) j++;
            cur = j;
        } else if (res > 0) {
            int j = cur + 1;
            while (j < (int)cmds.size() && cmds[j].code != 0 && !(cmds[j].code == 12 && (int16_t)(cmds[j].a4 & 0xFFFF) == res)) j++;
            cur = j;
        }
        if (code == 5 && g_obj != hero()) g_obj = nullptr;
        if (code == 33) {
            mapx::applyLeadingDisplayBytes(hero());
            for (int a = 0; a < 8; a++) G.actorChar[a]->display = hero()->display;
        }
        if (code == 35 && g_obj) mapx::applyLeadingDisplayBytes(g_obj);
        if ((code == 4 && G.battleLost) || G.suspend) break;
        if (cur >= (int)cmds.size() || cmds[cur].code == 0) break;
        cur++;
    }
    // end of the run (05 §6.4)
    data::clearEventStrings();
    g_routes = savedRoutes;
    if (ui::exists(0)) ui::destroy(0);
    Chara* o = g_obj;
    if (o && o->page >= 0) o->cooldown = o->attr.trigger(o->page) == 2 ? -1 : 10;
    bool busy = G.screenState == SS_ERASED && (hero()->hasRoute() || (o && o->hasRoute()));
    if (!G.quit && !rt::partyDown() && !busy) rt::showMap();
    G.eventRunning = false;
    g_obj = nullptr;
    G.runningEventId = -1;
}

void resumeHeroWait() {
    HeroWait& hw = G.heroWait;
    if (hw.id == -1) return;
    if (hw.obj) { hw.obj->resumeIndex = hw.index; hw.obj->page = hw.page; }
    runEvent(hw.id, hw.obj);
}

}  // namespace ev
