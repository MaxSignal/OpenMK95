// The window system of 06-windows-text.md: 13 window slots, labels, cursor, input.
#pragma once
#include <deque>
#include <memory>
#include "core/common.h"
#include "core/font.h"
#include "core/gfx.h"

namespace ui {

const int LineH = font::LineH;  // 18
const int W = font::MaxW;       // 17

enum Fmt { LEFT_WRAP, CENTRE_WRAP, RIGHT_WRAP, LEFT_PLAIN, RIGHT_PLAIN, SINGLE_VC, SINGLE_VC_RIGHT };

// palette indices of the static colours
enum { C_BLACK = 0, C_LTGRAY = 7, C_GRAY = 248, C_RED = 249, C_BLUE = 252, C_WHITE = 255 };

struct Label {
    bool present = false;
    Rect r;
    sjis text;
    int color = 0;
    int fmt = LEFT_WRAP;
    bool disabled = false;
};

struct Window {
    int slot = 0;
    int x = 0, y = 0, w = 0, h = 0;
    bool selectable = false;
    int rows = 0, itemCount = 0, fmt = LEFT_WRAP;
    Label labels[60];
    int curRow = 0, curCol = 0;
    int page = 0;
    bool lastPage = false;
    bool active = true;
    bool selPending = false;
    bool transparent = false;
    int normalColor = C_BLACK;
    std::vector<uint8_t> snapshot;  // darkened background (client area)
    std::vector<uint8_t> px;        // outer w*h
    bool hasButtons = false, btnPrev = false, btnNext = true;
    bool hasCursor() const { return selectable && labels[0].present; }
    int cw() const { return w - 6; }
    int ch() const { return h - 6; }
};

extern Window* slots[13];
extern int focused;
extern int selectedIndex;
extern bool haveSelMouse;
extern int selMouseX, selMouseY;
extern bool transparentMode;  // game setting

enum NType { N_SELECTED, N_CANCELLED, N_ACTIVATE, N_DEACTIVATE, N_PAGE, N_ENDLIST, N_CLOSEMOVIE };
struct Notif {
    NType type;
    int slot = 0, index = 0;
    bool byMouse = false;
    int page = 0, dir = 0;
};
extern std::deque<Notif> notifs;
void post(const Notif& n);

void init();
// Create a window (direct creation). Returns the window (the slot must be empty;
// if it is occupied the existing window is returned unchanged).
Window* create(int slot, int x, int y, int w, int h, bool selectable, int rows, int fmt,
               const sjis& itemText, int itemCount);
void destroy(int slot);
void destroyAll();
bool exists(int slot);
Window* get(int slot);

void setText(int slot, int label, const sjis& text);
void setColor(int slot, int label, int color);
void setDisabled(int slot, int label, bool dis);
void setFormat(int slot, int label, int fmt);
void repaint(Window* w);
void repaint(int slot);

// list-window helper (§6.2); partyColumn: 0 none, 1 HP, 2 MP
void listHelper(int x, int y, int w, int rows, const sjis& itemText, int L, int fmt, int slot,
                unsigned disableMask, bool colorByState, int partyColumn = 0);

// paged list content (§5.3)
void pagedSetRow(int slot, int row, const sjis& name, const sjis& value, bool disabled);
void pagedClearRest(int slot, int fromRow);
void sendEndOfList(int slot);

// Notifications handled by the window itself (activate, deactivate, end of list).
void handleWindowNotif(const Notif& n);

// Keys from the program's key routing (06 §8.2). Returns nothing; posts notifications.
void keyToWindow(int slot, int vk);
// Mouse: topmost window under the point (or nullptr).
Window* windowAt(int x, int y);
// Mouse handling over an active window: type 0 move, 1 left down, 2 right down.
void mouseToWindow(Window* w, int type, int x, int y);

// cursor helpers
void eraseCursor(Window* w);
void drawCursor(Window* w);
void moveCursor(Window* w, int row);

// draw text into a buffer (used by the save chooser too)
void drawLabelText(uint8_t* buf, int bw, int bh, const Rect& clip, const Rect& r, const sjis& text,
                   int fmt, uint8_t color);

}  // namespace ui
