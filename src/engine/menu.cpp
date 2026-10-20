// Field menu and its sub-menus, shops and inns (07-menus-shop-inn.md).
#include <algorithm>
#include <cstdio>
#include "engine/display.h"
#include "engine/game.h"
#include "platform/platform.h"
#include "engine/runtime.h"
#include "core/sjis.h"

namespace menu {

int subMode = 0;
int menuActor = 0;
int actionMember = 0;
static int g_equipMember = 0;
static int g_equipSlot = 0;
static std::vector<int> g_lists[13];   // entries of the paged lists
static bool g_shopSelling = false;

static const sjis& S(int n) { return data::strings[n]; }

void closeMenu() {
    ui::destroyAll();
    G.menuRequest = false;
}

static void postActivate(int slot) { ui::Notif n; n.type = ui::N_ACTIVATE; n.slot = slot; ui::post(n); }
static void postDeactivate(int slot) { ui::Notif n; n.type = ui::N_DEACTIVATE; n.slot = slot; ui::post(n); }

static int firstAble() {
    for (int i = 0; i < G.partySize; i++)
        if (G.party[i].hp > 0 && !(G.party[i].status & 0x02)) return i;
    return 0;
}

static bool knows(int member, int s) {
    const Battler& b = G.party[member];
    int a = b.actorId;
    if (a < 0 || a > 7) return false;
    int v = (int8_t)G.learn[a][s];
    return v > 0 && v <= b.level;
}

// ------------------------------------------------------------------ paged lists
static void fillPaged(int slot, int page, bool sellHalf) {
    std::vector<int>& L = g_lists[slot];
    int count = (int)L.size();
    for (int i = 0; i < 10; i++) {
        int e = page * 10 + i;
        if (e >= count) { ui::pagedClearRest(slot, i); ui::sendEndOfList(slot); return; }
        int id = L[e];
        sjis name, value;
        bool dis = false;
        switch (slot) {
            case 4: {
                name = data::magic[id].name;
                value = fmt("%dpts.", data::magic[id].cost);
                // the caster is the menu actor in the field (06 §5.3)
                dis = G.party[menuActor & 7].mp < data::magic[id].cost;
                break;
            }
            case 5: name = data::items[id].name; value = fmt("(%d)", G.itemCount[id]); break;
            case 7: name = (id >= 0 && id < 500) ? data::gd.maps[id].name : sjis(); break;
            case 8: name = data::items[id].name; value = fmt("%d", sellHalf ? data::items[id].price / 2 : data::items[id].price); break;
        }
        ui::pagedSetRow(slot, i, name, value, dis);
        if (count - 1 <= e) { ui::pagedClearRest(slot, i + 1); ui::sendEndOfList(slot); return; }
    }
}

static void createPaged(int slot, int x, int y, const std::vector<int>& list, bool sellHalf = false) {
    g_lists[slot] = list;
    if (ui::exists(slot)) return;
    ui::Window* w = ui::create(slot, x, y, 240, 210, true, 10, ui::LEFT_WRAP, "", (int)list.size());
    w->labels[45].text = "(";
    w->labels[46].text = "1";
    w->labels[47].text = fmt("/%d )", ((int)list.size() - 1) / 10 + 1);
    fillPaged(slot, 0, sellHalf);
}

void pageChanged(int slot, int page) {
    if (slot == 4 || slot == 5 || slot == 7 || slot == 8) fillPaged(slot, page, false);
}

// ------------------------------------------------------------------ main menu
void runFieldMenu() {
    int L = 4;
    for (int i = 0; i <= 4; i++) L = std::max(L, sj::charCount(S(i)));
    sjis text = S(0) + "\n" + S(1) + "\n" + S(2) + "\n" + S(3) + "\n" + S(4) + "\n" + sj::S("システム");
    ui::Window* w = ui::create(2, 40, 110, (L + 1) * ui::W, 6 * ui::LineH + 6, true, 6, ui::LEFT_WRAP, text, 6);
    menuActor = 0;
    Chara* lead = hero();
    bool locked = G.eventRunning || lead->hasRoute() || G.heroWait.id != -1;
    if (G.partySize < 2 || lead->substep != 0) w->labels[5].disabled = true;
    if (locked) for (int r : {0, 1, 2, 4}) w->labels[r + 1].disabled = true;
    ui::repaint(w);
    while (G.menuRequest && !G.quit) {
        rt::processMessages();
        if (G.menuRequest && !G.quit) plat::idle();
    }
    ui::destroy(2);
}

static void openItems() {
    std::vector<int> list;
    for (int i = 0; i < 100; i++) if (G.itemCount[i] > 0) list.push_back(i);
    if (list.empty()) { rt::popup(2, S(90)); return; }
    createPaged(5, 120, 80, list);
}

static void openPartyList(int x, int y, int w, int column, bool magicCaster) {
    sjis text;
    for (int i = 0; i < G.partySize; i++) text += battlerName(G.party[i]) + (i + 1 < G.partySize ? "\n" : "");
    unsigned mask = 0;
    if (magicCaster)
        for (int i = 0; i < G.fightSize(); i++)
            if (G.party[i].hp < 1 || (G.party[i].status & 0x7A)) mask |= 1u << i;
    ui::listHelper(x, y, w, G.partySize, text, 0, ui::LEFT_WRAP, 6, mask, true, column);
}

static void partyOrder();
static void openSystem() {
    sjis text = sj::S("ゲームを保存する\nゲームを読み込む\nスクリーンモードの変更\nバージョン情報\n終了する");
    unsigned mask = (!G.saveEnabled || hero()->hasRoute()) ? 1u : 0u;
    ui::listHelper(80, 180, 160, 5, text, 0, ui::LEFT_WRAP, 6, mask, false);
}

static void openTargetList(int column) {
    int n = G.partySize;
    int y0 = ui::haveSelMouse ? ui::selMouseY : 100;
    sjis text;
    for (int i = 0; i < n; i++) text += battlerName(G.party[i]) + (i + 1 < n ? "\n" : "");
    ui::listHelper(180, y0 - (ui::LineH * n) / 2 - 10, 240, n, text, 0, ui::LEFT_WRAP, 11, 0, true, column);
}

static void openDestList(int owner) {
    if (G.teleportForbidden == 1 || G.riding) { rt::popup(owner, S(96)); return; }
    std::vector<int> list;
    for (int i = 0; i < 100 && G.teleportList[i] > 0; i++) list.push_back(G.teleportList[i]);
    if (list.empty()) { rt::popup(owner, S(97)); return; }
    postDeactivate(owner);
    createPaged(7, 180, 140, list);
}

static void openStatus(int k) {
    postDeactivate(6);
    ui::create(9, 136, 80, 450, 230, false, 0, 0, "", 0);
    const Battler& b = G.party[k];
    int a = b.actorId;
    auto T = [&](int l, const sjis& t) { ui::Window* w = ui::get(9); if (w && w->labels[l].present) w->labels[l].text = t; };
    T(21, battlerName(b));
    T(22, fmt("Level %d", b.level));
    sjis cond;
    if (b.hp < 1) cond = S(26);
    else {
        int c = 0;
        for (int bit = 7; bit >= 0; bit--) if (b.status & (1 << bit)) { c = bit + 1; break; }
        cond = S(20 + c);
    }
    T(44, cond);
    T(13, "Hp:"); T(6, fmt("%d", b.hp)); T(16, "/"); T(18, fmt("%d", b.maxhp));
    T(14, "Mp:"); T(7, fmt("%d", b.mp)); T(17, "/"); T(19, fmt("%d", b.maxmp));
    T(11, sj::S("経験値:")); T(12, fmt("%d", b.exp));
    T(48, sj::S("ﾚﾍﾞﾙUPまで:")); T(49, fmt("%d", std::max(0, b.level * b.level * 10 - b.exp)));
    T(50, sj::S("知力:")); T(51, fmt("%d", b.intl));
    T(24, sj::S("攻撃力:")); T(27, fmt("%d", b.atk));
    T(25, sj::S("防御力:")); T(28, fmt("%d", b.def));
    T(26, sj::S("すばやさ:")); T(29, fmt("%d", b.agi));
    static const int nameL[5] = {32, 45, 33, 34, 35}, itemL[5] = {36, 46, 37, 38, 39};
    for (int s = 0; s < 5; s++) {
        T(nameL[s], S(10 + s) + ":");
        int e = (a >= 0 && a < 8) ? G.equip[a][s] : -1;
        T(itemL[s], e >= 0 && e < 100 ? data::items[e].name : sjis("---"));
    }
    T(30, data::gd.coin + ":");
    T(31, fmt("%d", G.money));
    ui::repaint(9);
}

static void openSubmenu(int r) {
    subMode = r;
    actionMember = 0;
    G.actions[0].command = r == 0 ? 0 : 1;
    postDeactivate(2);
    switch (r) {
        case 0: openItems(); break;
        case 1: openPartyList(90, 105, 240, 2, true); break;
        case 2: openPartyList(80, 105, 160, 0, false); break;
        case 3: openPartyList(80, 105, 140, 0, false); break;
        case 4: partyOrder(); break;
        case 5: openSystem(); break;
    }
}

// ------------------------------------------------------------------ item / magic use
void useItem(int item, int user, int target) { battle::useItem(item, user, target); }
void castSpell(int s, int c, int t, bool fromItem) { battle::castSpell(s, c, t, fromItem); }

static void itemChosen(int k) {
    if (k < 0 || k >= (int)g_lists[5].size()) return;
    int i = g_lists[5][k];
    int t = data::items[i].type;
    int st = t >= 18 && t - 18 < 100 ? data::magic[t - 18].type : -1;
    if (st >= 0 && st <= 10) { rt::popup(5, S(93)); return; }
    G.actions[actionMember].item = i;
    G.actions[actionMember].target = -1;
    if (t < 6) {
        ui::destroyAll();
        battle::useItem(i, firstAble(), -1);
        closeMenu();
    } else if (st == 12 || st == 21) {
        battle::useItem(i, firstAble(), 0);
    } else if (st == 20) {
        openDestList(5);
    } else {
        postDeactivate(5);
        openTargetList((st == 13 || t == 7 || t == 15) ? 2 : 1);
        G.actions[0].target = 0;
    }
}

static void casterChosen(int k) {
    menuActor = k;
    std::vector<int> list;
    for (int s = 0; s < 100; s++) if (data::magic[s].type >= 11 && knows(k, s)) list.push_back(s);
    if (list.empty()) { rt::popup(6, S(95)); return; }
    postDeactivate(6);
    createPaged(4, 140, 80, list);
}

static void spellChosen(int k) {
    if (k < 0 || k >= (int)g_lists[4].size()) return;
    int s = g_lists[4][k];
    postDeactivate(4);
    G.actions[menuActor].spell = s;
    int type = data::magic[s].type;
    if (type == 12 || type == 21) { closeMenu(); battle::castSpell(s, menuActor, 0, false); }
    else if (type == 20) openDestList(4);
    else openTargetList(type == 13 ? 2 : 1);
}

static void equipCandidate(int k) {
    if (k < 0 || k >= (int)g_lists[5].size()) return;
    int i = g_lists[5][k];
    int m = g_equipMember, s = g_equipSlot;
    ui::destroy(5);
    postDeactivate(11);
    Battler& b = G.party[m];
    int a = b.actorId;
    if (a < 0 || a > 7) return;
    int oldAtk = b.atk, oldDef = b.def;
    int oldM = (G.equip[a][4] >= 0 && G.equip[a][4] < 100) ? data::items[G.equip[a][4]].power : 0;
    G.itemCount[i]--;
    int prev = G.equip[a][s];
    G.equip[a][s] = (int8_t)i;
    if (prev >= 0 && prev < 100) G.itemCount[prev]++;
    rt::message(battlerName(b) + sj::S("は") + data::items[i].name + S(120), 5, true);
    auto pw = [&](int slot) { int e = G.equip[a][slot]; return (e >= 0 && e < 100) ? data::items[e].power : 0; };
    sjis nameS;
    int oldV, newV;
    if (s == 0) { b.atk = s16(std::min(b.pow + pw(0), 9999)); nameS = sj::S("攻撃力"); oldV = oldAtk; newV = b.atk; }
    else if (s == 4) { b.mdef = s16(pw(4)); nameS = sj::S("魔法防御力"); oldV = oldM; newV = b.mdef; }
    else { b.def = s16(std::min(b.vit + pw(1) + pw(2) + pw(3), 9999)); nameS = sj::S("防御力"); oldV = oldDef; newV = b.def; }
    sjis line;
    if (newV > oldV) line = nameS + sj::S(fmt("が%dポイント上がった\n", newV - oldV).c_str());
    else if (newV < oldV) line = nameS + sj::S(fmt("が%d ポイント下がった\n", oldV - newV).c_str());
    if (!line.empty()) rt::message(line, 10, true);
    ui::destroy(0);
    ui::selectedIndex = -1;
    postActivate(11);
}

static void equipSlotChosen(int s) {
    postDeactivate(11);
    g_equipSlot = s;
    int m = g_equipMember;
    int a = G.party[m].actorId;
    std::vector<int> list;
    for (int i = 0; i < 100; i++)
        if (G.itemCount[i] > 0 && data::items[i].type == s && a >= 0 && a < 8 && (data::items[i].mask & (1 << a))) list.push_back(i);
    if (list.empty()) { rt::popup(11, S(91)); return; }
    actionMember = m;
    createPaged(5, 180, 130, list);
}

// ------------------------------------------------------------------ party order (07 §6)
static void partyOrder() {
    int n = G.partySize;
    int h = ui::LineH * (n + 2) + 6;
    ui::create(10, 200, 190 - h, 200, h, false, 0, 0, "", 0);
    ui::setText(10, 40, "1");
    rt::processMessages();
    sjis text;
    for (int i = 0; i < n; i++) text += battlerName(G.party[i]) + (i + 1 < n ? "\n" : "");
    ui::listHelper(80, 180, 160, n, text, 0, ui::LEFT_WRAP, 6, 0, true);
    int k = 0;
    int order[8];
    bool placed[8] = {false};
    Battler copies[8];
    bool aborted = false;
    while (k < n - 1) {
        ui::selectedIndex = -1;
        rt::wait(0);
        if (G.quit) return;
        if (!ui::exists(10)) { aborted = true; break; }
        int s = ui::selectedIndex;
        if (s >= 0 && s < n) {
            if (placed[s]) continue;
            ui::setText(10, 40 + k, fmt("%d:", k + 1) + battlerName(G.party[s]));
            copies[k] = G.party[s];
            order[k] = s;
            placed[s] = true;
            k++;
            if (k < n) ui::setText(10, 40 + k, fmt("%d", k + 1));
            ui::setDisabled(6, s + 1, true);
        } else {
            if (k == 0) { aborted = true; break; }
            ui::setText(10, 40 + k, " ");
            ui::setText(10, 39 + k, fmt("%d", k));
            k--;
            placed[order[k]] = false;
            ui::setDisabled(6, order[k] + 1, false);
        }
        postActivate(6);
        ui::repaint(6);
    }
    if (aborted) {
        ui::destroy(6);
        ui::destroy(10);
        postActivate(2);
        return;
    }
    closeMenu();
    for (int s = 0; s < n; s++) if (!placed[s]) { order[n - 1] = s; copies[n - 1] = G.party[s]; }
    int oldActors[8];
    for (int i = 0; i < 8; i++) oldActors[i] = G.partyActor[i];
    Chara oldState[8];
    for (int i = 0; i < n; i++) if (G.actorChar[oldActors[i]]) oldState[i] = *G.actorChar[oldActors[i]];
    // "the previous leader's character" = the old position-0 actor's own map character
    // (not the ridden vehicle, which is the hero but not the leader, 04 §3.1)
    Chara* oldLeadChar = G.actorChar[oldActors[0]].get();
    int oldLeadMT = oldLeadChar->moveType, oldLeadSpeed = oldLeadChar->speed;
    for (int i = 0; i < n; i++) G.party[i] = copies[i];
    for (int i = 0; i < n; i++) G.partyActor[i] = oldActors[order[i]];
    if (!G.riding) {
        for (int i = 0; i < n; i++) {
            Chara* c = G.actorChar[G.partyActor[i]].get();
            const Chara& from = oldState[i];
            c->x = from.x; c->y = from.y; c->destX = from.destX; c->destY = from.destY;
            c->substep = from.substep; c->speedCounter = from.speedCounter;
            G.partyChar[i] = c;
        }
    }
    Chara* nl = G.actorChar[G.partyActor[0]].get();
    nl->moveType = oldLeadMT;
    nl->speed = oldLeadSpeed;
    if (!G.riding) G.passHero = G.partyChar[0];
}

// ------------------------------------------------------------------ dispatch (07 §1.4, §1.5)
void onSelected(int slot, int k, bool byMouse) {
    (void)byMouse;
    switch (slot) {
        case 2: openSubmenu(k); break;
        case 6:
            if (subMode == 3) openStatus(k);
            else if (subMode == 1) casterChosen(k);
            else if (subMode == 2) {
                if (k < G.partySize) {
                    g_equipMember = k;
                    postDeactivate(6);
                    sjis t = S(10) + "\n" + S(11) + "\n" + S(12) + "\n" + S(13) + "\n" + S(14);
                    ui::listHelper(110, 115, 160, 5, t, 0, ui::LEFT_WRAP, 11, 0, false);
                }
            } else if (subMode == 5) {
                closeMenu();
                switch (k) {
                    case 0: save::saveDialog(); break;
                    case 1: if (save::loadDialog()) save::afterLoadFromMenu(); break;
                    case 2: rt::screenModeToggle(); break;
                    case 3: rt::versionBox(); break;
                    case 4: G.quit = true; G.passAbort = true; break;
                }
            }
            break;
        case 11:
            if (subMode == 2) equipSlotChosen(k);
            else if (subMode == 0) {
                int it = G.actions[menuActor].item;
                if (it < 0 || it >= 100) { closeMenu(); break; }
                battle::useItem(it, menuActor, k);
            } else if (subMode == 1) {
                int sp = G.actions[menuActor].spell;
                if (sp < 0 || sp >= 100) { closeMenu(); break; }
                battle::castSpell(sp, menuActor, k, false);
            }
            break;
        case 7: {
            int dest = (k >= 0 && k < (int)g_lists[7].size()) ? g_lists[7][k] : 0;
            if (subMode == 0) {
                int it = G.actions[menuActor].item;
                if (it < 0 || it >= 100) { closeMenu(); break; }
                battle::useItem(it, menuActor, dest);
            } else {
                int sp = G.actions[menuActor].spell;
                if (sp < 0 || sp >= 100) { closeMenu(); break; }
                battle::castSpell(sp, menuActor, dest, false);
            }
            break;
        }
        case 5:
            if (subMode == 0) itemChosen(k);
            else if (subMode == 2) equipCandidate(k);
            break;
        case 4: spellChosen(k); break;
        case 9: closeMenu(); break;
        default: break;
    }
}

void onCancelled(int slot) {
    int m = menuActor;
    if (slot == 2) { closeMenu(); return; }
    if (ui::exists(10)) return;
    switch (slot) {
        case 6: postActivate(2); ui::destroy(6); break;
        case 4: postActivate(6); ui::destroy(4); G.actions[m].spell = -1; break;
        case 9: postActivate(6); ui::destroy(9); break;
        case 11:
            if (subMode == 2) { postActivate(6); ui::destroy(11); }
            else if (subMode == 0) { postActivate(5); ui::destroy(11); }
            else { postActivate(4); ui::destroy(11); }
            break;
        case 5:
            if (subMode == 2) postActivate(11); else postActivate(2);
            ui::destroy(5);
            G.actions[m].item = -1;
            break;
        case 7:
            if (subMode == 0) postActivate(5); else postActivate(4);
            ui::destroy(7);
            break;
        default: ui::destroy(slot); break;
    }
}

// ------------------------------------------------------------------ shop (07 §8)
static void splitLines(const sjis& s, sjis out[3]) {
    size_t p = 0;
    for (int i = 0; i < 3; i++) {
        if (p > s.size()) { out[i].clear(); continue; }
        size_t e = s.find('\n', p);
        if (e == sjis::npos) { out[i] = s.substr(p); p = s.size() + 1; }
        else { out[i] = s.substr(p, e - p); p = e + 1; }
    }
}

static sjis expandOr(const sjis& s) {
    sjis o;
    bool ok;
    ev::expandText(s, o, ok);
    return ok ? o : s;
}

void shop(const sjis& itemList, const sjis& texts, int buyOnly) {
    sjis lines[3];
    splitLines(texts, lines);
    int deals = 0;
    actionMember = 0;
    for (;;) {
        if (G.quit) return;
        rt::message(deals == 0 ? expandOr(lines[0]) : S(105), 1);
        sjis rows = buyOnly == 1 ? S(100) + "\n" + S(102) : S(100) + "\n" + S(101) + "\n" + S(102);
        int L = std::max(sj::charCount(S(100)), sj::charCount(S(102)));
        if (buyOnly == 0) L = std::max(L, sj::charCount(S(101)));
        L = std::max(L, 6);
        ui::listHelper(-1, 102, 0, buyOnly == 1 ? 2 : 3, rows, L, ui::CENTRE_WRAP, 6, 0, false);
        rt::wait(0);
        int sel = ui::selectedIndex;
        if (sel < 0 || (buyOnly == 1 ? sel == 1 : sel == 2)) break;
        bool selling = sel == 1;
        std::vector<int> list;
        if (!selling) {
            for (int i = 0; i < 100 && i < (int)itemList.size(); i++) if (itemList[i] == 'X') list.push_back(i);
            if (list.empty()) { rt::popup(6, S(106)); continue; }
            ui::destroy(6);
            rt::message(S(103) + sj::S(" （所持金: ") + fmt("%d", G.money) + data::gd.coin + sj::S("）"), 1);
        } else {
            for (int i = 0; i < 100; i++) if (G.itemCount[i] > 0) list.push_back(i);
            if (list.empty()) { rt::popup(6, S(90)); continue; }
            ui::destroy(6);
            rt::message(S(104) + " (" + sj::S("所持金: ") + fmt("%d", G.money) + data::gd.coin + ")", 1);
        }
        ui::selectedIndex = -1;
        g_shopSelling = selling;
        createPaged(8, 200, 102, list, selling);
        rt::wait(0);
        ui::destroy(8);
        int j = ui::selectedIndex;
        if (j < 0 || j >= (int)list.size()) continue;
        int i = list[j];
        const Item& it = data::items[i];
        if (!selling) {
            if (G.itemCount[i] < data::gd.itemMax) {
                if (G.money < it.price) rt::message(S(108), 10);
                else { G.itemCount[i]++; G.money -= it.price; deals++; }
            } else rt::message(S(107), 10);
        } else {
            if (it.type == 5 || it.price < 2) rt::message(S(110), 10);
            else { G.itemCount[i]--; G.money = std::min(G.money + it.price / 2, 100000000); deals++; }
        }
    }
    ui::destroy(6);
    sjis t;
    if (deals == 0) t = lines[2];
    else { t = lines[1]; G.pageRefresh = true; }
    rt::message(expandOr(t), 10);
    ui::destroyAll();
    G.menuRequest = false;
}

// ------------------------------------------------------------------ inn (07 §9)
void inn(const sjis& texts, int price, int flags) {
    sjis lines[3];
    splitLines(texts, lines);
    rt::message(expandOr(lines[0] + sj::S("（一泊") + fmt("%d", price) + data::gd.coin + sj::S("）")), 1);
    int L = std::max(sj::charCount(S(112)), sj::charCount(S(113)));
    ui::listHelper(-1, 100, 0, 2, S(112) + "\n" + S(113), L, ui::CENTRE_WRAP, 6, 0, false);
    rt::wait(0);
    int sel = ui::selectedIndex;
    ui::destroy(6);
    sjis text;
    if (sel == 0) {
        if (G.money < price) text = S(114);
        else {
            G.money -= price;
            ui::destroy(0);
            disp::fadeScreenToBlack(7);
            audio::saveBGM();
            audio::playBGM(sj::S("演奏停止"), nullptr, false);
            if (G.slots[11].hasName && G.slots[11].name.find(".mid") != sjis::npos) audio::setPlayOnce(true);
            audio::playJingle(11);
            for (int i = 0; i < G.partySize; i++) {
                Battler& b = G.party[i];
                if (flags & 0x4000) b.hp = b.maxhp;
                if (flags & 0x2000) b.mp = b.maxmp;
                if (flags & 0x0002) b.status &= 0x00DD;
                if (flags & 0x0001) b.status &= 0x00DE;
            }
            rt::wait(10);
            audio::waitJingle(11);
            audio::resumeBGM(false);
            mapx::gatherParty();
            mapx::fullTileRedraw();
            mapx::interimRedraw(false);
            disp::fullPresent();
            static const uint8_t normal[4] = {100, 100, 100, 0};
            disp::paletteEffect(7, normal, true);
            G.screenState = SS_MAP;
            text = lines[1];
        }
    } else text = lines[2];
    rt::message(expandOr(text), 10);
    ui::destroyAll();
    G.menuRequest = false;
}

}  // namespace menu
