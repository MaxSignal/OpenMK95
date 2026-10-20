#include "engine/display.h"
#include <algorithm>
#include <cstdio>
#include "core/fsys.h"
#include "platform/platform.h"
#include "core/rng.h"
#include "engine/state.h"

namespace disp {

Surface visible, back, cache;
Surface chara, chip[4];
Palette work, committed, realised;
bool haveRememberedMapPal = false;
Palette rememberedMapPal;
std::function<void(std::vector<Layer>&)> layersProvider;
MovieOverlay movieOverlay;

static void refreshCb(bool force);

static void setSystemColors(Palette& p) {
    for (int i = 0; i < 10; i++) p.c[i] = kSystemColors[i];
    for (int i = 0; i < 10; i++) p.c[246 + i] = kSystemColors[10 + i];
}

void init() {
    visible.create(640, 480);
    back.create(640, 480);
    cache.create(640, 480);
    for (int i = 0; i < 256; i++) work.c[i] = RGB{0, 0, 0};
    setSystemColors(work);
    committed = work;
    realised = work;
    plat::setRefreshCallback(refreshCb);
}

void markDirty() { plat::requestRefresh(); }

// ------------------------------------------------------------------ composition
void compose(uint32_t* out) {
    uint32_t lut[256];
    for (int i = 0; i < 256; i++) lut[i] = ((uint32_t)realised.c[i].r << 16) | ((uint32_t)realised.c[i].g << 8) | realised.c[i].b;
    const uint8_t* s = visible.px.data();
    for (int i = 0; i < 640 * 480; i++) out[i] = lut[s[i]];
    if (movieOverlay.active && movieOverlay.w > 0 && movieOverlay.h > 0) {
        Rect d = movieOverlay.dst;
        for (int y = std::max(0, d.t); y < std::min(480, d.b); y++) {
            int sy = (int)((int64_t)(y - d.t) * movieOverlay.h / std::max(1, d.h()));
            for (int x = std::max(0, d.l); x < std::min(640, d.r); x++) {
                int sx = (int)((int64_t)(x - d.l) * movieOverlay.w / std::max(1, d.w()));
                out[y * 640 + x] = movieOverlay.rgb[(size_t)sy * movieOverlay.w + sx];
            }
        }
    }
    if (layersProvider) {
        std::vector<Layer> layers;
        layersProvider(layers);
        for (auto& L : layers) {
            for (int y = 0; y < L.h; y++) {
                int sy = L.y + y;
                if (sy < 0 || sy >= 480) continue;
                for (int x = 0; x < L.w; x++) {
                    int sx = L.x + x;
                    if (sx < 0 || sx >= 640) continue;
                    out[sy * 640 + sx] = lut[L.px[(size_t)y * L.w + x]];
                }
            }
        }
    }
}

void composeIndices(Rect r, std::vector<uint8_t>& out) {
    int w = r.w(), h = r.h();
    out.assign((size_t)std::max(0, w) * std::max(0, h), 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int sx = r.l + x, sy = r.t + y;
            if (sx >= 0 && sx < 640 && sy >= 0 && sy < 480) out[(size_t)y * w + x] = visible.px[(size_t)sy * 640 + sx];
        }
    if (layersProvider) {
        std::vector<Layer> layers;
        layersProvider(layers);
        for (auto& L : layers)
            for (int y = 0; y < L.h; y++)
                for (int x = 0; x < L.w; x++) {
                    int ox = L.x + x - r.l, oy = L.y + y - r.t;
                    int sx = L.x + x, sy = L.y + y;
                    if (ox < 0 || oy < 0 || ox >= w || oy >= h) continue;
                    if (sx < 0 || sy < 0 || sx >= 640 || sy >= 480) continue;
                    out[(size_t)oy * w + ox] = L.px[(size_t)y * L.w + x];
                }
    }
}

static std::vector<uint32_t> g_frame(640 * 480);
static void refreshCb(bool) {
    compose(g_frame.data());
    plat::showFrame(g_frame.data());
}

// ------------------------------------------------------------------ palettes
void loadPaletteFrom(const Bitmap& bmp, bool commit) {
    int n = bmp.paletteCount();
    for (int i = 0; i < n && i <= 255; i++) {
        if (i <= 9 || i >= 246) work.c[i] = (i <= 9) ? kSystemColors[i] : kSystemColors[10 + i - 246];
        else work.c[i] = bmp.table[i];
    }
    realised = work;
    if (commit) committed = work;
    markDirty();
}

void realiseCommitted() {
    realised = committed;
    markDirty();
}

void setRealised(const Palette& p, bool commit) {
    realised = p;
    if (commit && &p != &committed) committed = p;
    markDirty();
}

int nearestRealised(int r, int g, int b) { return nearestIndex(realised, r, g, b); }

void reloadMapPaletteWithTint(bool inBattle) {
    std::vector<uint8_t> f;
    Bitmap bmp;
    if (fsys::read(inBattle ? "BG01.BMP" : "CHARA.BMP", f) && decodeBmp(f, bmp)) loadPaletteFrom(bmp, true);
    paletteEffect(4, nullptr, false);
}

// target palette (§4.2); returns the 'changed' flag
static bool computeTarget(const uint8_t* col, Palette& T) {
    bool changed = false;
    int pr = col[0], pg = col[1], pb = col[2], grey = col[3];
    for (int i = 0; i < 256; i++) {
        int c[3];
        if (i <= 9 || i >= 246) {
            c[0] = work.c[i].r; c[1] = work.c[i].g; c[2] = work.c[i].b;
        } else {
            int r = work.c[i].r, g = work.c[i].g, b = work.c[i].b;
            if (grey == 0) {
                int src[3] = {r, g, b}, p[3] = {pr, pg, pb};
                for (int k = 0; k < 3; k++) {
                    if (p[k] < 100) c[k] = p[k] * src[k] / 100;
                    else c[k] = src[k] + (255 - src[k]) * (p[k] - 100) / 100;
                }
            } else {
                int S = 12 * r + 15 * g + 10 * b;
                int L = S / 30;
                if (S > 0 && S % 30 == 0) L -= 1;
                int p[3] = {pr, pg, pb};
                for (int k = 0; k < 3; k++) {
                    int q = p[k] * 255 / 200;
                    int v = L * q / 255;
                    c[k] = std::min(v, 255);
                }
            }
        }
        if (c[0] != committed.c[i].r || c[1] != committed.c[i].g || c[2] != committed.c[i].b) changed = true;
        T.c[i] = RGB{(uint8_t)c[0], (uint8_t)c[1], (uint8_t)c[2]};
    }
    return changed;
}

void paletteFade(const Palette& target, const Palette& from, int n, bool commit) {
    n *= 2;  // full-screen behaviour (§1.3)
    if (n > 0) {
        Palette cur = from;
        int d[256][3];
        for (int i = 10; i <= 245; i++) {
            d[i][0] = ((int)target.c[i].r - (int)from.c[i].r) / n;
            d[i][1] = ((int)target.c[i].g - (int)from.c[i].g) / n;
            d[i][2] = ((int)target.c[i].b - (int)from.c[i].b) / n;
        }
        for (int k = 0; k < n - 1; k++) {
            for (int i = 10; i <= 245; i++) {
                cur.c[i].r = (uint8_t)(cur.c[i].r + d[i][0]);
                cur.c[i].g = (uint8_t)(cur.c[i].g + d[i][1]);
                cur.c[i].b = (uint8_t)(cur.c[i].b + d[i][2]);
            }
            realised = cur;
            markDirty();
            plat::delay(30);
        }
    }
    realised = target;
    if (commit) committed = target;
    markDirty();
    plat::pumpOS();
}

static void shake(int A) {
    for (int k = 0; k < 4; k++) {
        int dx = (k == 0 || k == 2) ? A : (k == 1 ? -A : 0);
        int dy = 0;
        if (k < 3) dy = (int)(rng::R() % (uint32_t)(2 * A)) - A;
        Rect src = Rect::xywh(std::max(-dx, 0), std::max(-dy, 0), 640 - std::abs(dx), 480 - std::abs(dy));
        plat::waitVBlank();
        blitOpaque(back, src, visible, std::max(dx, 0), std::max(dy, 0));
        markDirty();
        plat::delay(70);
    }
}

void paletteEffect(int mode, const uint8_t* colour, bool store) {
    const uint8_t* col = colour ? colour : G.tint;
    Palette T;
    bool changed = computeTarget(col, T);
    switch (mode) {
        case -1: committed = T; break;
        case 0:
        case 1:
            if (colour && changed) {
                for (int k = 0; k < (mode == 1 ? 2 : 1); k++) {
                    realised = T; markDirty(); plat::delay(20);
                    realiseCommitted(); plat::delay(20);
                }
            }
            break;
        case 2: shake(8); break;
        case 3: shake(16); break;
        case 4: if (changed) paletteFade(T, committed, 0, true); break;
        case 5:
        case 7: if (changed) paletteFade(T, committed, 5, true); break;
        case 6:
            if (changed) { Palette c = committed; paletteFade(T, c, 3, false); realiseCommitted(); }
            break;
        default: break;
    }
    if ((mode == 4 || mode == 5 || mode == 7) && store && colour) memcpy(G.tint, colour, 4);
}

void fadeScreenToBlack(int mode) {
    static const uint8_t black[4] = {0, 0, 0, 0};
    paletteEffect(mode, black, false);
    blackFillVisible();
    G.screenState = SS_ERASED;
}

// ------------------------------------------------------------------ presentation
void fullPresent() {
    plat::waitVBlank();
    visible.px = back.px;
    markDirty();
    plat::pumpOS();
}

void partialPresent(Rect r, int dx, int dy) {
    if (r.r <= 0 || r.b <= 0 || r.l >= 640 || r.t >= 480) return;
    Rect c(std::max(r.l, 0), std::max(r.t, 0), std::min(r.r, 640), std::min(r.b, 480));
    dx += c.l - r.l;
    dy += c.t - r.t;
    plat::waitVBlank();
    blitOpaque(back, c, visible, dx, dy);
    markDirty();
    plat::pumpOS();
}

void presentRect(Rect r) { partialPresent(r, r.l, r.t); }

void blockCopy(Rect r) {
    // §5.1 / §1.2: empty rectangles and rectangles not completely inside the screen copy nothing
    // (the transitions clip their rectangles with min() beforehand).
    if (r.r <= r.l || r.b <= r.t || !rectInside(r, 640, 480)) return;
    for (int y = r.t; y < r.b; y++) memcpy(visible.row(y) + r.l, back.row(y) + r.l, (size_t)(r.r - r.l));
    markDirty();
}

void blackFillVisible() {
    visible.fill(0);
    markDirty();
}

// ------------------------------------------------------------------ transitions
static const Rect kFull(0, 0, 640, 480);

void show(int type) { show(type, kFull); }

static void pacedFrame(int i, int n, int& lastFrameIdx) {
    // spread the work over ~400 ms, presenting at the display rate
    const int frames = 24;
    int f = (int)((int64_t)(i + 1) * frames / std::max(1, n));
    if (f != lastFrameIdx) { lastFrameIdx = f; plat::waitVBlank(); markDirty(); plat::pumpOS(); }
}

static void showDissolve(int type, Rect R) {
    int w = R.w(), h = R.h();
    int b = (w == 640) ? 8 : 2;
    int cols = w / b, rows = h / b, N = cols * rows;
    if (N <= 0) return;
    std::vector<int> a(N);
    for (int i = 0; i < N; i++) a[i] = (type == 3) ? N - 1 - i : i;
    for (int i = 0; i < N; i++) {
        int j;
        if (type == 1) j = (int)(rng::R() % (uint32_t)N);
        else {
            int m = cols * 8;
            j = i + (m ? (int)(rng::R() % (uint32_t)m) : 0);
            if (j >= N) j = N - 1;
        }
        std::swap(a[i], a[j]);
    }
    int lf = -1;
    for (int i = 0; i < N; i++) {
        int x = R.l + (a[i] % cols) * b, y = R.t + (a[i] / cols) * b;
        blockCopy(Rect::xywh(x, y, b, b));
        pacedFrame(i, N, lf);
    }
}

void show(int type, Rect R) {
    if (type > 14) return;
    if (type < 0) type = 2;
    switch (type) {
        case 0: presentRect(R); break;
        case 1: case 2: case 3: showDissolve(type, R); break;
        case 4: {
            int perm[64];
            for (int i = 0; i < 64; i++) perm[i] = i;
            for (int i = 0; i < 64; i++) { int j = (int)(rng::R() % 64); std::swap(perm[i], perm[j]); }
            for (int k = 0; k < 64; k++) {
                for (int sy = 0; sy < 8; sy++)
                    for (int sx = 0; sx < 11; sx++) {
                        int cx = 8 * sx + perm[k] % 8, cy = 8 * sy + perm[k] / 8;
                        if (cx < 80 && cy < 60) blockCopy(Rect(8 * cx, 8 * cy, 8 * cx + 8, 8 * cy + 8));
                    }
                plat::delay(5);
            }
            break;
        }
        case 5:
            for (int k = 0; k < 16; k++) {
                for (int j = 0; j <= 30; j++) { int y = 16 * j + k; if (y + 1 > 480) break; blockCopy(Rect(0, y, 640, y + 1)); }
                plat::delay(60);
            }
            break;
        case 6:
            for (int k = 0; k < 16; k++) {
                for (int j = 0; j <= 40; j++) { int x = 16 * j + k; if (x + 1 > 640) break; blockCopy(Rect(x, 0, x + 1, 480)); }
                plat::delay(60);
            }
            break;
        case 7:
            for (int i = 0; i <= 58; i += 2) {
                blockCopy(Rect(0, 8 * i, 640, std::min(8 * i + 8, 480)));
                blockCopy(Rect(0, 472 - 8 * i, 640, std::min(480 - 8 * i, 480)));
                plat::delay(30);
            }
            break;
        case 8:
        case 9:
            for (int i = 0; i <= 78; i += 2) { blockCopy(Rect(8 * i, 0, std::min(8 * i + 8, 640), 480)); plat::delay(10); }
            if (type == 8)
                for (int i = 1; i <= 79; i += 2) { blockCopy(Rect(8 * i, 0, std::min(8 * i + 8, 640), 480)); plat::delay(10); }
            else
                for (int i = 79; i >= 1; i -= 2) { blockCopy(Rect(8 * i, 0, std::min(8 * i + 8, 640), 480)); plat::delay(10); }
            break;
        case 10: {
            auto cell = [](int x, int y) { blockCopy(Rect(64 * x, 48 * y, std::min(64 * x + 64, 640), std::min(48 * y + 48, 480))); };
            cell(4, 4); plat::delay(3);
            int x = 4, y = 4, d = 1;
            for (int n = 2; n <= 21; n++) {
                for (int k = 0; k < n / 2; k++) {
                    x += DX[d]; y += DY[d];
                    if (x >= 0 && y >= 0) { cell(x, y); plat::delay(3); }
                }
                d = (d + 1) % 4;
            }
            break;
        }
        case 11: {
            auto pos = [](int x, int y) {
                for (int by = 0; by < 8; by++)
                    for (int bx = 0; bx < 11; bx++) {
                        int px = 64 * bx + 8 * x, py = 64 * by + 8 * y;
                        if (px < 640 && py < 480) blockCopy(Rect(px, py, std::min(px + 8, 640), std::min(py + 8, 480)));
                    }
            };
            int lf = -1, cnt = 0;
            pos(3, 3);
            int x = 3, y = 3, d = 0;
            for (int n = 2; n <= 17; n++) {
                d = (d + 1) % 4;
                for (int k = 0; k < n / 2; k++) {
                    x += DX[d]; y += DY[d];
                    if (x >= 0 && y >= 0) { pos(x, y); pacedFrame(cnt++, 64, lf); }
                }
            }
            break;
        }
        case 12:
            for (int i = 1; i <= 16; i++) {
                plat::waitVBlank();
                blockCopy(Rect(320 - 20 * i, 240 - 15 * i, 320 + 20 * i, 240 + 15 * i));
                plat::delay(40);
            }
            break;
        case 13: {
            int w = R.w(), h = R.h(), hw = w / 2, hh = h / 2;
            if (hw <= 0 || hh <= 0) { presentRect(R); break; }
            Surface S1(w, h), S2(hw, hh);
            Rect Q(R.l + 1, R.t + 1, R.r - 1, R.b - 1);
            int x, y;
            if (hh < hw) { y = 8; x = hw * 8 / hh; } else { x = 8; y = hh * 8 / hw; }
            do {
                blitStretch(back, Q, S2, Rect(0, 0, x, y), -1);
                blitStretch(S2, Rect(0, 0, x, y), S1, Rect(0, 0, w, h), -1);
                plat::waitVBlank();
                blitOpaque(S1, Rect(0, 0, w, h), visible, R.l, R.t);
                markDirty();
                x = x * 3 / 2; y = y * 3 / 2;
                plat::delay(2);
            } while (x <= hw && y <= hh);
            if (x < w || y < h) fullPresent();
            break;
        }
        case 14: {
            Palette P = committed;
            static const uint8_t black[4] = {0, 0, 0, 0};
            paletteEffect(5, black, false);
            fullPresent();
            Palette c = committed;
            paletteFade(P, c, 8, true);
            committed = P;
            break;
        }
    }
    markDirty();
    plat::pumpOS();
}

void hide(int type, bool blacken) {
    if (type == 12) {
        Surface S(640, 480);
        if (!blacken) S = back;
        for (int i = 0; i < 16; i++) {
            Rect rs[4] = {Rect(20 * i, 15 * i, 640 - 20 * i, 15 * i + 15), Rect(20 * i, 465 - 15 * i, 640 - 20 * i, 480 - 15 * i),
                          Rect(20 * i, 15 * i, 20 * i + 20, 480 - 15 * i), Rect(620 - 20 * i, 15 * i, 640 - 20 * i, 480 - 15 * i)};
            for (auto& r : rs) blitOpaque(S, r, back, r.l, r.t);
            fullPresent();
            plat::delay(40);
        }
        return;
    }
    if (type == 13) {
        Surface S1(640, 480), S2(320, 240);
        Rect Q(1, 1, 639, 479);
        int x = 320, y = 240;
        for (;;) {
            blitStretch(back, Q, S2, Rect(0, 0, x, y), -1);
            blitStretch(S2, Rect(0, 0, x, y), S1, Rect(0, 0, 640, 480), -1);
            plat::waitVBlank();
            visible.px = S1.px;
            markDirty();
            plat::pumpOS();
            x = 2 * x / 3; y = 2 * y / 3;
            if (x < 4 || y <= 3) break;
        }
        if (blacken) { back.fill(0); show(0); }
        return;
    }
    if (type == 14) {
        Palette black;
        for (int i = 0; i < 256; i++) black.c[i] = RGB{0, 0, 0};
        Palette c = committed;
        paletteFade(black, c, 8, false);
        visible.fill(0);
        back.fill(0);
        fullPresent();
        realiseCommitted();
        return;
    }
    if (blacken) back.fill(0);
    show(type, kFull);
}

// ------------------------------------------------------------------ map graphics
static void loadSheet(const sjis& file, Surface& out, int defW, int defH) {
    std::vector<uint8_t> f;
    Bitmap bmp;
    if (fsys::read(file, f) && decodeBmp(f, bmp)) {
        convertBitmap(bmp, realised, out);
    } else {
        fprintf(stderr, "warning: cannot load %s\n", file.c_str());
        out.create(defW, defH, 0);
    }
}

void loadMapGraphics(const sjis chipNames[4]) {
    std::vector<uint8_t> f;
    Bitmap bmp;
    bool ok = fsys::read("CHARA.BMP", f) && decodeBmp(f, bmp);
    if (ok) {
        loadPaletteFrom(bmp, false);
        convertBitmap(bmp, realised, chara);
    } else {
        fprintf(stderr, "warning: cannot load CHARA.BMP\n");
        chara.create(1024, 512, 0);
    }
    chara.key = chara.empty() ? -1 : chara.px[0];
    for (int k = 0; k < 4; k++) {
        // <game folder>\<name>.BMP, no base-name reduction (02-data-formats.md §8.1); when it cannot be
        // found, fall back to MCHIPn.BMP (04-map-movement.md §2.2), else a blank sheet (§8.1 UNCERTAIN).
        sjis n = chipNames[k];
        if (!fsys::exists(n + ".BMP")) {
            char b[16];
            snprintf(b, sizeof b, "MCHIP%d", k);
            if (fsys::exists(sjis(b) + ".BMP")) n = b;
        }
        loadSheet(n + ".BMP", chip[k], 256, 1024);
        chip[k].key = chara.key;
    }
    paletteEffect(4, nullptr, false);
}

}  // namespace disp
