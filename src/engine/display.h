// The 8-bit screen model of 03-display.md: palettes, palette effects, presentation,
// transitions and frame composition for the host window.
#pragma once
#include <functional>
#include <vector>
#include "core/gfx.h"

namespace disp {

extern Surface visible, back, cache;
extern Surface chara, chip[4];
extern Palette work, committed, realised;
extern bool haveRememberedMapPal;
extern Palette rememberedMapPal;

struct Layer {
    int x, y, w, h;
    const uint8_t* px;
};
// Window layers in creation order (bottom first). Set by the window system.
extern std::function<void(std::vector<Layer>&)> layersProvider;

struct MovieOverlay {
    bool active = false;
    Rect dst;
    int w = 0, h = 0;
    std::vector<uint32_t> rgb;
};
extern MovieOverlay movieOverlay;

void init();
// Something visible changed (screen, palette, windows): the host frame is refreshed.
void markDirty();
// Compose the current frame (visible + windows + movie) into 0x00RRGGBB.
void compose(uint32_t* out);
// Palette indices currently visible in rectangle r (screen + all windows), 0 outside.
void composeIndices(Rect r, std::vector<uint8_t>& out);

// ---- palettes (§2) ----
void loadPaletteFrom(const Bitmap& bmp, bool commit);
void realiseCommitted();
void setRealised(const Palette& p, bool commit);
void reloadMapPaletteWithTint(bool inBattle);
int nearestRealised(int r, int g, int b);

// ---- palette effect (§4) ----
// colour: 4 bytes (R%, G%, B%, grey) or nullptr = the stored tint.
void paletteEffect(int mode, const uint8_t* colour, bool store);
void paletteFade(const Palette& target, const Palette& from, int n, bool commit);
// Fade screen to black (§4.8); sets screen state ERASED.
void fadeScreenToBlack(int mode);

// ---- presentation (§5) ----
void fullPresent();
void partialPresent(Rect src, int dx, int dy);
void presentRect(Rect r);
void blockCopy(Rect r);
void blackFillVisible();

// ---- transitions (§6) ----
void show(int type);
void show(int type, Rect r);
void hide(int type, bool blacken);

// ---- map graphics (§8.1) ----
void loadMapGraphics(const sjis chipNames[4]);

}  // namespace disp
