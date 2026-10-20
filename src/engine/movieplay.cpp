// Event command 14 Play movie (09-audio-movies.md §7).
#include <algorithm>
#include <cstdio>
#include "engine/display.h"
#include "core/fsys.h"
#include "engine/game.h"
#include "movie/movie.h"
#include "platform/platform.h"
#include "engine/runtime.h"
#include "core/sjis.h"
#include "sound/snd.h"

namespace mov {

static bool g_showing = false;
bool movieShowing() { return g_showing; }

static Rect movieRect(int w, int h, int flags) {
    if (flags & 1) {
        int x, y, w2, h2;
        if (h < w) { h2 = h * 640 / w; w2 = 640; x = 0; y = (480 - h2) / 2; }
        else { w2 = w * 480 / h; h2 = 480; y = 0; x = (640 - w2) / 2; }
        w2 = std::min(w2, 639);
        h2 = std::min(h2, 479);
        return Rect::xywh(x, y, w2, h2);
    }
    int p = (flags >> 4) & 15;
    int ph = p % 3, pv = p / 3;
    if (pv > 2) pv = 2;
    int x = ph == 0 ? 24 : ph == 1 ? (640 - w) / 2 : std::max(0, 616 - w);
    int y = pv == 0 ? 24 : pv == 1 ? (480 - h) / 2 : std::max(0, 456 - h);
    return Rect::xywh(x, y, w, h);
}

void playMovie(const sjis& name, int flags) {
    std::vector<uint8_t> file;
    if (!fsys::read(sj::baseName(name), file)) return;
    ui::destroy(0);
    audio::saveBGM();
    audio::playBGM(sj::S("演奏停止"), nullptr, false);
    snd::close();
    bool black = (flags & 4) != 0;
    int prevState = G.screenState;
    static const uint8_t blk[4] = {0, 0, 0, 0};
    if (black) {
        if (G.screenState == SS_MAP) disp::paletteEffect(5, blk, false);
        disp::blackFillVisible();
        G.screenState = SS_MOVIE;
    }
    rt::flushKeysNoWait();
    rt::processMessages();
    G.confirm = false;
    std::string err;
    std::unique_ptr<movie::Movie> m = movie::openAvi(std::move(file), &err);
    if (!m) {
        fprintf(stderr, "movie: %s\n", err.c_str());
    } else {
        auto& ov = disp::movieOverlay;
        ov.w = m->width();
        ov.h = m->height();
        ov.dst = movieRect(ov.w, ov.h, flags);
        ov.rgb.assign((size_t)ov.w * ov.h, 0);
        g_showing = true;
        bool have = m->nextFrame(ov.rgb);
        ov.active = have;
        disp::markDirty();
        uint32_t t0 = plat::now();
        int frame = 0;
        int64_t usPer = std::max<int64_t>(1000, m->usPerFrame());
        // §7.2 step 7: the movie ends on confirm (Enter/Space/click), on Escape, or at its end.
        while (have && !G.quit) {
            if (G.confirm) { G.confirm = false; break; }
            rt::processMessages();
            if (G.confirm) { G.confirm = false; break; }
            if (plat::keyHeld(VK_ESCAPE_)) break;
            int64_t due = (int64_t)(plat::now() - t0) * 1000 / usPer;
            while (frame < due && have) {
                have = m->nextFrame(ov.rgb);
                frame++;
                disp::markDirty();
            }
            plat::idle();
        }
        ov.active = false;
        g_showing = false;
        disp::markDirty();
    }
    if (!black) {
        disp::realiseCommitted();
        rt::processMessages();
    } else {
        disp::blackFillVisible();
        disp::paletteEffect(4, blk, false);
        disp::fullPresent();
        disp::paletteEffect(5, nullptr, false);
        G.screenState = prevState;
    }
    snd::open();
    audio::resumeBGM(false);
    disp::markDirty();
}

}  // namespace mov
