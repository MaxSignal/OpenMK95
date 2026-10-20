// Maps, characters and movement (04-map-movement.md).
#include <algorithm>
#include <cstdio>
#include "engine/display.h"
#include "engine/game.h"
#include "platform/platform.h"
#include "core/rng.h"
#include "engine/runtime.h"
#include "core/sjis.h"

Game G;

namespace rt { int g_mapGeneration = 0; }
using rt::g_mapGeneration;

namespace mapx {

static const uint16_t kPassMask[7] = {0x0C8F, 0x0481, 0x0004, 0x0030, 0x0060, 0x3DFF, 0x3FFF};

// The hero of the movement rules (04 §3.1): party position 0's character. The main loop re-fetches it
// at the start of every pass; party slot 0 only changes mid-pass on boarding completion (after which
// the vehicle is the hero) and at the start of alighting (which aborts the pass; the leader's first
// step is started without tests, see g_noTests).
static bool g_noTests = false;
static inline bool isHero(const Chara* c) { return !g_noTests && c == hero(); }

static bool passable(int vt, int a) {
    if (vt < 0 || vt > 6) vt = 0;
    return (kPassMask[vt] >> (a & 31)) & 1;
}

static int tileAt(int x, int y) {
    if (G.W <= 0 || G.H <= 0) return 0;
    x = ((x % G.W) + G.W) % G.W;
    y = ((y % G.H) + G.H) % G.H;
    size_t i = (size_t)y * G.W + x;
    return i < G.tiles.size() ? G.tiles[i] : 0;
}

static Chara*& gridAt(int L, int c, int r) {
    static Chara* dummy = nullptr;
    if (L < 0 || L > 2 || c < -1 || c > 20 || r < -1 || r > 15) { dummy = nullptr; return dummy; }
    return G.grid[L][r + 1][c + 1];
}

static Chara* gridGet(int L, int c, int r) {
    if (L < 0 || L > 2 || c < -1 || c > 20 || r < -1 || r > 15) return nullptr;
    return G.grid[L][r + 1][c + 1];
}

void clearGrid() { memset(G.grid, 0, sizeof G.grid); }

// 04 §4.2
static void screenCell(int x, int y, int& c, int& r, bool loopRule) {
    int xx = x < G.ox ? x + G.W : x;
    int yy = y < G.oy ? y + G.H : y;
    c = xx - G.ox;
    r = yy - G.oy;
    if (loopRule) {
        if (c == G.W - 1 && G.W > 21) c = -1;
        if (r == G.H - 1 && G.H > 16) r = -1;
    }
}
static void screenCell(int x, int y, int& c, int& r) { screenCell(x, y, c, r, G.mapType == 1); }

int vehicleLoopStart() { return G.riding ? 1 : 0; }
bool vehicleUpdatable(int k) {
    return k >= 0 && k < 10 && G.vehicles[k].obj && G.vehicles[k].map == G.curMap;
}

// ------------------------------------------------------------------ party
void setupPartyCharacters() {
    for (int a = 0; a < 8; a++) {
        G.actorChar[a].reset(new Chara());
        Chara* c = G.actorChar[a].get();
        c->graphic = data::gd.actors[a].graphic;
        c->moveType = MT_PLAYER;
        c->layer = 1;
        if (data::gd.actors[a].startMember) {
            c->x = c->destX = data::gd.startX;
            c->y = c->destY = data::gd.startY;
        }
    }
    G.partyMask = 0;
    for (int i = 0; i < 8; i++) G.partyChar[i] = nullptr;
    for (int i = 0; i < G.partySize; i++) {
        G.partyChar[i] = G.actorChar[G.partyActor[i]].get();
        G.partyMask |= (uint8_t)(1 << G.partyActor[i]);
    }
    if (G.partySize == 0) G.partyChar[0] = G.actorChar[0].get();
    G.shown = std::max(1, G.partySize);
    resetTrail();
    G.passHero = G.partyChar[0];
}

void resetTrail() { for (int i = 0; i < 8; i++) G.trail[i] = -1; }

void gatherParty() {
    Chara* L = G.partyChar[0];
    for (int i = 1; i < G.shown; i++) {
        Chara* c = G.partyChar[i];
        c->x = c->destX = L->x;
        c->y = c->destY = L->y;
        c->facing = L->facing;
        c->motion = -1;
        c->substep = 0;
    }
    resetTrail();
}

// ------------------------------------------------------------------ camera
static void computeCamera(int x, int y) {
    if (G.mapType == 1) {
        G.ox = (G.W + x - 9) % G.W;
        G.oy = (y + G.H - 6) % G.H;
    } else {
        G.ox = x - 9;
        if (G.ox < 0) G.ox = 0; else if (G.ox > G.W - 20) G.ox = G.W - 20;
        G.oy = y - 6;
        if (G.oy < 0) G.oy = 0; else if (G.oy > G.H - 15) G.oy = G.H - 15;
        if (G.ox < 0) G.ox = 0;
        if (G.oy < 0) G.oy = 0;
    }
    G.cpx = G.ox * 32;
    G.cpy = G.oy * 32;
}

void recomputeScrollFlags() {
    Chara* h = G.partyChar[0];
    int sx = ((h->x - G.ox) % G.W + G.W) % G.W, sy = ((h->y - G.oy) % G.H + G.H) % G.H;
    G.scrollFlag[0] = sy < 8;
    G.scrollFlag[1] = sx > 8;
    G.scrollFlag[2] = sy > 6;
    G.scrollFlag[3] = sx < 10;
    if (G.mapType != 1) {
        if (G.oy == 0) G.scrollFlag[0] = false;
        if (G.ox == G.W - 20) G.scrollFlag[1] = false;
        if (G.oy == G.H - 15) G.scrollFlag[2] = false;
        if (G.ox == 0) G.scrollFlag[3] = false;
        if (G.W <= 20) G.scrollFlag[1] = G.scrollFlag[3] = false;
        if (G.H <= 15) G.scrollFlag[0] = G.scrollFlag[2] = false;
    }
}

static void buildAreaTable() {
    G.areaTable.assign((size_t)G.W * G.H, (uint16_t)G.curMap);
    const MapRec& m = data::gd.maps[G.curMap];
    int c = m.child;
    int guard = 0;
    while (c != 0 && c < 500 && guard++ < 1000) {
        const MapRec& a = data::gd.maps[c];
        if (a.type == 2 && a.areaX < G.W && a.areaY < G.H) {
            int y0 = std::max(0, a.areaY), x0 = std::max(0, a.areaX);
            int y1 = std::min(a.areaY + (int)a.h, G.H), x1 = std::min(a.areaX + (int)a.w, G.W);
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++) G.areaTable[(size_t)y * G.W + x] = (uint16_t)c;
        }
        c = a.sibling;
    }
}

void deleteMapEvents() {
    G.events.clear();
    g_mapGeneration++;
}

// ------------------------------------------------------------------ teleport (04 §11.1)
void teleport(int M, int x, int y) {
    if (M < 0 || M >= 500) return;
    const MapRec& m = data::gd.maps[M];
    if (m.w == 0 || x >= m.w || y >= m.h) return;
    if (m.type == 2 || m.tiles.empty()) return;  // areas cannot be entered
    if (x < 0 || y < 0) return;
    G.arrival = true;
    ui::destroyAll();
    G.menuRequest = false;
    if (G.screenState != SS_ERASED && G.screenState != SS_BEFORE_START) {
        disp::hide(G.trans[0], true);
        G.screenState = SS_ERASED;
    }
    int prev = G.curMap;
    G.W = m.w;
    G.H = m.h;
    G.mapType = m.type;
    G.tiles = m.tiles;
    computeCamera(x, y);
    for (int a = 0; a < 8; a++) {
        Chara* c = G.actorChar[a].get();
        c->motion = -1;
        c->substep = 0;
        c->x = c->destX = x;
        c->y = c->destY = y;
    }
    resetTrail();
    if (G.riding && G.vehicles[0].obj) {
        Chara* v = G.vehicles[0].obj.get();
        v->x = v->destX = x;
        v->y = v->destY = y;
        v->motion = -1;
        v->substep = 0;
        G.vehicles[0].map = M;
    }
    if (M != G.curMap) {
        G.curMap = M;
        G.heroWait.clear();
        clearGrid();
        deleteMapEvents();
        ev::createMapEvents();
        buildAreaTable();
        ev::refreshAllPages();
    }
    G.encArea = G.curMap;
    recomputeScrollFlags();
    G.autoWalkDir = 5;
    G.drag = false;
    plat::captureMouse(false);
    rt::flushKeysNoWait();
    if (prev != -1) rt::showMap();
    G.encSteps = 0;
}

// ------------------------------------------------------------------ drawing
static void drawTile(int t, int dx, int dy) {
    int c = (t >> 8) & 3, n = t & 255;
    blitOpaque(disp::chip[c], Rect::xywh((n % 8) * 32, (n / 8) * 32, 32, 32), disp::back, dx, dy);
}

void fullTileRedraw() {
    for (int r = 0; r < 15; r++)
        for (int c = 0; c < 20; c++) drawTile(tileAt(G.ox + c, G.oy + r), 32 * c, 32 * r);
    disp::cache.px = disp::back.px;
}

static void drawSprite(int g, int col, int dir, int px, int py) {
    Rect src;
    const Surface* sheet;
    if (g < 0x100) {
        src = Rect::xywh((g % 16) * 64 + col * 32, (g / 16) * 128 + dir * 32, 32, 32);
        sheet = &disp::chara;
    } else {
        int t = g - 0x100;
        int n = t & 255;
        sheet = &disp::chip[(t >> 8) & 3];
        src = Rect::xywh((n % 8) * 32, (n / 8) * 32, 32, 32);
    }
    int w = 32, h = 32;
    if (px < 0) { src.l -= px; w += px; px = 0; } else if (px > 608) w = 640 - px;
    if (py < 0) { src.t -= py; h += py; py = 0; } else if (py > 448) h = 480 - py;
    if (w <= 0 || h <= 0) return;
    src.r = src.l + w;
    src.b = src.t + h;
    blitKeyed(*sheet, src, disp::back, px, py, disp::chara.key);
}

static void drawCharacter(Chara* c, bool animate, int partyIdx) {
    c->onScreen = 0;
    if (c->eventId != 0 && c->display == DISP_REMOVED) return;
    bool frozen = !animate;
    if (partyIdx < 0 && G.moveControl == 1 && c->resumeIndex == 0) frozen = true;
    if (partyIdx >= 0 && (G.party[partyIdx].hp < 1 || (G.party[partyIdx].status & 0x02))) frozen = true;
    if (!frozen) {
        c->animTimer += (c->animType == 0x13 || c->animType == 0x14) ? 9 : 2;
        if (c->animTimer > c->speed * 10 + 5) {
            c->animTimer = 0;
            if (c->animType == 0x15) c->animFrame = (int)(rng::R() & 3);
            else c->animFrame = (c->animFrame + 1) & 3;
        }
    }
    // displayed direction (04 §6.3)
    int dir = c->facing & 3;
    int col = c->animFrame & 1;
    int fdir = c->facing & 3;  // facing before the sub-step-3 update
    if (c->display == DISP_VISIBLE && c->graphic < 0x100) {
        bool fromTable = false;
        bool fixed = c->animType == 0x16 || c->animType == 0x18;
        if (c->facingOverride != -1 && !fixed) dir = c->facingOverride & 3;
        else if (c->motion >= 4 && c->motion <= 7) dir = c->motion & 3;
        else {
            fromTable = true;
            bool stepping = c->motion >= 0 && c->motion <= 3;
            switch (c->animType) {
                case 0x13: dir = c->animFrame & 3; break;
                case 0x14: dir = (4 - c->animFrame) & 3; break;
                case 0x15: dir = c->animFrame & 3; break;
                case 0x16: case 0x18: dir = c->facing & 3; break;
                default: dir = stepping ? c->motion : (c->facing & 3); break;
            }
        }
        if (fromTable && (c->animType == 0x17 || c->animType == 0x18)) col = 0;
        c->facingOverride = -1;
    }
    if (c->substep >= 4) c->substep = 0;
    int px = c->x * 32 - G.cpx, py = c->y * 32 - G.cpy;
    if (c->motion >= 0 && c->motion <= 3) { px += DX[c->motion] * 8 * c->substep; py += DY[c->motion] * 8 * c->substep; }
    if (px < -31) px += G.W * 32;
    if (py < -31) py += G.H * 32;
    bool beyond = px > 639 || py > 479;
    bool cart = c->vehicleType == 1 || c->vehicleType == 2;
    int rc = 0, rr = 0;
    bool registered = false;
    if (!beyond) {
        if (c->display == DISP_VISIBLE) drawSprite(c->graphic, col, dir, px, py);
        if (c->display != DISP_REMOVED) {
            screenCell(c->regX(), c->regY(), rc, rr);
            if (rc <= 20 && rr <= 15 && rc >= -1 && rr >= -1) {
                gridAt(c->layer, rc, rr) = (c->moveType == MT_BOARDING) ? nullptr : c;
                c->onScreen = 1;
                registered = true;
            }
        }
    }
    if (cart) {
        int fdx = DX[fdir], fdy = DY[fdir];
        int px2 = c->x * 32 - fdx * 8 * (4 - c->substep) - G.cpx;
        int py2 = c->y * 32 - fdy * 8 * (4 - c->substep) - G.cpy;
        if (G.mapType == 1) { if (px2 < -31) px2 += G.W * 32; if (py2 < -31) py2 += G.H * 32; }
        if (!(px2 < -31 || py2 < -31 || px2 > 639 || py2 > 479) && c->display == DISP_VISIBLE)
            drawSprite(c->vehicleType == 1 ? 62 : 63, col, fdir, px2, py2);
        if (registered) {
            int tc = rc - fdx, tr = rr - fdy;
            if (tc >= -1 && tc <= 20 && tr >= -1 && tr <= 15) gridAt(c->layer, tc, tr) = c;
        }
    }
    if (beyond) return;
    if (c->display != DISP_REMOVED && c->substep == 3) c->facing = dir;
}

static int rowOf(const Chara* c) { return c->substep < 2 ? c->y : c->destY; }

static void drawCharacters(bool animate) {
    for (int L = 0; L <= 2; L++) {
        for (int r = -1; r <= 15; r++) {
            int m = ((G.oy + r) % G.H + G.H) % G.H;
            for (size_t i = 0; i < G.events.size(); i++) {
                Chara* e = G.events[i].get();
                if (e->layer == L && rowOf(e) == m) drawCharacter(e, animate, -1);
            }
            for (int k = vehicleLoopStart(); k < 10; k++) {
                Chara* v = G.vehicles[k].obj.get();
                if (v && G.vehicles[k].map == G.curMap && v->layer == L && rowOf(v) == m) drawCharacter(v, animate, -1);
            }
            Chara* lead = G.partyChar[0];
            if (lead && lead->layer == L && rowOf(lead) == m)
                for (int i = G.shown - 1; i >= 0; i--)
                    if (G.partyChar[i]) drawCharacter(G.partyChar[i], animate, i);
        }
    }
}

static void render(int D, int S, bool tick, bool present, bool animate) {
    if (G.W <= 0 || G.H <= 0) return;
    bool scrolling = D >= 0 && D <= 3 && G.scrollFlag[D];
    if (scrolling) {
        G.cpx = (G.W * 32 + G.ox * 32 + DX[D] * 8 * S) % (G.W * 32);
        G.cpy = (G.H * 32 + G.oy * 32 + DY[D] * 8 * S) % (G.H * 32);
    }
    int o = scrolling ? 8 * S : 0;
    if (!scrolling || o == 0) disp::back.px = disp::cache.px;
    else {
        Surface& c = disp::cache;
        Surface& b = disp::back;
        switch (D) {
            case 0: blitOpaque(c, Rect(0, 0, 640, 480 - o), b, 0, o); break;
            case 2: blitOpaque(c, Rect(0, o, 640, 480), b, 0, 0); break;
            case 3: blitOpaque(c, Rect(0, 0, 640 - o, 480), b, o, 0); break;
            case 1: blitOpaque(c, Rect(o, 0, 640, 480), b, 0, 0); break;
        }
        for (int i = 0; i < (D == 0 || D == 2 ? 20 : 15); i++) {
            int t, n, ch;
            Rect src;
            int dx, dy;
            if (D == 3) { t = tileAt(G.ox - 1, G.oy + i); n = t & 255; src = Rect::xywh((n % 8) * 32 + 32 - o, (n / 8) * 32, o, 32); dx = 0; dy = 32 * i; }
            else if (D == 1) { t = tileAt(G.ox + 20, G.oy + i); n = t & 255; src = Rect::xywh((n % 8) * 32, (n / 8) * 32, o, 32); dx = 640 - o; dy = 32 * i; }
            else if (D == 0) { t = tileAt(G.ox + i, G.oy - 1); n = t & 255; src = Rect::xywh((n % 8) * 32, (n / 8) * 32 + 32 - o, 32, o); dx = 32 * i; dy = 0; }
            else { t = tileAt(G.ox + i, G.oy + 15); n = t & 255; src = Rect::xywh((n % 8) * 32, (n / 8) * 32, 32, o); dx = 32 * i; dy = 480 - o; }
            ch = (t >> 8) & 3;
            blitOpaque(disp::chip[ch], src, disp::back, dx, dy);
        }
    }
    if (tick && S == 4 && D >= 0 && D <= 3) {
        disp::cache.px = disp::back.px;
        if (G.scrollFlag[D]) {
            G.ox = (G.ox + DX[D] + G.W) % G.W;
            G.oy = (G.oy + DY[D] + G.H) % G.H;
        }
    }
    drawCharacters(animate);
    if (present && G.screenState == SS_MAP) disp::fullPresent();
}

void tickRender(int D, int S) { render(D, S, true, true, true); }

void interimRedraw(bool present) {
    Chara* h = hero();
    if (h->substep == 4) return;
    int D = -1, S = 0;
    if (h->motion >= 0 && h->motion <= 3 && h->substep % 4 > 0) { D = h->motion; S = h->substep; }
    G.heroScrollSub = S;
    render(D, S, false, present, present);
}

// ------------------------------------------------------------------ movement core (04 §7)
void startMotion(Chara* C, int m) {
    if (C->eventId != 0 && C->display == DISP_REMOVED) return;
    C->motion = m;
    if (m == -1) return;
    if (m >= 4 && m <= 7) { C->facing = m & 3; return; }
    if (m == MOTION_WAIT) return;
    if (m < 0 || m > 3) { C->motion = -1; return; }
    int nx = C->x + DX[m], ny = C->y + DY[m];
    if (G.mapType != 1) {
        if (nx < 0 || nx >= G.W) nx = C->x;
        if (ny < 0 || ny >= G.H) ny = C->y;
    } else {
        nx = (nx + G.W) % G.W;
        ny = (ny + G.H) % G.H;
    }
    C->destX = nx;
    C->destY = ny;
    if (nx == C->x && ny == C->y) { C->motion = -1; return; }
    int c, r;
    screenCell(nx, ny, c, r);
    bool off = c < -1 || r < -1 || c > 20 || r > 15;
    bool isEvent = C->eventId != 0;
    if (!(isEvent || isHero(C))) return;
    if (isEvent && !C->hasRoute() && off) { C->motion = -1; return; }
    Chara* below = off ? nullptr : gridGet(0, c, r);
    int t = tileAt(nx, ny);
    if (below && below->graphic >= 0x100) t = below->graphic - 0x100;
    bool blocked = !passable(C->vehicleType, data::atr[t & 1023]);
    if (below && (below->vehicleType == 3 || below->vehicleType == 4))
        blocked = !(C->vehicleType == 0 || C->vehicleType == 5 || C->vehicleType == 6);
    if (C->display == DISP_REMOVED) blocked = false;
    if (blocked) {
        if (C->vehicleType == 1 || C->vehicleType == 2 || C->animType == 0x16 || C->animType == 0x18) C->motion = -1;
        else { C->facing = m; C->motion = m + 4; }
        C->destX = C->x;
        C->destY = C->y;
        return;
    }
    if (C->display != DISP_REMOVED && !off && (C->vehicleType == 1 || C->vehicleType == 2)) {
        Chara* o = gridGet(1, c, r);
        if (o && o != C && o->graphic < 0x100) { C->motion = -1; C->destX = C->x; C->destY = C->y; }
    }
}

Chara* advance(Chara* C) {
    if ((C->eventId != 0 && C->display == DISP_REMOVED) || C->motion == -1) return nullptr;
    if (C->motion >= 0 && C->motion <= 17) C->substep++;
    if (C->motion >= 4 && C->motion <= 7) {
        C->facingOverride = C->motion & 3;
        C->substep = 0;
        C->motion = -1;
        return nullptr;
    }
    if (C->motion == MOTION_WAIT) { if (C->substep == 4) C->motion = -1; return nullptr; }
    if (C->motion < 0 || C->motion > 3) { C->motion = -1; return nullptr; }
    int c, r;
    screenCell(C->destX, C->destY, c, r);
    bool off = c < -1 || r < -1 || c > 20 || r > 15;
    if (off) {
        if (C->substep == 4) { C->x = C->destX; C->y = C->destY; C->motion = -1; }
        return nullptr;
    }
    int L = clampi(C->layer, 0, 2);
    Chara* o = gridGet(L, c, r);
    if (C->eventId == 0) {
        if (isHero(C) && C->substep == 1 && o && o->eventId != 0 && C->display != DISP_REMOVED &&
            C->vehicleType != 5 && C->vehicleType != 6) {
            C->substep = 0;
            if (C->vehicleType == 1 || C->vehicleType == 2) C->motion = -1;
            else { C->facing = C->motion; C->motion += 4; }
            return o;
        }
        gridAt(L, c, r) = C;
    }
    if (C->eventId != 0 && C->substep == 2) {
        if (!o || o->eventId == C->eventId) gridAt(L, c, r) = C;
        else {
            bool touchedHero = (o == hero());
            if (!C->hasRoute()) {
                C->motion = (C->motion + 2) & 3;
                std::swap(C->x, C->destX);
                std::swap(C->y, C->destY);
                C->substep = 4 - C->substep;
            } else C->substep -= 1;
            if (touchedHero) return C;
            return nullptr;
        }
    }
    if (C->substep == 4) { C->x = C->destX; C->y = C->destY; C->motion = -1; }
    return nullptr;
}

// ------------------------------------------------------------------ routes (04 §8.3)
bool routeNext(Chara* C) {
    bool seenLoop = false;
    int guard = 0;
    for (;;) {
        if (++guard > 100000) break;
        int len = (int)C->route.size();
        if (C->routePos < 0 && C->routeMode == ROUTE_PINGPONG) { C->routePos = 0; C->routeStep = 1; }
        else if (C->routePos < 0 || C->routePos >= len) break;
        if (C->routePos >= len) break;
        int b = C->route[C->routePos];
        if (b < 18) {
            int d;
            if (b >= 8 && b <= 15) {
                int k = b - 8;
                Chara* ref = (k & 2) ? C : hero();
                d = ((ref->facing & 3) + (k & 1) * 2) & 3;
                if (k > 3) d += 4;
            } else d = b;
            if (b == 16) d = C->facing & 3;
            else if (C->routeStep == -1) d = ((d + 2) & 3) + (d > 3 ? 4 : 0);
            startMotion(C, d);
            C->routePos += C->routeStep;
            return false;
        }
        if (b <= 24) { C->animType = b; C->routePos += C->routeStep; continue; }
        if (b <= 27) { C->display = b; C->routePos += C->routeStep; continue; }
        if (b == 28) break;
        if (b == 29) {
            if (seenLoop) break;
            C->routeMode = ROUTE_REPEAT; C->routePos = 0; seenLoop = true;
            continue;
        }
        if (b == 30 || b == 31) {
            if (seenLoop) break;
            C->routeMode = b == 30 ? ROUTE_BACK : ROUTE_PINGPONG;
            C->routeStep = -1;
            C->routePos += C->routeStep;
            seenLoop = true;
            continue;
        }
        break;
    }
    C->routeMode = ROUTE_NONE;
    C->routePos = 0;
    C->routeStep = 1;
    C->route.clear();
    return true;
}

void applyLeadingDisplayBytes(Chara* c) {
    while (c->hasRoute() && c->routePos >= 0 && c->routePos < (int)c->route.size()) {
        int b = c->route[c->routePos];
        if (b < 0x19 || b > 0x1B) break;
        c->display = b;
        c->routePos++;
    }
}

// ------------------------------------------------------------------ event update (04 §8.1)
static void aiMove(Chara* E) {
    Chara* h = hero();
    int tx = h->x, ty = h->y;
    switch (E->moveType) {
        case MT_APPROACH:
        case MT_AVOID: {
            int hd, vd;
            if (E->moveType == MT_APPROACH) { hd = (E->x - tx <= 0) ? 1 : 3; vd = (E->y - ty <= 0) ? 2 : 0; }
            else { hd = (E->x - tx >= 0) ? 1 : 3; vd = (E->y - ty >= 0) ? 2 : 0; }
            uint32_t r = rng::R() % 3;
            if (r == 0) startMotion(E, hd);
            else if (r == 1) startMotion(E, vd);
            else startMotion(E, (int)(rng::R() & 3));
            break;
        }
        case MT_RANDOM: startMotion(E, (int)(rng::R() & 3)); break;
        default: break;
    }
}

void updateObject(Chara* E, bool isMapEvent) {
    (void)isMapEvent;
    if (!E) return;
    if (E->cooldown > 0) E->cooldown--;
    if (G.moveControl == 1 && E->resumeIndex == 0) return;
    Chara* h = hero();
    if (E->speed == h->speed && h->speedCounter == 0) E->speedCounter = E->speed;
    E->speedCounter++;
    if (E->speedCounter < E->speed) return;
    E->speedCounter = 0;
    if (E->motion == -1) {
        bool ended = false;
        if (E->hasRoute()) ended = routeNext(E);
        else aiMove(E);
        if (ended) {
            if (G.screenState == SS_MAP) { ui::destroyAll(); G.menuRequest = false; interimRedraw(true); }
            if (E->resumeIndex > 0) {
                int gen = g_mapGeneration;
                ev::runEvent(E->eventId, E);
                if (G.passAbort || gen != g_mapGeneration) return;
            }
        }
    }
    Chara* t = advance(E);
    if (t) bumpTouch(t);
}

// ------------------------------------------------------------------ player control
void heroControlStep() {
    Chara* h = G.passHero = hero();
    if (G.moveControl <= 1 && !h->hasRoute() && G.releaseLatch && (G.lastKey == VK_SPACE_ || G.lastKey == VK_RETURN_)) {
        if (G.riding && h->moveType != MT_ALIGHTING) alight();
        else actionButton();
        if (G.passAbort) return;
    }
    h = G.passHero = hero();
    if (!h->hasRoute() || h->moveType == MT_BOARDING || h->moveType == MT_ALIGHTING) {
        if (h->moveType == MT_PLAYER) {
            if (G.moveControl < 2) {
                static const int keys[4][2] = {{VK_UP_, VK_NUMPAD8_}, {VK_LEFT_, VK_NUMPAD4_}, {VK_DOWN_, VK_NUMPAD2_}, {VK_RIGHT_, VK_NUMPAD6_}};
                static const int dirs[4] = {0, 3, 2, 1};
                for (int i = 0; i < 4; i++) {
                    bool held = false;
                    for (int k = 0; k < 2; k++) {
                        if (plat::keyHeld(keys[i][k])) { held = true; break; }
                        G.releaseLatch = true;
                    }
                    if (held) { startMotion(h, dirs[i]); break; }
                }
            }
        } else if (h->moveType == MT_BOARDING || h->moveType == MT_ALIGHTING) startMotion(h, MOTION_WAIT);
        else if (h->moveType == MT_AUTOWALK) {
            startMotion(h, G.autoWalkDir);
            if (h->motion != G.autoWalkDir) { G.autoWalkDir = 5; h->moveType = MT_PLAYER; }
        } else if (h->moveType == MT_RANDOM) startMotion(h, (int)(rng::R() & 3));
    } else {
        bool ended = routeNext(h);
        if (h->moveType != MT_BOARDING && h->moveType != MT_ALIGHTING)
            for (int a = 0; a < 8; a++) { G.actorChar[a]->display = h->display; G.actorChar[a]->animType = h->animType; }
        if (ended) {
            if (G.screenState == SS_MAP) { ui::destroyAll(); G.menuRequest = false; interimRedraw(true); }
            if (G.heroWait.id != -1) ev::resumeHeroWait();
        }
    }
}

void endAutoWalk() {
    Chara* h = hero();
    if (h && h->moveType == MT_AUTOWALK) h->moveType = MT_PLAYER;
}

void mouseDragMove(int x, int y) {
    if (G.eventRunning || G.inBattle) return;
    Chara* h = hero();
    if (!h) return;
    if (!(h->moveType == MT_PLAYER || h->moveType == MT_AUTOWALK) || h->hasRoute() || !G.drag || G.moveControl >= 2) return;
    int dx = x - G.dragX, dy = y - G.dragY;
    if (std::abs(dx) <= 1 && std::abs(dy) <= 1) return;
    int d = std::abs(dy) < std::abs(dx) ? (dx < 1 ? 3 : 1) : (dy < 1 ? 0 : 2);
    if (h->motion == -1) startMotion(h, d);
    if (d == G.autoWalkDir) {
        if (G.sameDirCount > 10000) G.sameDirCount = 10000; else G.sameDirCount++;
    } else {
        G.autoWalkDir = d;
        if (h->moveType == MT_PLAYER) G.sameDirCount = 0;
    }
    if (G.sameDirCount > 20) h->moveType = MT_AUTOWALK;
    G.dragX = x;
    G.dragY = y;
}

void faceEachOther(Chara* e, bool layerTest) {
    Chara* H = hero();
    if (layerTest && e->layer != H->layer) return;
    int ex = e->substep >= 2 ? e->destX : e->x, ey = e->substep >= 2 ? e->destY : e->y;
    if (ex == H->x && ey == H->y) return;
    int d = ex < H->x ? 3 : ex > H->x ? 1 : (H->y <= ey ? 2 : 0);
    if (H->vehicleType != 1 && H->vehicleType != 2) H->facingOverride = d;
    if (e->vehicleType != 1 && e->vehicleType != 2) e->facingOverride = (d + 2) & 3;
    interimRedraw(true);
}

static int objId(Chara* e) {
    if (e->eventId) return e->eventId;
    for (auto& v : G.vehicles) if (v.obj.get() == e) return v.id;
    return 0;
}

void actionButton() {
    Chara* h = hero();
    if (h->vehicleType == 5 || h->vehicleType == 6 || h->moveType == MT_ALIGHTING || h->substep >= 1) return;
    int hx = ((h->x - G.ox) % G.W + G.W) % G.W, hy = ((h->y - G.oy) % G.H + G.H) % G.H;
    int fdx = DX[h->facing & 3], fdy = DY[h->facing & 3];
    auto usable = [](int c, int r) { return c >= -1 && c <= 20 && r >= -1 && r <= 15; };
    Chara* e = nullptr;
    if (usable(hx + fdx, hy + fdy)) {
        e = gridGet(1, hx + fdx, hy + fdy);
        if (e && e->eventId == 0) e = nullptr;
    }
    if (!e && usable(hx + 2 * fdx, hy + 2 * fdy) && data::atr[tileAt(h->x + fdx, h->y + fdy) & 1023] == 13) {
        e = gridGet(1, hx + 2 * fdx, hy + 2 * fdy);
        if (e && e->eventId == 0) e = nullptr;
    }
    if (!e) e = gridGet(2, hx, hy);
    if (!e) e = gridGet(0, hx, hy);
    if (!e || e == h || e->resumeIndex >= 1 || e->cooldown > 0) return;
    faceEachOther(e, true);
    ev::runEvent(objId(e), e);
    rt::flushKeysWait();
}

void bumpTouch(Chara* e) {
    Chara* h = hero();
    if (h->vehicleType == 5 || h->vehicleType == 6 || h->moveType == MT_ALIGHTING || h->substep >= 1 || e == h) return;
    if (e->page < 0 || e->attr.trigger(e->page) != 1 || e->cooldown != 0 || e->resumeIndex >= 1) return;
    faceEachOther(e, false);
    ev::runEvent(objId(e), e);
    rt::flushKeysWait();
}

void stepOnCheck() {
    Chara* h = hero();
    if (h->vehicleType == 5 || h->vehicleType == 6 || h->moveType == MT_ALIGHTING || h->substep >= 1) return;
    int hx = ((h->x - G.ox) % G.W + G.W) % G.W, hy = ((h->y - G.oy) % G.H + G.H) % G.H;
    Chara* e = gridGet(2, hx, hy);
    if (!e) e = gridGet(0, hx, hy);
    if (!e || e == h) return;
    if (e->page < 0 || e->attr.trigger(e->page) != 1 || e->resumeIndex >= 1) return;
    faceEachOther(e, true);
    ev::runEvent(objId(e), e);
    rt::flushKeysNoWait();
}

// ------------------------------------------------------------------ per-step effects (04 §13)
void poisonStep() {
    bool flashed = false;
    for (int i = 0; i < G.partySize; i++) {
        Battler& b = G.party[i];
        if (b.hp > 0 && (b.status & 0x01)) {
            if (!flashed) {
                disp::back.fill((uint8_t)disp::nearestRealised(255, 0, 0));
                disp::fullPresent();
                interimRedraw(true);
                flashed = true;
            }
            b.hp = s16(b.hp - 5);
            if (b.hp < 1) {
                b.hp = 0;
                b.status = 0;
                rt::message(battlerName(b) + data::strings[45], 5);
            }
        }
    }
}

void encounterStep() {
    Chara* h = hero();
    size_t cell = (size_t)h->y * G.W + h->x;
    G.encSteps = std::min(G.encSteps + 1, 10000);
    if (h->vehicleType >= 5 || h->moveType == MT_ALIGHTING) return;
    if (cell >= G.areaTable.size()) return;
    int a = G.areaTable[cell];
    if (a != G.encArea) { G.encSteps = 0; G.encArea = a; return; }
    rng::R();
    int order[10];
    for (int i = 0; i < 10; i++) order[i] = i;
    for (int i = 0; i < 10; i++) { int j = (int)(rng::R() % 10); std::swap(order[i], order[j]); }
    const MapRec& m = data::gd.maps[a];
    for (int i = 0; i < 10; i++) {
        int e = m.enc[order[i]];
        int p = e & 0x1FF, r = e >> 9;
        if (p == 0 || r == 0) continue;
        int d = std::max(r / 10, 1);
        int thr = r + (int)(rng::R() % (uint32_t)(2 * d + 1)) - d;
        if (h->vehicleType == 1 || h->vehicleType == 2) thr *= 2;
        if (thr <= G.encSteps && p <= 250 && (int16_t)data::atr[tileAt(h->x, h->y) & 1023] == data::troops[p - 1].terrain) {
            G.encSteps = 0;
            battle::start(p - 1, false);
            return;
        }
    }
}

// ------------------------------------------------------------------ vehicles (04 §12)
void boardVehicle(Chara* obj) {
    ui::destroyAll();
    G.menuRequest = false;
    int k = -1;
    for (int i = 0; i < 10 && G.vehicles[i].id != 0; i++)
        if (G.vehicles[i].id == obj->eventId) { k = i; break; }
    std::unique_ptr<Chara> owned;
    if (k >= 0) {
        owned = std::move(G.vehicles[k].obj);
    } else {
        k = 0;
        while (k < 10 && G.vehicles[k].id != 0) k++;
        for (size_t i = 0; i < G.events.size(); i++)
            if (G.events[i].get() == obj) { owned = std::move(G.events[i]); G.events.erase(G.events.begin() + i); break; }
        if (!owned) return;
        if (k == 10) {
            G.vehicles[9].obj.reset();
            G.vehicles[9].id = 0;
            k = 9;
        }
    }
    for (int i = k; i > 0; i--) {
        G.vehicles[i].id = G.vehicles[i - 1].id;
        G.vehicles[i].map = G.vehicles[i - 1].map;
        G.vehicles[i].obj = std::move(G.vehicles[i - 1].obj);
    }
    G.vehicles[0].id = 0;
    G.vehicles[0].map = G.curMap;
    G.vehicles[0].obj = std::move(owned);
    Chara* h = hero();
    h->moveType = MT_BOARDING;
    startMotion(h, MOTION_WAIT);
    h->display = DISP_INVISIBLE;
    G.moveControl = 1;
}

void finishBoarding() {
    Chara* h = hero();
    h->substep = 0;
    G.moveControl = 0;
    for (int i = 0; i < G.partySize; i++) { if (G.partyChar[i]) startMotion(G.partyChar[i], -1); G.trail[i] = -1; }
    Chara* v = G.vehicles[0].obj.get();
    if (!v) return;
    G.shown = 1;
    G.partyChar[0] = v;
    G.passHero = v;
    v->moveType = MT_PLAYER;
    G.vehicles[0].id = v->eventId;
    v->eventId = 0;
    v->layer = v->vehicleType > 4 ? 2 : 1;
    v->animType = 0x12;
    G.riding = true;
    interimRedraw(true);
    audio::playSlotBGM(v->vehicleType);
    if (G.heroWait.id != -1) ev::resumeHeroWait();  // resumed without an event object (05 §6.5)
}

// occupant query (04 §12.4): returns -2 none, -1 occupied, >= 0 tile id
static int occupant(int x, int y) {
    int c, r;
    screenCell(x, y, c, r, true);
    if (c < -1 || c > 20 || r < -1 || r > 15) return -2;
    if (gridGet(1, c, r)) return -1;
    Chara* b = gridGet(0, c, r);
    if (b) return b->graphic < 0x100 ? -1 : b->graphic - 0x100;
    return -2;
}

bool alight() {
    if (!G.riding) return false;
    Chara* V = hero();
    int x = V->x, y = V->y;
    int fdx = DX[V->facing & 3], fdy = DY[V->facing & 3];
    if (V->vehicleType == 5 || V->vehicleType == 6) {
        int q = occupant(x, y);
        if (q == -1) return false;
        int t = q >= 0 ? q : tileAt(x, y);
        int a = data::atr[t & 1023];
        if (!(passable(1, a) || passable(2, a))) return false;
    }
    int Fx = x + fdx, Fy = y + fdy;
    if (G.mapType != 1 && (Fx < 0 || Fx >= G.W || Fy < 0 || Fy >= G.H)) return false;
    Fx = (Fx + G.W) % G.W;
    Fy = (Fy + G.H) % G.H;
    int q = occupant(Fx, Fy);
    int t = q >= 0 ? q : tileAt(Fx, Fy);
    if (!passable(0, data::atr[t & 1023])) return false;
    if (q == -1) return false;
    G.shown = std::max(1, G.partySize);
    for (int i = 0; i < G.partySize; i++) {
        Chara* c = G.actorChar[G.partyActor[i]].get();
        G.partyChar[i] = c;
        c->x = c->destX = x;
        c->y = c->destY = y;
        c->facing = V->facing;
        G.trail[i] = -1;
    }
    G.passAbort = true;
    Chara* L = G.partyChar[0];
    L->moveType = MT_ALIGHTING;
    g_noTests = true;  // the leader is not the hero yet: no terrain/collision tests
    startMotion(L, V->facing & 3);
    g_noTests = false;
    L->display = DISP_VISIBLE;
    V->moveType = 0;
    G.moveControl = 1;
    G.riding = false;
    return true;
}

void finishAlighting() {
    Chara* L = hero();
    L->moveType = MT_PLAYER;
    L->substep = 0;
    G.moveControl = 0;
    G.passHero = L;
    Chara* V = G.vehicles[0].obj.get();
    if (V) {
        ev::applyPage(V, true);
        V->eventId = G.vehicles[0].id;
    }
    for (int i = 0; i < G.shown; i++) { startMotion(G.partyChar[i], -1); G.trail[i] = -1; }
    interimRedraw(true);
    audio::playSlotBGM(L->vehicleType);
    stepOnCheck();
    if (G.heroWait.id != -1) ev::resumeHeroWait();
}

bool escapeProcedure(int A) {
    if (A < 0 || A >= 500) return false;
    const MapRec& a = data::gd.maps[A];
    if (a.type != 2 || G.riding) return false;
    int P = a.parent;
    if (P < 0 || P >= 500 || data::gd.maps[P].w == 0) return false;
    const MapRec& p = data::gd.maps[P];
    if (p.type != 0 && p.type != 1) return false;
    if (a.exitX >= p.w || a.exitY >= p.h) return false;
    audio::playSE("wibble.wav", nullptr);
    if (!G.inBattle) {
        G.passAbort = true;
        ui::destroy(0);
        int saved = G.trans[0];
        G.trans[0] = 13;
        teleport(P, a.exitX, a.exitY);
        G.trans[0] = saved;
    } else G.escapeAfterBattle = A;
    return true;
}

bool charaOnRowVisited(const Chara*) { return true; }
void heroSubstepTick() {}

}  // namespace mapx
