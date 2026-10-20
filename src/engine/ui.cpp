#include "engine/ui.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include "engine/display.h"
#include "platform/platform.h"
#include "core/sjis.h"
#include "engine/state.h"

namespace ui {

Window* slots[13];
int focused = -1;
int selectedIndex = -1;
bool haveSelMouse = false;
int selMouseX = 0, selMouseY = 0;
bool transparentMode = false;
std::deque<Notif> notifs;
static std::vector<int> g_z;  // creation order

void post(const Notif& n) { notifs.push_back(n); }

static void layers(std::vector<disp::Layer>& out) {
    for (int s : g_z) {
        Window* w = slots[s];
        if (!w) continue;
        out.push_back({w->x, w->y, w->w, w->h, w->px.data()});
    }
}

void init() {
    for (auto& s : slots) s = nullptr;
    disp::layersProvider = layers;
}

bool exists(int slot) { return slot >= 0 && slot < 13 && slots[slot]; }
Window* get(int slot) { return exists(slot) ? slots[slot] : nullptr; }

// ------------------------------------------------------------------ text layout
struct TChar { int code; bool ul; };

void drawLabelText(uint8_t* buf, int bw, int bh, const Rect& clip, const Rect& r, const sjis& text, int fmt, uint8_t color) {
    // prefix processing
    std::vector<TChar> chars;
    for (size_t i = 0; i < text.size();) {
        int c = sj::next(text, i);
        if (c == '&') {
            if (i >= text.size()) break;
            size_t j = i;
            int n = sj::next(text, j);
            if (n == '&') { chars.push_back({'&', false}); i = j; continue; }
            chars.push_back({n, true});
            i = j;
            continue;
        }
        chars.push_back({c, false});
    }
    bool single = fmt == SINGLE_VC || fmt == SINGLE_VC_RIGHT;
    bool wrap = fmt == LEFT_WRAP || fmt == CENTRE_WRAP || fmt == RIGHT_WRAP;
    std::vector<std::vector<TChar>> lines(1);
    for (size_t i = 0; i < chars.size(); i++) {
        int c = chars[i].code;
        if (c == '\r' || c == '\n') {
            if (single) continue;
            if (c == '\r' && i + 1 < chars.size() && chars[i + 1].code == '\n') i++;
            lines.emplace_back();
            continue;
        }
        lines.back().push_back(chars[i]);
    }
    int width = r.w();
    if (wrap) {
        std::vector<std::vector<TChar>> out;
        for (auto& line : lines) {
            std::vector<TChar> cur;
            int curW = 0;
            bool afterWrap = false;
            for (auto& ch : line) {
                int adv = font::advance(ch.code);
                if (curW + adv > width && !cur.empty()) {
                    // a single-byte non-space character (part of a word of single-byte text)
                    bool asciiWord = ch.code < 0x100 && ch.code != ' ';
                    int lastSpace = -1;
                    if (asciiWord)
                        for (int k = (int)cur.size() - 1; k > 0; k--)
                            if (cur[k].code == ' ') { lastSpace = k; break; }
                    if (lastSpace > 0) {
                        out.emplace_back(cur.begin(), cur.begin() + lastSpace);
                        std::vector<TChar> rest(cur.begin() + lastSpace + 1, cur.end());
                        cur = rest;
                        curW = 0;
                        for (auto& t : cur) curW += font::advance(t.code);
                        afterWrap = false;
                    } else {
                        out.push_back(cur);
                        cur.clear();
                        curW = 0;
                        afterWrap = true;
                    }
                }
                if (afterWrap && ch.code == ' ') continue;
                afterWrap = false;
                cur.push_back(ch);
                curW += adv;
            }
            out.push_back(cur);
        }
        lines.swap(out);
    }
    int h = r.h();
    for (size_t li = 0; li < lines.size(); li++) {
        int top = single ? r.t + (h - font::TextH) / 2 : r.t + (int)li * font::TextH;
        if (top >= clip.b) break;
        int lw = 0;
        for (auto& c : lines[li]) lw += font::advance(c.code);
        int x;
        if (fmt == CENTRE_WRAP) x = r.l + (width - lw) / 2;
        else if (fmt == RIGHT_WRAP || fmt == RIGHT_PLAIN || fmt == SINGLE_VC_RIGHT) x = r.r - lw;
        else x = r.l;
        for (auto& c : lines[li]) {
            int adv = font::advance(c.code);
            font::drawChar(buf, bw, bh, clip, x, top, c.code, color);
            if (c.ul) {
                int y = top + font::Ascent + 1;
                for (int k = 0; k < adv; k++) {
                    int px = x + k;
                    if (px >= clip.l && px < clip.r && y >= clip.t && y < clip.b && px >= 0 && y >= 0 && px < bw && y < bh)
                        buf[(size_t)y * bw + px] = color;
                }
            }
            x += adv;
        }
        if (single) break;
    }
}

// ------------------------------------------------------------------ painting
static void drawFrame(Window* w) {
    auto P = [&](int x, int y, uint8_t c) { if (x >= 0 && y >= 0 && x < w->w && y < w->h) w->px[(size_t)y * w->w + x] = c; };
    int W_ = w->w, H_ = w->h;
    const uint8_t tl[3] = {C_LTGRAY, C_WHITE, C_LTGRAY}, br[3] = {C_BLACK, C_GRAY, C_LTGRAY};
    for (int k = 0; k < 3; k++) {
        for (int x = k; x < W_ - k; x++) P(x, k, tl[k]);
        for (int y = k; y < H_ - k; y++) P(k, y, tl[k]);
        for (int x = k; x < W_ - k; x++) P(x, H_ - 1 - k, br[k]);
        for (int y = k; y < H_ - k; y++) P(W_ - 1 - k, y, br[k]);
    }
}

static Rect clientRectOuter(Window* w) { return Rect(3, 3, w->w - 3, w->h - 3); }

static void drawLabel(Window* w, int i, int colorOverride = -1) {
    Label& L = w->labels[i];
    if (!L.present) return;
    Rect cl = clientRectOuter(w);
    Rect lr(L.r.l + 3, L.r.t + 3, L.r.r + 3, L.r.b + 3);
    Rect clip(std::max(cl.l, lr.l), std::max(cl.t, lr.t), std::min(cl.r, lr.r), std::min(cl.b, lr.b));
    if (clip.r <= clip.l || clip.b <= clip.t) return;
    int color = L.disabled ? C_GRAY : (colorOverride >= 0 ? colorOverride : L.color);
    drawLabelText(w->px.data(), w->w, w->h, clip, lr, L.text, L.fmt, (uint8_t)color);
}

static void drawButton(Window* w, int cx, int cy, const char* cap, bool enabled) {
    int x0 = cx + 3, y0 = cy + 3, bw = 40, bh = 20;
    auto P = [&](int x, int y, uint8_t c) {
        if (x >= 3 && y >= 3 && x < w->w - 3 && y < w->h - 3) w->px[(size_t)y * w->w + x] = c;
    };
    // face
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) P(x0 + x, y0 + y, C_LTGRAY);
    // outer ring: top/left white, then bottom/right black including the corners
    for (int x = 0; x < bw; x++) P(x0 + x, y0, C_WHITE);
    for (int y = 0; y < bh; y++) P(x0, y0 + y, C_WHITE);
    for (int x = 0; x < bw; x++) P(x0 + x, y0 + bh - 1, C_BLACK);
    for (int y = 0; y < bh; y++) P(x0 + bw - 1, y0 + y, C_BLACK);
    // inner ring: top/left stay face colour, bottom/right gray
    for (int x = 1; x < bw - 1; x++) P(x0 + x, y0 + bh - 2, C_GRAY);
    for (int y = 1; y < bh - 1; y++) P(x0 + bw - 2, y0 + y, C_GRAY);
    Rect clip(std::max(3, x0 + 2), std::max(3, y0 + 2), std::min(w->w - 3, x0 + bw - 2), std::min(w->h - 3, y0 + bh - 2));
    // caption in the regular (non-bold) weight of the window font (§4.5 allows this), centred
    sjis s = cap;
    int tw = 0;
    for (size_t i = 0; i < s.size(); i++) tw += font::advanceRegular((uint8_t)s[i]);
    int tx = x0 + (bw - tw) / 2, ty = y0 + (bh - font::TextH) / 2;
    auto drawRegular = [&](int ox, int oy, uint8_t c) {
        int xx = ox;
        for (size_t i = 0; i < s.size(); i++) {
            font::drawChar(w->px.data(), w->w, w->h, clip, xx, oy, (uint8_t)s[i], c, false);
            xx += font::advanceRegular((uint8_t)s[i]);
        }
    };
    if (enabled) drawRegular(tx, ty, C_BLACK);
    else { drawRegular(tx + 1, ty + 1, C_WHITE); drawRegular(tx, ty, C_GRAY); }
}

static Rect cursorCell(Window* w) {
    int colW = w->cw();
    Rect c(w->curCol * colW, w->curRow * LineH, w->curCol * colW + colW, w->curRow * LineH + LineH);
    c.l = std::max(c.l, 0); c.t = std::max(c.t, 0);
    c.r = std::min(c.r, w->cw()); c.b = std::min(c.b, w->ch());
    return Rect(c.l + 3, c.t + 3, c.r + 3, c.b + 3);
}

static void paintCursor(Window* w) {
    if (!w->hasCursor()) return;
    Rect c = cursorCell(w);
    if (c.r <= c.l || c.b <= c.t) return;
    if (!w->transparent) {
        for (int y = c.t; y < c.b; y++)
            for (int x = c.l; x < c.r; x++) {
                uint8_t& p = w->px[(size_t)y * w->w + x];
                p = (uint8_t)(255 - p);
            }
        return;
    }
    for (int y = c.t; y < c.b; y++)
        for (int x = c.l; x < c.r; x++) w->px[(size_t)y * w->w + x] = C_BLACK;
    auto temp = [&](int li) {
        if (li < 0 || li >= 60 || !w->labels[li].present) return;
        int col = w->labels[li].color == w->normalColor ? C_GRAY : C_BLUE;
        drawLabel(w, li, col);
    };
    int r = w->curRow;
    temp(1 * w->curRow + w->curCol + 1);
    if (w->slot == 4 || w->slot == 5 || w->slot == 8) temp(11 + r);
    if (w->slot == 6 || w->slot == 11)
        for (int j = 1; j <= 4; j++) temp(w->rows + 4 * r + j);
}

void repaint(Window* w) {
    if (!w) return;
    w->px.assign((size_t)w->w * w->h, C_LTGRAY);
    drawFrame(w);
    if (w->transparent && !w->snapshot.empty()) {
        for (int y = 0; y < w->ch(); y++)
            for (int x = 0; x < w->cw(); x++) w->px[(size_t)(y + 3) * w->w + x + 3] = w->snapshot[(size_t)y * w->cw() + x];
    }
    uint8_t sep = w->transparent ? C_WHITE : C_BLACK;
    auto sepLine = [&](int x0, int x1, int yy) {
        for (int x = x0; x < x1; x++) {
            int px = x + 3, py = yy + 3;
            if (px >= 3 && px < w->w - 3 && py >= 3 && py < w->h - 3) w->px[(size_t)py * w->w + px] = sep;
        }
    };
    if (w->slot == 1) sepLine(6, 589, 18);
    if (w->slot == 9) sepLine(16, 430, 198);
    for (int i = 0; i < 60; i++) drawLabel(w, i);
    if (w->hasButtons) {
        drawButton(w, 145, 182, "<<", w->btnPrev);
        drawButton(w, 190, 182, ">>", w->btnNext);
    }
    paintCursor(w);
    disp::markDirty();
}

void repaint(int slot) { repaint(get(slot)); }

// ------------------------------------------------------------------ creation
static void darkenSnapshot(Window* w) {
    disp::composeIndices(Rect::xywh(w->x + 3, w->y + 3, w->cw(), w->ch()), w->snapshot);
    uint8_t map[256];
    for (int i = 0; i < 256; i++) {
        int c[3] = {disp::realised.c[i].r, disp::realised.c[i].g, disp::realised.c[i].b};
        int o[3];
        for (int k = 0; k < 3; k++) {
            int v = c[k];
            if (w->slot == 0) { o[k] = 7 * v / 10; if (v > 0 && v % 10 == 0) o[k]--; }
            else if (w->slot == 1) {
                if (k < 2) { o[k] = 3 * v / 5; if (v > 0 && v % 5 == 0) o[k]--; }
                else o[k] = 4 * v / 5;
            } else o[k] = v / 2;
        }
        map[i] = (uint8_t)disp::nearestRealised(o[0], o[1], o[2]);
    }
    for (auto& p : w->snapshot) p = map[p];
}

static void addLabel(Window* w, int i, Rect r, int fmt) {
    Label& L = w->labels[i];
    L.present = true;
    L.r = r;
    L.fmt = fmt;
    L.color = w->normalColor;
    L.text.clear();
    L.disabled = false;
}

static void layoutLabels(Window* w, int fmt) {
    int s = w->slot;
    if (w->selectable) {
        int colW = w->cw();
        int pad = fmt == CENTRE_WRAP ? 0 : 10;
        addLabel(w, 0, Rect::xywh(0, 0, colW, LineH), fmt);
        if (s == 4 || s == 5 || s == 7 || s == 8) {
            for (int r = 0; r < w->rows; r++) {
                addLabel(w, 1 + r, Rect::xywh(pad, LineH * r + 1, colW / 2 + 20, LineH), fmt);
                addLabel(w, 11 + r, Rect::xywh(pad + colW / 2, LineH * r + 1, colW / 2 - 20, LineH), fmt);
            }
            addLabel(w, 45, Rect::xywh(24, 182, 20, 20), RIGHT_WRAP);
            addLabel(w, 46, Rect::xywh(40, 182, 20, 20), RIGHT_WRAP);
            addLabel(w, 47, Rect::xywh(60, 182, 40, 20), CENTRE_WRAP);
            w->hasButtons = true;
            w->btnPrev = false;
            w->btnNext = true;
        } else {
            for (int r = 0; r < w->rows; r++) addLabel(w, 1 + r, Rect::xywh(pad, LineH * r + 1, colW, LineH), fmt);
            if (s == 6 || s == 11) {
                static const int xs[4] = {130, 160, 197, 205}, ws[4] = {30, 37, 8, 37};
                for (int r = 0; r < w->rows; r++)
                    for (int j = 0; j < 4; j++) {
                        int li = w->rows + 4 * r + 1 + j;
                        if (li < 60) addLabel(w, li, Rect::xywh(xs[j], LineH * r + 1, ws[j], LineH), RIGHT_WRAP);
                    }
            }
        }
        return;
    }
    switch (s) {
        case 0: addLabel(w, 1, Rect::xywh(10, 10, w->w - 20, 4 * LineH - 8), LEFT_WRAP); break;
        case 12: addLabel(w, 1, Rect(4, 4, w->w, w->h), LEFT_WRAP); break;
        case 1: {
            struct { int id, x, y, w, h, f; } t[] = {
                {12, 0, 0, 200, 20, CENTRE_WRAP}, {13, 200, 0, 100, 20, CENTRE_WRAP}, {14, 300, 0, 100, 20, CENTRE_WRAP},
                {15, 450, 0, 100, 20, CENTRE_WRAP}, {9, 0, 20, 20, 100, RIGHT_WRAP}, {10, 0, 20, 200, 100, CENTRE_WRAP},
                {6, 200, 20, 45, 100, RIGHT_WRAP}, {16, 245, 20, 20, 100, LEFT_WRAP}, {18, 248, 20, 45, 100, RIGHT_WRAP},
                {7, 300, 20, 45, 100, RIGHT_WRAP}, {17, 345, 20, 20, 100, LEFT_WRAP}, {19, 348, 20, 45, 100, RIGHT_WRAP},
                {8, 450, 20, 100, 100, CENTRE_WRAP}};
            for (auto& e : t) addLabel(w, e.id, Rect::xywh(e.x, e.y, e.w, e.h), e.f);
            break;
        }
        case 9: {
            struct { int id, x, y, w, f; } t[] = {
                {21, 30, 10, 140, SINGLE_VC}, {22, 160, 10, 90, SINGLE_VC_RIGHT}, {44, 270, 10, 160, SINGLE_VC},
                {13, 40, 30, 60, SINGLE_VC_RIGHT}, {6, 100, 30, 40, SINGLE_VC_RIGHT}, {16, 140, 30, 20, SINGLE_VC},
                {18, 150, 30, 40, SINGLE_VC_RIGHT}, {14, 40, 50, 60, SINGLE_VC_RIGHT}, {7, 100, 50, 40, SINGLE_VC_RIGHT},
                {17, 140, 50, 20, SINGLE_VC}, {19, 150, 50, 40, SINGLE_VC_RIGHT}, {11, 0, 70, 100, SINGLE_VC_RIGHT},
                {12, 130, 70, 60, SINGLE_VC_RIGHT}, {48, 0, 90, 100, SINGLE_VC_RIGHT}, {49, 130, 90, 60, SINGLE_VC_RIGHT},
                {50, 0, 110, 100, SINGLE_VC_RIGHT}, {51, 130, 110, 60, SINGLE_VC_RIGHT}, {24, 0, 130, 100, SINGLE_VC_RIGHT},
                {27, 130, 130, 60, SINGLE_VC_RIGHT}, {25, 0, 150, 100, SINGLE_VC_RIGHT}, {28, 130, 150, 60, SINGLE_VC_RIGHT},
                {26, 0, 170, 100, SINGLE_VC_RIGHT}, {29, 100, 170, 90, SINGLE_VC_RIGHT}, {32, 200, 30, 110, SINGLE_VC_RIGHT},
                {45, 200, 50, 110, SINGLE_VC_RIGHT}, {33, 200, 70, 110, SINGLE_VC_RIGHT}, {34, 200, 90, 110, SINGLE_VC_RIGHT},
                {35, 200, 110, 110, SINGLE_VC_RIGHT}, {36, 310, 30, 140, SINGLE_VC}, {46, 310, 50, 140, SINGLE_VC},
                {37, 310, 70, 140, SINGLE_VC}, {38, 310, 90, 140, SINGLE_VC}, {39, 310, 110, 140, SINGLE_VC},
                {30, 200, 200, 110, SINGLE_VC_RIGHT}, {31, 310, 200, 90, SINGLE_VC_RIGHT}};
            for (auto& e : t) addLabel(w, e.id, Rect::xywh(e.x, e.y, e.w, 20), e.f);
            break;
        }
        case 10:
            for (int k = 0; k < 8; k++) addLabel(w, 40 + k, Rect::xywh(10, 10 + 20 * k, 130, 20), SINGLE_VC);
            break;
        default: break;
    }
}

Window* create(int slot, int x, int y, int w, int h, bool selectable, int rows, int fmt, const sjis& itemText, int itemCount) {
    if (slot < 0 || slot >= 13) return nullptr;
    if (slots[slot]) return slots[slot];
    Window* win = new Window();
    win->slot = slot;
    win->x = x; win->y = y; win->w = std::max(w, 6); win->h = std::max(h, 6);
    win->selectable = selectable;
    win->rows = rows;
    win->fmt = fmt;
    win->itemCount = itemCount;
    win->transparent = transparentMode;
    win->normalColor = win->transparent ? C_WHITE : C_BLACK;
    if (win->transparent) darkenSnapshot(win);
    layoutLabels(win, fmt);
    if (!itemText.empty()) {
        size_t p = 0;
        int li = 1;
        while (p <= itemText.size() && li < 60) {
            size_t e = itemText.find('\n', p);
            sjis line = itemText.substr(p, e == sjis::npos ? sjis::npos : e - p);
            while (li < 60 && !win->labels[li].present) li++;
            if (li >= 60) break;
            win->labels[li].text = line;
            li++;
            if (e == sjis::npos) break;
            p = e + 1;
        }
    }
    slots[slot] = win;
    g_z.erase(std::remove(g_z.begin(), g_z.end(), slot), g_z.end());
    g_z.push_back(slot);
    focused = slot;
    repaint(win);
    return win;
}

void destroy(int slot) {
    if (!exists(slot)) return;
    delete slots[slot];
    slots[slot] = nullptr;
    g_z.erase(std::remove(g_z.begin(), g_z.end(), slot), g_z.end());
    disp::markDirty();
}

void destroyAll() {
    for (int s = 0; s < 13; s++) destroy(s);
}

void setText(int slot, int label, const sjis& text) {
    static bool logOn = getenv("OPENMK95_LOG") != nullptr;
    if (logOn && (slot == 0 || slot == 12) && label == 1) fprintf(stderr, "[msg%d] %s\n", slot, sj::toUtf8(text).c_str());
    Window* w = get(slot);
    if (!w || label < 0 || label >= 60 || !w->labels[label].present) return;
    w->labels[label].text = text;
    repaint(w);
}
void setColor(int slot, int label, int color) {
    Window* w = get(slot);
    if (!w || label < 0 || label >= 60 || !w->labels[label].present) return;
    w->labels[label].color = color;
    repaint(w);
}
void setDisabled(int slot, int label, bool dis) {
    Window* w = get(slot);
    if (!w || label < 0 || label >= 60 || !w->labels[label].present) return;
    w->labels[label].disabled = dis;
    repaint(w);
}
void setFormat(int slot, int label, int fmt) {
    Window* w = get(slot);
    if (!w || label < 0 || label >= 60 || !w->labels[label].present) return;
    w->labels[label].fmt = fmt;
}

// ------------------------------------------------------------------ helpers
void listHelper(int x, int y, int w, int rows, const sjis& itemText, int L, int fmt, int slot, unsigned disableMask,
                bool colorByState, int partyColumn) {
    selectedIndex = -1;
    int H = LineH * rows + 6;
    if (L > 0) w = (L + 1) * W;
    int Wr = (w / 32 + 1) * 32;
    if (y + H > 480) y = 440 - H;
    if (x < 0) x = (640 - Wr) / 2;
    if (exists(slot)) return;
    Window* win = create(slot, x, y, Wr, H, true, rows, fmt, itemText, rows);
    win->labels[0].fmt = fmt;
    for (int i = 0; i < 32; i++)
        if (disableMask & (1u << i))
            if (i + 1 < 60 && win->labels[i + 1].present) win->labels[i + 1].disabled = true;
    if (colorByState) {
        for (int i = 0; i < rows && i < 8; i++) {
            const Battler& b = G.party[i];
            if (i + 1 < 60) win->labels[i + 1].color = (b.status == 0 && b.hp > 0) ? win->normalColor : C_RED;
        }
    }
    if (partyColumn) {
        for (int i = 0; i < rows && i < 8; i++) {
            const Battler& b = G.party[i];
            int base = rows + 4 * i + 1;
            if (base + 3 >= 60) break;
            win->labels[base].text = partyColumn == 2 ? "MP" : "HP";
            win->labels[base + 1].text = ::fmt("%d", partyColumn == 2 ? b.mp : b.hp);
            win->labels[base + 2].text = "/";
            win->labels[base + 3].text = ::fmt("%d", partyColumn == 2 ? b.maxmp : b.maxhp);
        }
    }
    repaint(win);
}

void pagedSetRow(int slot, int row, const sjis& name, const sjis& value, bool disabled) {
    Window* w = get(slot);
    if (!w || row < 0 || row >= 10) return;
    w->labels[1 + row].fmt = LEFT_PLAIN;
    w->labels[11 + row].fmt = RIGHT_PLAIN;
    w->labels[1 + row].text = name;
    w->labels[11 + row].text = value;
    w->labels[1 + row].disabled = disabled;
    w->labels[11 + row].disabled = disabled;
    repaint(w);
}

void pagedClearRest(int slot, int fromRow) {
    Window* w = get(slot);
    if (!w) return;
    for (int r = fromRow; r < 10; r++) { w->labels[1 + r].text.clear(); w->labels[11 + r].text.clear(); }
    repaint(w);
}

void sendEndOfList(int slot) {
    Notif n; n.type = N_ENDLIST; n.slot = slot;
    post(n);
}

void handleWindowNotif(const Notif& n) {
    Window* w = get(n.slot);
    if (!w) return;
    switch (n.type) {
        case N_ACTIVATE: w->active = true; w->selPending = false; focused = n.slot; repaint(w); break;
        case N_DEACTIVATE: w->active = false; break;
        case N_ENDLIST: w->btnNext = false; w->lastPage = true; repaint(w); break;
        default: break;
    }
}

// ------------------------------------------------------------------ cursor
void eraseCursor(Window* w) { (void)w; }
void drawCursor(Window* w) { repaint(w); }
void moveCursor(Window* w, int row) {
    if (row != w->curRow && row >= 0 && row < w->rows) w->curRow = row;
    repaint(w);
}

static void pageIndicator(Window* w) {
    w->labels[46].text = fmt("%d", w->page + 1);
    const sjis& t = w->labels[47].text;
    int N = t.size() > 1 ? c_atoi(t.c_str() + 1) : 1;
    w->labels[47].text = fmt("/%d )", N);
}

static void pagePrev(Window* w) {
    w->btnNext = true;
    w->page--;
    Notif n; n.type = N_PAGE; n.slot = w->slot; n.page = w->page; n.dir = -1;
    post(n);
    if (w->page == 0) w->btnPrev = false;
    w->lastPage = false;
    pageIndicator(w);
    repaint(w);
}

static void pageNext(Window* w) {
    w->btnPrev = true;
    w->page++;
    Notif n; n.type = N_PAGE; n.slot = w->slot; n.page = w->page; n.dir = 1;
    post(n);
    if (w->page == 9) w->btnNext = false;
    pageIndicator(w);
    repaint(w);
}

static bool paged(Window* w) { return w->slot == 4 || w->slot == 5 || w->slot == 7 || w->slot == 8; }

static void trySelect(Window* w, bool byMouse) {
    int index = w->page * 10 + w->curRow + w->curCol;
    int li = index % 10 + 1;
    if (index >= 0 && index < w->itemCount && li < 60 && w->labels[li].present && !w->labels[li].disabled) {
        w->selPending = true;
        Notif n; n.type = N_SELECTED; n.slot = w->slot; n.index = index; n.byMouse = byMouse;
        post(n);
    }
}

void keyToWindow(int slot, int vk) {
    Window* w = get(slot);
    if (!w || !w->active) return;
    switch (vk) {
        case VK_ESCAPE_: { Notif n; n.type = N_CANCELLED; n.slot = slot; post(n); break; }
        case VK_RETURN_:
        case VK_SPACE_:
            if (!w->selectable) { Notif n; n.type = N_SELECTED; n.slot = slot; n.index = 0; n.byMouse = true; post(n); }
            else trySelect(w, false);
            break;
        case VK_UP_:
        case VK_NUMPAD8_: {
            if (w->selPending || !w->labels[0].present || !w->selectable) break;
            int t = w->curRow < 1 ? w->curRow + w->rows - 1 : w->curRow - 1;
            moveCursor(w, t);
            break;
        }
        case VK_DOWN_:
        case VK_NUMPAD2_: {
            if (w->selPending || !w->labels[0].present || !w->selectable) break;
            int t = w->curRow >= w->rows - 1 ? w->curRow - (w->rows - 1) : w->curRow + 1;
            moveCursor(w, t);
            break;
        }
        case VK_LEFT_:
        case VK_NUMPAD4_:
            if (paged(w) && !w->selPending && w->page > 0) pagePrev(w);
            break;
        case VK_RIGHT_:
        case VK_NUMPAD6_:
            if (paged(w) && !w->selPending && w->page < 9 && !w->lastPage) pageNext(w);
            break;
        default: break;
    }
}

Window* windowAt(int x, int y) {
    for (int i = (int)g_z.size() - 1; i >= 0; i--) {
        Window* w = slots[g_z[i]];
        if (w && x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) return w;
    }
    return nullptr;
}

void mouseToWindow(Window* w, int type, int x, int y) {
    int cx = x - w->x - 3, cy = y - w->y - 3;
    // The 3-pixel frame is the window's non-client border (a dialog frame): mouse input
    // over it reaches neither the window's client handling nor the game screen.
    if (cx < 0 || cy < 0 || cx >= w->cw() || cy >= w->ch()) return;
    if (type == 0) {
        if (w->selectable && w->labels[0].present && !w->selPending && cy >= 0) {
            int row = cy / LineH;
            if (row != w->curRow && row < w->rows) moveCursor(w, row);
        }
        return;
    }
    if (type == 2) {
        if (focused >= 0) { Notif n; n.type = N_CANCELLED; n.slot = focused; post(n); }
        return;
    }
    // left button
    if (w->hasButtons && cy >= 182 && cy < 202) {
        if (cx >= 145 && cx < 185) { if (w->btnPrev && w->page > 0) pagePrev(w); return; }
        if (cx >= 190 && cx < 230) { if (w->btnNext && w->page < 9 && !w->lastPage) pageNext(w); return; }
    }
    if (!w->selectable) { Notif n; n.type = N_SELECTED; n.slot = w->slot; n.index = 0; n.byMouse = true; post(n); return; }
    if (cy < 180) trySelect(w, true);
}

}  // namespace ui
