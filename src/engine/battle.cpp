// Battles (08-battle.md) and the item/magic effect code shared with the field menu.
#include <algorithm>
#include <cstdio>
#include "engine/display.h"
#include "core/fsys.h"
#include "engine/game.h"
#include "platform/platform.h"
#include "core/rng.h"
#include "engine/runtime.h"
#include "core/sjis.h"

namespace battle {

bool autoBattle = false;

static Battler enemies[10];
static int enemyCount = 0;
static Troop troop;
static Surface monSheet[10];
static bool monOk[10];
static int expTotal = 0, goldTotal = 0, dropped = -1;
static int curInput = 0;
static int16_t savedStats[8][3];
static std::vector<int> enemyList;  // living enemies sorted by troop x
static int partyListCount = 0;
static int exitCode = 0;
static int lastCmd = 0;
static int lowered = -1;  // acting enemy drawn 30 px lower
static const Rect kArea(0, 102, 640, 362);

static const sjis& S(int n) { return data::strings[n]; }
static int F() { return G.fightSize(); }
static Battler& bat(int t) { return t >= 100 ? enemies[(t - 100) % 10] : G.party[t & 7]; }
static sjis num(int v) { return fmt("%d", v); }
static void msg(const sjis& t, int f) { rt::message(t, f); }

static bool allEnemiesDead() {
    for (int j = 0; j < enemyCount; j++) if (enemies[j].hp > 0) return false;
    return true;
}
static bool partyDefeated() {
    for (int i = 0; i < F(); i++) if (G.party[i].hp > 0 && !(G.party[i].status & 0x02)) return false;
    return true;
}
static bool canInput(int i) { return G.party[i].hp > 0 && !(G.party[i].status & 0x12); }

int maxLuck() {
    int m = 0;
    for (int i = 0; i < G.partySize; i++) if (G.party[i].hp != 0) m = std::max(m, (int)G.party[i].luck);
    return m;
}

static void rebuildEnemyList() {
    enemyList.clear();
    for (int j = 0; j < enemyCount; j++) if (enemies[j].hp > 0) enemyList.push_back(j);
    std::stable_sort(enemyList.begin(), enemyList.end(), [](int a, int b) { return troop.m[a].x < troop.m[b].x; });
}

// ------------------------------------------------------------------ scene drawing (08 §0.5)
static void drawMonster(int j, int dy) {
    if (!monOk[j]) return;
    blitKeyed(monSheet[j], Rect(0, 0, monSheet[j].w, monSheet[j].h), disp::back, troop.m[j].x, troop.m[j].y + dy, monSheet[j].key);
}

static void sceneCompose(int hideSlot = -1, int jumpSlot = -1, int jumpDy = 0) {
    disp::back.px = disp::cache.px;
    for (int j = 0; j < enemyCount; j++) {
        if (enemies[j].hp <= 0 || j == hideSlot) continue;
        int dy = 0;
        if (j == jumpSlot) dy = jumpDy;
        else if (j == lowered) dy = 30;
        drawMonster(j, dy);
    }
}

static void presentArea() { disp::presentRect(kArea); }

static void enemyRemoval(int e, int r) {
    int saved = lowered;
    lowered = -1;
    sceneCompose();
    lowered = saved;
    if (monOk[e]) disp::show(r, Rect::xywh(troop.m[e].x, troop.m[e].y, monSheet[e].w, monSheet[e].h));
}

static void jump(int e, int phase) {
    static const int OFF[10] = {-8, -12, -6, 6, 30, 34, 28, 18, 7, 0};
    if (phase == 0) audio::playBuiltinSE(50);
    for (int j = 0; j < 5; j++) {
        int saved = lowered;
        lowered = -1;
        sceneCompose(-1, e, OFF[phase * 5 + j]);
        lowered = saved;
        presentArea();
    }
    lowered = phase == 0 ? e : -1;
}

// ------------------------------------------------------------------ animations (08 §5)
static const int ANM_SOUND[42] = {0, 18, 19, 15, 16, 28, 24, 32, 21, 23, 22, 8, 8, 25, 48, 48, 48, 52, 33, 27, 29,
                                  14, 43, 3, 34, 18, 33, 52, 16, 16, 16, 46, 46, 46, 46, 34, 25, 46, 13, 8, 18, 2};
static const int ANM_POS[42] = {-2, -2, -4, -1, -4, -4, -1, -3, -3, -3, -3, -1, -1, -2, -4, -4, -2, -2, -2, -2, -2,
                                -3, -2, -1, -2, -2, -2, -2, -4, -4, -2, -3, -2, -4, -2, -3, -1, -3, -3, -2, -4, -2};
static const int ANM_REP[42] = {1, 1, 1, 2, 1, 1, 2, 1, 1, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 2, 1,
                                1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1};
static const int ANM_START[42] = {0, -1, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 0, -1, -1,
                                  -1, -1, -1, -1, -1, -1, 0, 0, 0, -1, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1};
static const int ANMTAB[66] = {1, 40, 4, 2, 35, 6, 7, 5, 8, 10, 29, 9, 11, 12, 36, 13, 21, 20, 27, 32, 16, 16,
                               15, 14, 30, 28, 3, 2, 35, 6, 7, 5, 23, 10, 29, 9, 11, 12, 36, 13, 21, 20, 27, 32,
                               16, 16, 15, 14, 19, 22, 34, 18, 24, 26, 20, 20, 20, 14, 33, 16, 31, 14, 14, 14, 37, 37};

static int animFile(int A) { return A < 99 ? ANMTAB[A % 66] : 0; }

static void animFlash(int A) {
    if (A < 0 || A >= 100) return;
    int mode; uint8_t c[4];
    auto set = [&](int m, int r, int g, int b, int gr) { mode = m; c[0] = (uint8_t)r; c[1] = (uint8_t)g; c[2] = (uint8_t)b; c[3] = (uint8_t)gr; };
    if (A < 48) {
        int m = A % 24;
        if (m < 4) set(6, 180, 0, 0, 0);
        else if (m < 8) set(6, 0, 0, 180, 0);
        else if (m < 12) set(1, 180, 180, 0, 0);
        else if (m < 16) set(1, 180, 180, 180, 0);
        else if (m < 20) set(6, 130, 0, 0, 0);
        else set(6, 180, 180, 180, 0);
    } else if (A <= 56) set(1, 130, 130, 130, 1);
    else set(6, 180, 180, 180, 0);
    disp::paletteEffect(mode, c, false);
}

static void errorBox(const sjis& file) {
    plat::messageBox(sj::toUtf8(sj::S("ﾌｧｲﾙ[") + file + sj::S("]が見つかりません")), sj::toUtf8(sj::S("RPGﾂｸｰﾙ95")), plat::MB_ERROR, false);
}

static sjis fullPathSjis(const sjis& name) {
    sjis d;
    sj::fromUtf8(fsys::gameDirHost(), d);
    return d + "\\" + name;
}

// target: enemy slot, or -1 = all
static void playAnimation(int A, int target, int q) {
    if (A < 0 || A >= 100) return;
    int count = (A >= 24 && A <= 47) ? 10 : 1;
    int f = animFile(A);
    int delay = A == 99 ? 1 : 100;
    int minLen = (ANM_START[f] == -1 && count >= 2) ? 10 : 3;
    sjis name = fmt("Anm%02d.bmp", f);
    std::vector<uint8_t> file;
    Bitmap bmp;
    if (!fsys::read(name, file)) { errorBox(fullPathSjis(name)); return; }
    Surface sheet;
    if (decodeBmp(file, bmp)) { convertBitmap(bmp, disp::realised, sheet); sheet.key = sheet.empty() ? -1 : sheet.px[0]; }
    int frames = sheet.w / 128;
    struct Sub { bool drawn; int x, y, start, len; };
    std::vector<Sub> subs(count);
    int T = minLen;
    int e = target < 0 ? 0 : target;
    for (int k = 0; k < count; k++) {
        Sub& s = subs[k];
        int mode = ANM_POS[f];
        if (count == 1 && mode == -1) mode = -2;
        s.drawn = false;
        if (mode == -1) {
            s.y = (int)(rng::R() % 120) + 120;
            s.x = (int)(rng::R() % 512);
            s.drawn = true;
        } else {
            if (!(e > enemyCount || e >= 10 || enemies[e].hp <= 0) && monOk[e]) {
                int ex = troop.m[e].x, ey = troop.m[e].y, w = monSheet[e].w, h = monSheet[e].h;
                s.x = ex + (w - 128) / 2;
                if (mode == -2) s.y = ey + (h - 128) / 2;
                else if (mode == -3) s.y = ey - 32;
                else s.y = ey + h + 8 - 128;
                s.x = clampi(s.x, 0, 512);
                s.y = clampi(s.y, 0, 352);
                if (e == q) s.y += 30;
                s.drawn = true;
            }
            if (target < 0) { e++; if (e == 10) e = 0; }
        }
        s.len = frames * ANM_REP[f];
        T = std::max(T, s.len);
        int st = ANM_START[f];
        if (st == -1) {
            if (k == 0) st = 0;
            else if (k == count - 1) st = T - s.len;
            else st = (int)(rng::R() % (uint32_t)(T - s.len + 1));
        }
        s.start = st;
    }
    int savedLow = lowered;
    lowered = q;
    for (int t = 0; t <= T; t++) {
        if (t > 0) disp::back.px = disp::cache.px;
        for (int j = 0; j < enemyCount; j++) if (enemies[j].hp > 0) drawMonster(j, j == q ? 30 : 0);
        if (t < T) {
            bool sound = false;
            for (auto& s : subs) {
                if (!s.drawn) continue;
                int r = t - s.start;
                if (r < 0 || r >= s.len) continue;
                if (t == s.start && !sound) { audio::playBuiltinSE(ANM_SOUND[f]); sound = true; }
                int fr = frames ? r % frames : 0;
                blitKeyed(sheet, Rect::xywh(fr * 128, 0, 128, 128), disp::back, s.x, s.y, sheet.key);
            }
        }
        presentArea();
        plat::delay(delay);
    }
    lowered = savedLow;
}

// ------------------------------------------------------------------ status window (08 §1.7.1)
static void updateMark() {
    sjis t;
    if (curInput >= 0 && curInput <= 3 && curInput < F()) { for (int i = 0; i < curInput; i++) t += "\n"; t += "*"; }
    else t = " ";
    ui::setText(1, 9, t);
}

static void statusRefresh() {
    if (!ui::exists(1)) return;
    ui::Window* w = ui::get(1);
    sjis hp, sl1, mhp, mp, sl2, mmp;
    for (int i = 0; i < F(); i++) {
        const Battler& b = G.party[i];
        hp += fmt("%3d", b.hp) + "\n"; sl1 += "/\n"; mhp += fmt("%3d", b.maxhp) + "\n";
        mp += fmt("%3d", b.mp) + "\n"; sl2 += "/\n"; mmp += fmt("%3d", b.maxmp) + "\n";
    }
    w->labels[6].text = hp; w->labels[16].text = sl1; w->labels[18].text = mhp;
    w->labels[7].text = mp; w->labels[17].text = sl2; w->labels[19].text = mmp;
    w->labels[8].color = w->transparent ? ui::C_LTGRAY : ui::C_GRAY;
    for (int i = 0; i < F(); i++) if (G.party[i].hp < 1) G.party[i].status |= 0x20;
    sjis st;
    for (int i = 0; i < F(); i++) {
        int k = 0;
        for (int bit = 7; bit >= 0; bit--) if (G.party[i].status & (1 << bit)) { k = bit + 1; break; }
        st += S(20 + k) + "\n";
    }
    w->labels[8].text = st;
    for (int i = 0; i < F(); i++) if (G.party[i].hp < 1) G.party[i].status = 0;
    ui::repaint(w);
}

static void createCommandWindow() {
    int L = 0;
    for (int n : {0, 1, 5, 6, 7}) L = std::max(L, sj::charCount(S(n)));
    sjis t = S(5) + "\n" + S(1) + "\n" + S(0) + "\n" + S(6) + "\n" + S(7);
    ui::Window* w = ui::create(2, 10, 280, (L + 1) * ui::W, 5 * ui::LineH + 6, true, 5, ui::LEFT_WRAP, t, 5);
    if (curInput < F() && (G.party[curInput].status & 0x04)) w->labels[2].disabled = true;
    ui::repaint(w);
}

// ------------------------------------------------------------------ effects shared with the field
void statusCure(Battler& X) {
    sjis n = battlerName(X);
    if ((X.status & 0x1e) == 0) { msg(n + S(67), 5); return; }
    sjis t;
    if (X.status & 0x02) t += n + sj::S("は") + S(35) + "\n";
    if (X.status & 0x04) t += n + S(38) + "\n";
    if (X.status & 0x08) t += n + S(41) + "\n";
    if (X.status & 0x10) t += n + S(44) + "\n";
    X.status &= 0xffe1;
    msg(t, 10);
}

bool teleportTo(int d) {
    if (d < 0 || d > 500 || d >= 500) return false;
    const MapRec& a = data::gd.maps[d];
    if (a.type != 2 || G.riding) return false;
    if (a.parent < 0 || a.parent >= 500) return false;
    const MapRec& p = data::gd.maps[a.parent];
    if (p.type >= 2 || p.w == 0) return false;
    if (a.exitX >= p.w || a.exitY >= p.h) return false;
    if (G.inBattle) {
        audio::playSE("wibble.wav", nullptr);
        exitCode = d;
        return true;
    }
    return mapx::escapeProcedure(d);
}

static void applyDamage(int t, int dmg, int a);

void castSpell(int s, int c, int t, bool fromItem) {
    if (s < 0 || s >= 100) { if (!G.inBattle) menu::closeMenu(); return; }
    const Magic& M = data::magic[s];
    int type = M.type;
    if (!G.inBattle && !fromItem) ui::destroyAll();
    int dest = 0;
    if (type == 20) { dest = t; t = 0; }
    if (type == 21) t = 0;
    Battler& caster = bat(c);
    bool partySide = t < 100;
    // 4.3 step 4 / 07 §3.4: the party side always counts the fighting members (field heal-all quirk)
    int count = partySide ? F() : enemyCount;
    int k = partySide ? t : t - 100;
    if (k < 0) k = 0;
    if (!fromItem) {
        msg(battlerName(caster) + sj::S("は") + M.name + S(60), 5);
        if (caster.status & 0x04) { msg(S(61), 5); goto end; }
        if (caster.mp < M.cost) { msg(S(62), 5); goto end; }
        caster.mp -= M.cost;
    }
    {
        int A = M.animNo;
        bool validA = A >= 0 && A < 100;
        if (partySide) {
            if (validA) {
                if (type != 20 && type != 21) audio::playBuiltinSE(ANM_SOUND[animFile(A)]);
                animFlash(A);
            }
        } else if (validA) {
            int q = c >= 100 ? c - 100 : -1;
            if (type == 12 || type == 1) playAnimation(A, -1, q);
            else playAnimation(A, k, q);
        }
        rt::wait(2);
        Battler& X = partySide ? G.party[k & 7] : enemies[k % 10];
        sjis xn = battlerName(X);
        int rate = 0, amount = 0;
        if (type != 1 && type != 12) {
            rate = X.rate[s];
            if (partySide && s >= 0 && s <= 10) rate = clampi(rate - X.mdef, 0, 200);
            if (type != 20 && type != 21) {
                if (type != 16 && X.hp < 1) { msg(S(65), 5); goto end; }
                amount = M.power * rate / 100;
                int v = std::max(1, amount / 5);
                if (rate > 0) amount += (int)(rng::R() % (uint32_t)(2 * v + 1)) - v;
                if (amount < 1 && type != 7 && type != 15) { msg(xn + S(66), 5); goto end; }
            }
        }
        auto statusSpell = [&](int bit, int sNew, int sAlr) {
            if ((int)(rng::R() % 100) < rate) {
                if (X.status & bit) msg(xn + S(sAlr), 5);
                else { X.status |= bit; msg(xn + S(sNew), 5); }
            } else msg(xn + S(66), 5);
        };
        switch (type) {
            case 0: applyDamage(partySide ? k : k + 100, amount, c); break;
            case 1:
                for (int j = 0; j < count; j++) {
                    Battler& Y = partySide ? G.party[j] : enemies[j];
                    if (Y.hp <= 0) continue;
                    int rj = Y.rate[s];
                    if (partySide) rj = std::max(0, rj - Y.mdef);
                    int am = M.power * rj / 100;
                    int v = std::max(1, am / 5);
                    if (rj > 0) am += (int)(rng::R() % (uint32_t)(2 * v + 1)) - v;
                    if (am < 1) msg(battlerName(Y) + S(66), 5);
                    else applyDamage(partySide ? j : j + 100, am, c);
                }
                break;
            case 2: statusSpell(0x01, 30, 31); break;
            case 3: statusSpell(0x02, 33, 34); break;
            case 4: statusSpell(0x04, 36, 37); break;
            case 5: statusSpell(0x08, 39, 40); break;
            case 6: statusSpell(0x10, 42, 43); break;
            case 7:
                if ((int)(rng::R() % 100) < rate) {
                    X.hp = 0;
                    msg(xn + S(45), 5);
                    if (!partySide) enemyRemoval(k, 3);
                } else msg(xn + S(66), 5);
                break;
            case 8: X.atk = s16(X.atk - amount); msg(xn + sj::S("の攻撃力が下がった"), 5); break;
            case 9: X.def = s16(X.def - amount); msg(xn + sj::S("の防御力が下がった"), 5); break;
            case 10: X.agi = s16(X.agi - amount); msg(xn + sj::S("の素早さが下がった"), 5); break;
            case 11: {
                int old = X.hp;
                X.hp = s16(std::min(X.hp + amount, (int)X.maxhp));
                msg(old == X.hp ? xn + S(67) : xn + sj::S("のHPが回復した"), 5);
                break;
            }
            case 12:
                for (int j = 0; j < count; j++) {
                    Battler& Y = partySide ? G.party[j] : enemies[j];
                    if (Y.hp <= 0) continue;
                    int rj = Y.rate[s];
                    int am = M.power * rj / 100;
                    int v = std::max(1, am / 5);
                    am += (int)(rng::R() % (uint32_t)(2 * v + 1)) - v;
                    if (am < 1) msg(battlerName(Y) + S(66), 5);
                    else {
                        int old = Y.hp;
                        Y.hp = s16(std::min(Y.hp + am, (int)Y.maxhp));
                        if (Y.hp != old) msg(battlerName(Y) + sj::S("のHPが回復した"), 5);
                    }
                }
                break;
            case 13: {
                int old = X.mp;
                X.mp = s16(std::min(X.mp + amount, (int)X.maxmp));
                msg(old == X.mp ? xn + S(67) : xn + sj::S("のMPが回復した"), 5);
                break;
            }
            case 14:
                if ((int)(rng::R() % 100) < rate) {
                    if (X.status & 1) { X.status &= ~1; msg(xn + S(32), 5); }
                    else msg(xn + S(67), 5);
                } else msg(xn + S(66), 5);
                break;
            case 15:
                if ((int)(rng::R() % 100) < rate) statusCure(X);
                else msg(xn + S(66), 5);
                break;
            case 16:
                if (X.hp < 1) {
                    if ((int)(rng::R() % 100) < rate) {
                        int p = std::min((int)M.power, 100);
                        int hp = X.maxhp * p / 100 + (int)(rng::R() % 7) - 3;
                        if (hp < 0) hp = 1; else if (hp > X.maxhp) hp = X.maxhp;
                        X.hp = s16(hp);
                        if (!partySide) sceneCompose();
                        msg(xn + S(46), 5);
                    } else msg(S(47) + xn + S(48), 5);
                } else msg(xn + S(67), 5);
                break;
            case 17: X.atk = s16(X.atk + amount); msg(xn + sj::S("の攻撃力が上がった"), 5); break;
            case 18: X.def = s16(X.def + amount); msg(xn + sj::S("の防御力が上がった"), 5); break;
            case 19: X.agi = s16(X.agi + amount); msg(xn + sj::S("の素早さが上がった"), 5); break;
            case 20:
                if (!G.inBattle || !G.battleEscapeForbidden) { if (!teleportTo(dest)) msg(S(64), 5); }
                else msg(S(63), 5);
                break;
            case 21: {
                bool ok = false;
                if (G.escapeMap > 0) {
                    if (G.inBattle && G.battleEscapeForbidden) { msg(S(63), 5); break; }
                    ok = teleportTo(G.escapeMap);
                }
                if (G.escapeMap <= 0 || !ok) msg(S(64), 5);
                break;
            }
            default: break;
        }
    }
end:
    if (!G.inBattle && !fromItem) menu::closeMenu();
}

static void itemEffect(int T, int t, int p) {
    Battler& X = G.party[t & 7];
    sjis n = battlerName(X);
    if (X.hp < 1 && T != 10) { msg(S(65), 5); return; }
    switch (T) {
        case 6: { int o = X.hp; X.hp = s16(std::min(X.hp + p, (int)X.maxhp)); msg(o != X.hp ? n + sj::S("のHPが回復した") : n + S(67), 5); break; }
        case 7: { int o = X.mp; X.mp = s16(std::min(X.mp + p, (int)X.maxmp)); msg(o != X.mp ? n + sj::S("のMPが回復した") : n + S(67), 5); break; }
        case 8: { sjis tx = (X.status & 1) ? n + S(32) : n + S(67); X.status &= ~1; msg(tx, 5); break; }
        case 9: statusCure(X); break;
        case 10:
            if (X.hp < 1) { X.hp = s16(std::max(1, X.maxhp * std::min(p, 100) / 100)); X.status = 0; msg(n + S(46), 5); }
            else msg(n + S(67), 5);
            break;
        case 11: case 12: case 13: case 14: case 15: case 16: case 17: {
            static const char* names[7] = {"最大ＨＰ", "最大ＭＰ", "力", "体力", "知力", "素早さ", "運"};
            int16_t* fields[7] = {&X.maxhp, &X.maxmp, &X.pow, &X.vit, &X.intl, &X.agi, &X.luck};
            sjis text = n + sj::S("の") + sj::S(names[T - 11]) + sj::S("が") + num(p) + sj::S("ポイント上がった");
            int16_t* fld = fields[T - 11];
            *fld = s16(*fld + p);
            if (*fld > 9999) *fld = 9999;
            if (T == 13) {
                text += "\n" + sj::S("攻撃力が") + num(p) + sj::S("ポイント上がった");
                X.atk = s16(clampi(s16(X.atk + p), 0, 9999));
            } else if (T == 14) {
                text += "\n" + sj::S("防御力が") + num(p) + sj::S("ポイント上がった");
                X.def = s16(clampi(s16(X.def + p), 0, 9999));
                text += "\n" + sj::S("最大ＨＰが") + num(p * 10) + sj::S("ポイント上がった");
                X.maxhp = s16(clampi(s16(X.maxhp + 10 * p), 1, 9999));
            } else if (T == 15) {
                text += "\n" + sj::S("最大ＭＰが") + num(p * 10) + sj::S("ポイント上がった");
                X.maxmp = s16(clampi(s16(X.maxmp + 10 * p), 0, 9999));
            }
            msg(text, 10);
            break;
        }
        default: break;
    }
}

void useItem(int item, int u, int t) {
    if (item < 0 || item >= 100) { if (!G.inBattle) menu::closeMenu(); return; }
    if (!G.inBattle) ui::destroyAll();
    const Item& it = data::items[item];
    int T = it.type;
    int Sx = (T >= 18 && T - 18 < 100) ? data::magic[T - 18].type : -1;
    sjis un = battlerName(G.party[u & 7]);
    if (G.itemCount[item] < 1) { msg(un + sj::S("は") + it.name + S(122), 10); goto end; }
    msg(un + sj::S("は") + it.name + S(121), 5);
    if (T < 6) msg(S(64), 5);
    else {
        G.itemCount[item]--;
        if (G.itemCount[item] < 1) { G.itemCount[item] = 0; ev::refreshAllPages(); }
        if (!G.inBattle && Sx >= 0 && (Sx < 11 || (Sx >= 17 && Sx <= 19))) msg(S(64), 5);
        else if (T > 17) {
            if (t == -1) msg(S(64), 5);
            else castSpell(T - 18, u, t, true);
        } else itemEffect(T, t, it.power);
    }
end:
    if (!G.inBattle) menu::closeMenu();
}

// ------------------------------------------------------------------ physical attack (08 §4.1, §4.2)
static void applyDamage(int t, int dmg, int a) {
    audio::playBuiltinSE(11);
    bool tParty = t < 100;
    Battler& T = bat(t);
    if (tParty) disp::paletteEffect(2, nullptr, false);
    else {
        for (int k = 0; k < 4; k++) {
            int saved = lowered;
            if (a >= 100) lowered = a - 100;
            sceneCompose((k & 1) ? -1 : t - 100);
            lowered = saved;
            presentArea();
            plat::delay(40);
        }
    }
    dmg = std::min(dmg, (int)T.hp);
    if (tParty) msg(battlerName(T) + sj::S("は") + num(dmg) + S(86), 5);
    else msg(battlerName(T) + sj::S("に") + num(dmg) + S(87), 5);
    T.hp = s16(std::max(0, T.hp - dmg));
    if (T.hp < 1) {
        if (tParty) { T.status = 0; msg(battlerName(T) + S(45), 5); }
        else {
            expTotal += T.exp;
            goldTotal += T.gold;
            int prob = (int8_t)T.dropProb;
            if (dropped == -1 && prob > 0)
                if ((int)(rng::R() % 100) < prob + maxLuck() / 10) dropped = T.dropItem;
            msg(battlerName(T) + S(88), 2);
            enemyRemoval(t - 100, 3);
        }
    }
}

static void physicalAttack(int a, int t, int mode) {
    Battler& A = bat(a);
    Battler& T = bat(t);
    bool tParty = t < 100, aParty = a < 100;
    if (T.hp <= 0) { msg(battlerName(T), 5); return; }
    if (!(T.status & 0x10) && T.agi - A.agi > 10) {
        if (rng::R() % 2 == 0) {
            int base = tParty ? 73 : 80;
            msg(battlerName(T) + S(base + (int)(rng::R() % 3)), 5);
            return;
        }
    }
    int dmg = A.atk - T.def / 2;
    int v = dmg / 5;
    if (v < 1) v = 1;
    dmg += (int)(rng::R() % (uint32_t)(2 * v + 1)) - v;
    if (T.status & 0x10) {
        if (dmg < 1) dmg = (int)(rng::R() % 3);
        dmg = dmg * 3 / 2;
    }
    if (dmg < 1) {
        int base = tParty ? 76 : 83;
        msg(battlerName(T) + S(base + (int)(rng::R() % 3)), 5);
        return;
    }
    if (aParty) {
        if ((int)(rng::R() % 100) < A.luck / 10 + 5) { msg(tParty ? S(72) : S(71), 5); dmg *= 2; }
    } else if (mode == 2) { msg(tParty ? S(72) : S(71), 5); dmg *= 2; }
    applyDamage(t, dmg, a);
    if (aParty || tParty) statusRefresh();
}

// ------------------------------------------------------------------ party action (08 §3.3)
static void knownSpells(int i, std::vector<int>& K) {
    K.clear();
    int aid = G.party[i].actorId;
    if (aid < 0 || aid > 7) return;
    for (int s = 0; s < 100; s++) {
        int v = (int8_t)G.learn[aid][s];
        if (v > 0 && v <= G.party[i].level) K.push_back(s);
    }
}

static int escapeChance() {
    int minP = 99, maxE = 0;
    for (int i = 0; i < F(); i++) if (G.party[i].hp != 0) minP = std::min(minP, (int)G.party[i].agi);
    for (int j = 0; j < enemyCount; j++) if (enemies[j].hp != 0) maxE = std::max(maxE, (int)enemies[j].agi);
    int c;
    if (maxE < minP) c = 95;
    else c = (maxE ? minP * 100 / maxE : 0) - 10;
    c += maxLuck() / 10;
    return std::max(c, 0);
}

static int autoHeal(int i, int& target) {
    int cHalf = 0, cQuarter = 0, worst = 0, deficit = 0;
    for (int j = 0; j < F(); j++) {
        const Battler& b = G.party[j];
        if (b.hp <= 0) continue;
        if (b.hp <= b.maxhp / 2) cHalf++;
        if (b.hp <= b.maxhp / 4) cQuarter++;
        if (b.maxhp - b.hp > deficit) { deficit = b.maxhp - b.hp; worst = j; }
    }
    const Battler& me = G.party[i];
    int aid = me.actorId;
    auto castable = [&](int type, std::vector<int>& out) {
        out.clear();
        if (aid < 0 || aid > 7) return;
        for (int s = 0; s < 100; s++) {
            int v = G.learn[aid][s];
            if (v >= 1 && v <= me.level && me.mp >= data::magic[s].cost && data::magic[s].type == type) out.push_back(s);
        }
    };
    std::vector<int> C;
    if (cQuarter > 0) { if (cQuarter == 1) castable(11, C); if (C.empty()) castable(12, C); }
    if (C.empty() && cHalf > 0) { if (cHalf == 1) castable(11, C); if (C.empty()) castable(12, C); }
    if (C.empty()) return -1;
    int best = C[0], bd = std::abs(data::magic[C[0]].power - deficit);
    for (int s : C) { int d = std::abs(data::magic[s].power - deficit); if (d < bd) { bd = d; best = s; } }
    target = data::magic[best].type == 11 ? worst : 0;
    return best;
}

static void partyAction(int i) {
    ActionRec& a = G.actions[i];
    Battler& me = G.party[i];
    sjis nm = battlerName(me);
    if (a.command == -1 || me.hp <= 0) return;
    if (me.status & 0x02) { msg(nm + S(49), 5); return; }
    if (me.status & 0x10) { msg(nm + S(50), 5); return; }
    std::vector<int> P, E;
    for (int k = 0; k < F(); k++) if (G.party[k].hp > 0) P.push_back(k);
    for (int j = 0; j < enemyCount; j++) if (enemies[j].hp > 0) E.push_back(j);
    partyListCount = (int)P.size();
    if (P.empty() || E.empty()) return;
    bool confused = false;
    if (me.status & 0x08) {
        msg(nm + S(51), 5);
        if (rng::R() % 100 < 75) {
            a.command = (rng::R() % 100 < 40) ? 1 : 0;
            if (a.command == 1) {
                std::vector<int> K;
                knownSpells(i, K);
                if (K.empty()) a.command = 0;
                else a.spell = K[(int)(rng::R() % (uint32_t)(K.size() * 10)) / 10];
            } else a.spell = -1;
            a.target = (rng::R() % 100 < 40) ? P[rng::R() % P.size()] : E[rng::R() % E.size()] + 100;
            confused = true;
        }
    }
    if (a.command == 3) {
        msg(F() < 2 ? nm + S(123) : nm + sj::S("たち") + S(123), 5);
        if (G.battleEscapeForbidden) msg(S(124), 5);
        else {
            int r = (int)(rng::R() % 100);
            int c = escapeChance();
            if (r < c) exitCode = -1;
            else msg(S(125), 5);
        }
        return;
    }
    int t = a.target;
    if (autoBattle && !confused && !(me.status & 0x04)) {
        int tt = 0;
        int s = autoHeal(i, tt);
        if (s >= 0) { a.command = 1; a.spell = s; t = tt; }
    }
    if (t == -1 && a.command != 2) return;
    int ST = -1, itemType = -1;
    if (a.command == 1 && a.spell >= 0 && a.spell < 100) ST = data::magic[a.spell].type;
    if (a.command == 2 && a.item >= 0 && a.item < 100) {
        itemType = data::items[a.item].type;
        if (itemType > 17 && itemType - 18 < 100) ST = data::magic[itemType - 18].type;
    }
    if (t != -1) {
        bool enemySide = t >= 100;
        int count = enemySide ? enemyCount : F();
        int k = enemySide ? t - 100 : t;
        bool dead = (k >= 0 && k < (enemySide ? 10 : 8)) ? bat(t).hp <= 0 : true;
        if (ST != 20 && ST != 21 && ST != 1 && ST != 12 && ST != 16 && dead && !(a.command == 2 && itemType == 10)) {
            int best = -1, bd = -1;
            for (int j = 0; j < count; j++) {
                const Battler& b = enemySide ? enemies[j] : G.party[j];
                if (b.hp > 0 && b.maxhp - b.hp > bd) { bd = b.maxhp - b.hp; best = j; }
            }
            k = best >= 0 ? best : 0;
            t = enemySide ? k + 100 : k;
        }
    }
    switch (a.command) {
        case 0:
            msg(nm + sj::S("の攻撃"), 5);
            if (t >= 100) playAnimation(99, t - 100, -1);
            physicalAttack(i, t, 0);
            break;
        case 1: castSpell(a.spell, i, t, false); break;
        case 2: useItem(a.item, i, t); break;
        default: break;
    }
}

// ------------------------------------------------------------------ enemy action (08 §3.6)
static int lowestHp(const std::vector<int>& L, bool enemy) {
    int cand = -1, thr = 9999;
    for (int x : L) {
        int hp = enemy ? enemies[x].hp : G.party[x].hp;
        if (hp <= thr) { cand = x; thr = hp; }
    }
    if (cand < 0) cand = (int)(rng::R() % L.size());
    return cand;
}

static void enemyAttack(int e, int action, bool conf, int t, const std::vector<int>& P) {
    int hits = action == 3 ? 2 : 1;
    sjis nm = battlerName(enemies[e]);
    for (int h = 0;; h++) {
        if (h == 0) { jump(e, 0); msg(nm + sj::S("の攻撃"), 5); }
        else msg(S(126), 5);
        if (!conf && h == 0) {
            int r = (int)(rng::R() % 100);
            if (r < 30) t = P[0];
            else if (r < 60 && P.size() >= 2) t = P[1];
            else if (r < 90) {
                do { t = P[rng::R() % P.size()]; } while (G.party[t].hp <= 0);
            } else t = lowestHp(P, false);
        }
        if (t >= 100) playAnimation(99, t - 100, e);
        physicalAttack(e + 100, t, action);
        int X = t >= 100 ? enemies[(t - 100) % 10].hp : data::monsters[t % 100].maxhp;
        if (!((t >= 100 || G.party[t & 7].hp > 0) && X > 0 && h + 1 < hits)) break;
    }
}

static void enemyAction(int e) {
    Battler& me = enemies[e];
    sjis nm = battlerName(me);
    if (me.hp <= 0) return;
    if (me.status & 0x02) { msg(nm + S(49), 10); return; }
    if (me.status & 0x10) { msg(nm + S(50), 10); return; }
    bool conf = me.status & 0x08;
    if (conf) msg(nm + S(51), 10);
    std::vector<int> P, E;
    for (int k = 0; k < F(); k++) if (G.party[k].hp > 0) P.push_back(k);
    for (int j = 0; j < 10; j++) if (enemies[j].hp > 0) E.push_back(j);
    partyListCount = (int)P.size();
    if (P.empty() || E.empty()) return;
    int m = 0;
    while (m < 10 && me.pattern[m]) m++;
    int action = m == 0 ? 1 : me.pattern[rng::R() % m];
    int t = 0;
    if (conf) t = (rng::R() % 100 < 50) ? P[rng::R() % P.size()] : E[rng::R() % E.size()] + 100;
    if (action >= 1 && action <= 3) enemyAttack(e, action, conf, t, P);
    else if (action == 4) msg(nm + S(127), 5);
    else if (action == 5) {
        msg(nm + S(128), 5);
        me.hp = 0;
        enemyRemoval(e, 0);
    } else {
        int s = action - 10;
        if (s < 0 || s > 99) { action = 1; enemyAttack(e, 1, conf, t, P); }
        else {
            if (!conf) {
                if (data::magic[s].type >= 11) t = lowestHp(E, true) + 100;
                else t = P[rng::R() % P.size()];
            }
            if (me.mp < data::magic[s].cost) { action = 1; enemyAttack(e, 1, conf, t, P); }
            else { jump(e, 0); castSpell(s, e + 100, t, false); }
        }
    }
    if (action != 4 && action != 5) jump(e, 1);
}

// ------------------------------------------------------------------ turn (08 §3)
static void endOfTurn() {
    static const uint8_t red[4] = {255, 0, 0, 0};
    for (int i = 0; i < F(); i++) {
        Battler& b = G.party[i];
        if (b.hp <= 0) continue;
        if (b.status & 0x01) {
            msg(battlerName(b) + S(132), 5);
            disp::paletteEffect(0, red, false);
            applyDamage(i, 5, 0);
            statusRefresh();
        }
        if (b.status & 0x10) {
            if (b.sleep == 0) {
                if (rng::R() % 3 == 0) { b.status &= ~0x10; msg(battlerName(b) + S(44), 10); statusRefresh(); }
            } else b.sleep--;
        }
    }
    for (int j = 0; j < 10; j++) {
        Battler& b = enemies[j];
        if (b.hp <= 0) continue;
        if (b.status & 0x01) { msg(battlerName(b) + S(132), 10); applyDamage(j + 100, 5, 0); }
        if (b.status & 0x10) {
            if (b.sleep == 0) { if (rng::R() % 2 == 0) { b.status &= ~0x10; msg(battlerName(b) + S(44), 10); } }
            else b.sleep--;
        }
    }
}

static void victory();

static void executeTurn() {
    ui::destroy(2);
    for (;;) {
        partyListCount = F();
        rebuildEnemyList();
        struct Ent { int key, who; };
        std::vector<Ent> gen, order;
        for (int i = 0; i < F(); i++) if (G.party[i].hp > 0) gen.push_back({std::max(G.party[i].agi + (int)(rng::R() % 10), -1000), i});
        for (int j = 0; j < enemyCount; j++) if (enemies[j].hp > 0) gen.push_back({std::max(enemies[j].agi + (int)(rng::R() % 10), -1000), 100 + j});
        for (auto& e : gen) {
            size_t p = 0;
            while (p < order.size() && order[p].key > e.key) p++;
            order.insert(order.begin() + p, e);
        }
        for (auto& e : order) {
            if (e.who < 100) partyAction(e.who); else enemyAction(e.who - 100);
            statusRefresh();
            if (exitCode != 0 || G.quit) return;
            if (partyDefeated()) break;
        }
        if (partyDefeated()) {
            rt::wait(rt::messageSpeed() * 2);
            G.battleLost = true;
            plat::setBusyCursor(false);
            return;
        }
        if (allEnemiesDead()) { victory(); return; }
        endOfTurn();
        ui::setText(0, 1, S(131));
        bool any = false;
        for (int i = 0; i < F(); i++) if (canInput(i)) any = true;
        if (!any) continue;
        break;
    }
    if (!autoBattle) createCommandWindow();
}

static void nextMember() {
    for (int s : {7, 11, 5, 4, 6}) ui::destroy(s);
    if (ui::exists(2)) { ui::Notif n; n.type = ui::N_ACTIVATE; n.slot = 2; ui::post(n); }
    curInput++;
    while (curInput < F() && !canInput(curInput)) curInput++;
    if (curInput >= F()) {
        ui::destroy(2);
        curInput = F();
        updateMark();
        curInput = 0;
        executeTurn();
        if (exitCode != 0 || G.battleLost || allEnemiesDead() || G.quit) return;
    }
    int k = curInput;
    while (k < F() && !canInput(k)) k++;
    if (k < F()) {
        curInput = k;
        if (ui::exists(2)) ui::setDisabled(2, 2, (G.party[k].status & 0x04) != 0);
        updateMark();
    }
}

static void back() {
    for (int k = curInput - 1; k >= 0; k--) if (canInput(k)) { curInput = k; break; }
    updateMark();
}

// ------------------------------------------------------------------ victory and level up (08 §6)
static sjis D(const char* name, int o, int n) {
    if (n > o) return sj::S(name) + sj::S("が") + num(n - o) + sj::S("ポイント上がった") + "\n";
    if (n < o) return sj::S(name) + sj::S("が") + num(o - n) + sj::S(" ポイント下がった") + "\n";
    return "";
}

static void levelUp(int i) {
    Battler& b = G.party[i];
    int gain = i < 4 ? expTotal : expTotal / 2;
    b.exp = std::min(b.exp + gain, 100000);
    if (b.level >= 100) return;
    int n = b.level;
    while ((int64_t)n * n * 10 <= b.exp) n++;
    if (n == b.level) return;
    int newL = std::min(n, 100);
    audio::playSlotBGM(10);
    sjis nm = battlerName(b);
    msg(nm + sj::S("はレベルが上がった!"), 7);
    int oVit = b.vit, oPow = b.pow, oInt = b.intl, oAgi = b.agi, oLuck = b.luck, oMhp = b.maxhp, oMmp = b.maxmp, oAtk = b.atk, oDef = b.def;
    int aid = b.actorId;
    if (aid < 0 || aid > 7) { b.level = (int16_t)newL; return; }
    const Actor& A = data::gd.actors[aid];
    int oldIdx = clampi(b.level - 1, 0, 99);
    auto grow = [&](int16_t& st, const uint8_t* curve) { st = s16(st + curve[clampi(newL - 1, 0, 99)] - curve[oldIdx]); if (st > 9999) st = 9999; };
    grow(b.pow, A.curvePow);
    grow(b.vit, A.curveVit);
    grow(b.intl, A.curveInt);
    grow(b.agi, A.curveAgi);
    grow(b.luck, A.curveLuck);
    auto pw = [&](int s) { int e = G.equip[aid][s]; return (e >= 0 && e < 100) ? (int)data::items[e].power : 0; };
    if (b.intl != oInt) b.maxmp = s16(std::min((int)(rng::R() % 5) + (b.intl - 1) * 10, 9999));
    if (b.pow != oPow) b.atk = s16(std::min(b.pow + pw(0), 9999));
    if (b.vit != oVit) {
        b.maxhp = s16(std::min((int)(rng::R() % 5) + b.vit * 10, 9999));
        b.def = s16(std::min(b.vit + pw(1) + pw(2) + pw(3), 9999));
    }
    if (b.hp > b.maxhp) b.hp = b.maxhp;
    if (b.mp > b.maxmp) b.mp = b.maxmp;
    msg(sj::S("レベルが ") + num(b.level) + sj::S(" から ") + num(newL) + sj::S(" に上がった") + "\n" +
            D("最大ＨＰ", oMhp, b.maxhp) + D("最大ＭＰ", oMmp, b.maxmp) + D("力", oPow, b.pow), 20);
    sjis t = D("体力", oVit, b.vit) + D("知力", oInt, b.intl) + D("素早さ", oAgi, b.agi) + D("運", oLuck, b.luck);
    if (!t.empty()) msg(t, 20);
    t = D("攻撃力", oAtk, b.atk) + D("防御力", oDef, b.def);
    if (!t.empty()) msg(t, 20);
    for (int l = b.level + 1; l <= newL; l++)
        for (int s = 0; s < 100; s++)
            if (G.learn[aid][s] == l) {
                if (S(133) == sj::S("未使用")) msg(nm + sj::S("は ") + data::magic[s].name + sj::S(" の魔法を覚えた"), 20);
                else msg(nm + sj::S("は ") + data::magic[s].name + " " + S(133), 20);
            }
    b.level = (int16_t)newL;
}

static void victory() {
    plat::setBusyCursor(false);
    audio::playSlotBGM(9);
    partyListCount = F();
    sjis text;
    if (goldTotal > 0) text = num(goldTotal) + data::gd.coin + sj::S("獲得") + "\n";
    if (expTotal > 0) {
        if (F() == 1) text += battlerName(G.party[0]) + sj::S("は") + num(expTotal) + sj::S("pointの経験値を獲得");
        else text += sj::S("それぞれ ") + num(expTotal) + sj::S("pointの経験値を獲得");
    }
    if (!text.empty()) { msg(text, 10); G.money += goldTotal; }
    for (int i = 0; i < 8; i++) { G.party[i].atk = savedStats[i][0]; G.party[i].def = savedStats[i][1]; G.party[i].agi = savedStats[i][2]; }
    for (int i = 0; i < G.partySize; i++) if (G.party[i].hp > 0) levelUp(i);
    if (dropped >= 0 && dropped < 100) {
        msg(S(129), 10);
        msg(data::items[dropped].name + S(130), 10);
        G.itemCount[dropped]++;
        if (G.itemCount[dropped] == 1) ev::refreshAllPages();
        if (G.itemCount[dropped] >= data::gd.itemMax) G.itemCount[dropped] = (int16_t)data::gd.itemMax;
    }
    rt::wait(30);
}

// ------------------------------------------------------------------ input dispatch (08 §2)
static void fillList(int slot, int page) {
    std::vector<int> L;
    if (slot == 4) {
        std::vector<int> K;
        knownSpells(curInput, K);
        L = K;
    } else if (slot == 5) {
        for (int i = 0; i < 100; i++) if (G.itemCount[i] > 0 && data::items[i].type > 5) L.push_back(i);
    } else if (slot == 7) {
        for (int i = 0; i < 100 && G.teleportList[i] > 0; i++) L.push_back(G.teleportList[i]);
    } else return;
    int count = (int)L.size();
    for (int i = 0; i < 10; i++) {
        int e = page * 10 + i;
        if (e >= count) { ui::pagedClearRest(slot, i); ui::sendEndOfList(slot); return; }
        int id = L[e];
        if (slot == 4) ui::pagedSetRow(slot, i, data::magic[id].name, fmt("%dpts.", data::magic[id].cost), data::magic[id].cost > G.party[curInput].mp);
        else if (slot == 5) ui::pagedSetRow(slot, i, data::items[id].name, fmt("(%d)", G.itemCount[id]), false);
        else ui::pagedSetRow(slot, i, (id >= 0 && id < 500) ? data::gd.maps[id].name : sjis(), "", false);
        if (count - 1 <= e) { ui::pagedClearRest(slot, i + 1); ui::sendEndOfList(slot); return; }
    }
}

static std::vector<int> g_list4, g_list5, g_list7;

static void openPaged(int slot, int x, int y, const std::vector<int>& L) {
    if (ui::exists(slot)) return;
    ui::Window* w = ui::create(slot, x, y, 240, 210, true, 10, ui::LEFT_WRAP, "", (int)L.size());
    w->labels[45].text = "(";
    w->labels[46].text = "1";
    w->labels[47].text = fmt("/%d )", ((int)L.size() - 1) / 10 + 1);
    fillList(slot, 0);
}

void pageChanged(int slot, int page) { fillList(slot, page); }

static int g_Y0 = 250;

static void enemyTargetWindow(int slot, int x) {
    rebuildEnemyList();
    sjis t;
    for (size_t k = 0; k < enemyList.size(); k++) t += battlerName(enemies[enemyList[k]]) + (k + 1 < enemyList.size() ? "\n" : "");
    int n = (int)enemyList.size();
    ui::listHelper(x, g_Y0 - ui::LineH * n / 2 - 10, 160, n, t, 0, ui::LEFT_WRAP, slot, 0, false);
}

static void partyTargetWindow(int nForY, int width, int column) {
    int n = F();
    sjis t;
    for (int k = 0; k < n; k++) t += battlerName(G.party[k]) + (k + 1 < n ? "\n" : "");
    int y = g_Y0 - ui::LineH * nForY / 2 - 10;
    int h = ui::LineH * n + 6;
    if (y + h > 480) y = 440 - h;
    ui::listHelper(180, y, width, n, t, 0, ui::LEFT_WRAP, 11, 0, true, column);
}

static void destList(int owner) {
    if (G.teleportForbidden == 1 || G.riding) { rt::popup(owner, S(96)); return; }
    std::vector<int> L;
    for (int i = 0; i < 100 && G.teleportList[i] > 0; i++) L.push_back(G.teleportList[i]);
    if (L.empty()) { rt::popup(owner, S(97)); return; }
    { ui::Notif n; n.type = ui::N_DEACTIVATE; n.slot = owner; ui::post(n); }
    g_list7 = L;
    openPaged(7, 180, 140, L);
}

static void startAutoBattle() {
    autoBattle = true;
    plat::setBusyCursor(true);
    ui::destroy(2);
    while (exitCode == 0 && !G.quit && autoBattle) {
        rebuildEnemyList();
        int n = (int)enemyList.size();
        if (n == 0) break;
        G.actions[curInput].command = 0;
        int r = (int)(rng::R() % 100);
        int res = -1;
        if (r < 50) {
            int thr = 9999;
            for (int j : enemyList) if (enemies[j].hp <= thr) { res = j; thr = enemies[j].hp; }
            if (res < 0) res = (int)(rng::R() % n);
        } else if (r < 70) {
            int thr = -1;
            for (int j : enemyList) if (enemies[j].hp >= thr) { res = j; thr = enemies[j].hp; }
            if (res < 0) res = (int)(rng::R() % n);
        } else res = enemyList[rng::R() % n];
        G.actions[curInput].target = res + 100;
        nextMember();
        if (allEnemiesDead() || partyDefeated() || exitCode != 0 || G.battleLost) break;
    }
}

void onSelected(int slot, int k, bool byMouse) {
    (void)byMouse;
    ActionRec& a = G.actions[curInput & 7];
    switch (slot) {
        case 2: {
            if (allEnemiesDead() || partyDefeated() || exitCode != 0) return;
            lastCmd = k;
            g_Y0 = ui::haveSelMouse ? ui::selMouseY : 250;
            rebuildEnemyList();
            if (ui::exists(4) || ui::exists(5)) return;
            a.command = k;
            if (k == 0) enemyTargetWindow(6, 90);
            else if (k == 1) {
                std::vector<int> K;
                knownSpells(curInput, K);
                if (K.empty()) rt::popup(2, S(94));
                else { g_list4 = K; openPaged(4, 80, 170, K); }
            } else if (k == 2) {
                std::vector<int> L;
                for (int i = 0; i < 100; i++) if (G.itemCount[i] > 0 && data::items[i].type > 5) L.push_back(i);
                if (L.empty()) rt::popup(2, S(92));
                else { g_list5 = L; openPaged(5, 80, 170, L); }
            } else if (k == 3) nextMember();
            else if (k == 4) startAutoBattle();
            break;
        }
        case 6:
            if (k >= 0 && k < (int)enemyList.size()) a.target = enemyList[k] + 100;
            nextMember();
            break;
        case 4: {
            if (k < 0 || k >= (int)g_list4.size()) return;
            int s = g_list4[k];
            g_Y0 = ui::haveSelMouse ? ui::selMouseY : 250;
            { ui::Notif n; n.type = ui::N_DEACTIVATE; n.slot = 4; ui::post(n); }
            a.spell = s;
            a.target = 0;
            int type = data::magic[s].type;
            if (type < 11) {
                a.target = 100;
                if (type == 1) nextMember();
                else enemyTargetWindow(11, 180);
            } else {
                a.target = 0;
                if (type == 12 || type == 21) nextMember();
                else if (type == 20) destList(4);
                else {
                    int nOld = partyListCount;
                    partyListCount = F();
                    partyTargetWindow(nOld, 160, 0);
                }
            }
            break;
        }
        case 5: {
            if (k < 0 || k >= (int)g_list5.size()) return;
            int i = g_list5[k];
            g_Y0 = ui::haveSelMouse ? ui::selMouseY : 250;
            int T = data::items[i].type;
            int Sx = (T >= 18 && T - 18 < 100) ? data::magic[T - 18].type : -1;
            a.item = i;
            a.target = -1;
            if (T < 6) nextMember();
            else if (Sx == 12) { a.target = 0; nextMember(); }
            else if (Sx == 1) { a.target = 100; nextMember(); }
            else if (Sx == 21) { a.target = 0; nextMember(); }
            else if (Sx == 20) destList(5);
            else {
                { ui::Notif n; n.type = ui::N_DEACTIVATE; n.slot = 5; ui::post(n); }
                if (Sx >= 0 && Sx <= 10) { a.target = 100; enemyTargetWindow(11, 180); }
                else {
                    a.target = 0;
                    partyListCount = F();
                    partyTargetWindow(F(), 240, (Sx == 13 || T == 7 || T == 15) ? 2 : 1);
                }
            }
            break;
        }
        case 11:
            if (a.target == 0) a.target = k;
            else if (k >= 0 && k < (int)enemyList.size()) a.target = enemyList[k] + 100;
            nextMember();
            break;
        case 7:
            if (k >= 0 && k < (int)g_list7.size()) a.target = g_list7[k];
            nextMember();
            break;
        default: break;
    }
}

void onCancelled(int slot) {
    auto act = [](int s) { ui::Notif n; n.type = ui::N_ACTIVATE; n.slot = s; ui::post(n); };
    switch (slot) {
        case 2: back(); break;
        case 6: case 5: case 4: act(2); ui::destroy(slot); break;
        case 11: act(lastCmd == 1 ? 4 : 5); ui::destroy(11); break;
        case 7: if (ui::exists(5)) act(5); if (ui::exists(4)) act(4); ui::destroy(7); break;
        case 12: ui::destroy(12); break;
        case 1: break;
        case 0: if (ui::exists(6) || lastCmd == 2) break; ui::destroy(0); break;
        default: break;
    }
}

// ------------------------------------------------------------------ start / end (08 §1, §7)
static bool setup() {
    autoBattle = false;
    expTotal = goldTotal = 0;
    dropped = -1;
    curInput = 0;
    lowered = -1;
    for (int i = 0; i < 8; i++) { savedStats[i][0] = G.party[i].atk; savedStats[i][1] = G.party[i].def; savedStats[i][2] = G.party[i].agi; }
    for (int j = 0; j < 10; j++) {
        Battler& E = enemies[j];
        if (j >= enemyCount) { E.hp = 0; continue; }
        int id = troop.m[j].id;
        const Battler& c = data::monsters[(id >= 0 && id < 100) ? id : 0];
        E.hp = c.maxhp; E.maxhp = c.maxhp; E.mp = c.maxmp; E.maxmp = c.maxmp; E.agi = c.agi; E.unused2a = c.unused2a;
        E.exp = c.exp; E.gold = c.gold; E.dropItem = c.dropItem; E.dropProb = c.dropProb; E.def = c.def; E.atk = c.atk;
        memcpy(E.rate, c.rate, 100);
        memcpy(E.pattern, c.pattern, 10);
        E.status = 0; E.sleep = 0;
        memset(E.name, 0, sizeof E.name);
        size_t n = 0;
        while (n < 21 && c.name[n]) n++;
        memcpy(E.name, c.name, n);
    }
    sjis bgName = fmt("bg%02d.bmp", (uint16_t)troop.bg + 1);
    std::vector<uint8_t> f;
    if (!fsys::read(bgName, f)) { errorBox(fullPathSjis(bgName)); return false; }
    Bitmap bmp;
    if (decodeBmp(f, bmp)) {
        disp::loadPaletteFrom(bmp, false);
        Surface bg;
        convertBitmap(bmp, disp::realised, bg);
        blitClipped(bg, disp::cache, 0, 0);
    }
    if (!fsys::exists("MONSTPIC.DAT")) { errorBox("MONSTPIC.DAT"); return false; }
    for (int j = 0; j < 10; j++) { monOk[j] = false; monSheet[j] = Surface(); }
    for (int j = 0; j < enemyCount; j++) {
        MonsterPic mp;
        data::loadMonsterPic(troop.m[j].id, mp);
        if (!mp.ok) continue;
        RGB table[256];
        for (int i = 0; i < 256; i++) table[i] = RGB{mp.tableRaw[i * 4 + 2], mp.tableRaw[i * 4 + 1], mp.tableRaw[i * 4]};
        convertIndexed(mp.w, mp.h, mp.idx.data(), table, 256, disp::realised, monSheet[j]);
        monSheet[j].key = monSheet[j].px[0];
        monOk[j] = true;
    }
    disp::back.px = disp::cache.px;
    for (int j = 0; j < enemyCount; j++) drawMonster(j, 0);
    disp::paletteEffect(4, nullptr, false);
    disp::show(G.trans[2]);
    G.screenState = SS_MAP;
    rt::wait(3);
    ui::create(0, 92, 10, 456, 92, false, 0, 0, "", 0);
    ui::setText(0, 1, S(70));
    ui::Window* w = ui::create(1, 20, 362, 600, 100, false, 0, 0, "", 0);
    w->labels[12].text = "Name"; w->labels[13].text = "Hp"; w->labels[14].text = "Mp"; w->labels[15].text = "Status";
    statusRefresh();
    sjis names;
    for (int i = 0; i < F(); i++) names += battlerName(G.party[i]) + "\n";
    ui::setText(1, 10, names);
    partyListCount = F();
    rebuildEnemyList();
    createCommandWindow();
    curInput = 0;
    while (curInput < F() && !canInput(curInput)) curInput++;
    updateMark();
    // 1.8: the command window was created for member 0 (1.7.2); a silenced first input member only disables row 1
    if (curInput < F() && ui::exists(2) && (G.party[curInput].status & 0x04)) ui::setDisabled(2, 2, true);
    return true;
}

static void redrawMapAndShow() {
    mapx::fullTileRedraw();
    mapx::tickRender(-1, 0);
    disp::show(G.trans[1]);
    G.screenState = SS_MAP;
}

static void endBattle() {
    plat::setBusyCursor(false);
    if (partyDefeated()) G.battleLost = true;
    ui::destroyAll();
    for (int j = 0; j < 10; j++) { monSheet[j] = Surface(); monOk[j] = false; }
    for (int i = 0; i < F(); i++) G.party[i].status &= 0x0003;
    disp::hide(exitCode > 0 ? 13 : G.trans[2], true);
    G.screenState = SS_ERASED;
    audio::battleEndUnload();
    if (G.quit) { G.inBattle = false; return; }
    disp::loadMapGraphics(data::gd.maps[G.curMap].chip);
    if (!G.battleLost && !G.passAbort && exitCode < 1) audio::resumeBGM(false);
    for (int i = 0; i < 8; i++) if (G.party[i].hp < 0) G.party[i].status = 0;
    if (G.battleLost) { G.inBattle = false; G.passAbort = true; return; }
    if (!G.passAbort) {
        if (exitCode < 1) redrawMapAndShow();
        else {
            const MapRec& a = data::gd.maps[exitCode];
            int saved = G.trans[0];
            G.trans[0] = 13;
            G.inBattle = false;
            mapx::teleport(a.parent, a.exitX, a.exitY);
            G.trans[0] = saved;
            audio::resumeBGM(false);
        }
    }
    G.inBattle = false;
    G.drag = false;
    G.sameDirCount = 0;
    hero()->moveType = MT_PLAYER;
}

void start(int t, bool escapeForbidden) {
    if (t < 0 || t >= 250) return;
    troop = data::troops[t];
    if (troop.count <= 0) return;
    enemyCount = std::min(troop.count, 10);
    G.inBattle = true;
    G.drag = false;
    G.sameDirCount = 0;
    ui::destroy(0);
    audio::saveBGM();
    audio::playBGM(sj::S("演奏停止"), nullptr, false);
    if (G.slots[7].hasName && G.slots[7].name.find(".mid") != sjis::npos) audio::setPlayOnce(true);
    audio::playJingle(7);
    static const uint8_t white[4] = {200, 200, 200, 0};
    disp::paletteEffect(1, white, false);
    disp::hide(G.trans[1], true);
    disp::hide(0, true);
    G.screenState = SS_ERASED;
    audio::waitJingle(7);
    audio::playSlotBGM(8);
    exitCode = 0;
    G.battleEscapeForbidden = escapeForbidden;
    lastCmd = 0;
    if (!setup()) {
        if (!G.passAbort) redrawMapAndShow();
        G.screenState = SS_MAP;
        G.inBattle = false;
        G.drag = false;
        G.sameDirCount = 0;
        hero()->moveType = MT_PLAYER;
        return;
    }
    plat::discardKeyboardEvents();
    while (!G.quit && !allEnemiesDead() && !partyDefeated() && exitCode == 0 && !G.battleLost) {
        rt::processMessages();
        if (!G.quit && !allEnemiesDead() && !partyDefeated() && exitCode == 0 && !G.battleLost) plat::idle();
    }
    endBattle();
}

}  // namespace battle
