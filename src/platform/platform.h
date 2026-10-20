// Host platform layer (SDL2): window, frame output, input queue, clock.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Windows virtual-key codes are used for all keys inside the runtime.
enum {
    VK_BACK_ = 8, VK_TAB_ = 9, VK_RETURN_ = 13, VK_SHIFT_ = 16, VK_CONTROL_ = 17, VK_MENU_ = 18,
    VK_PAUSE_ = 19, VK_ESCAPE_ = 27, VK_SPACE_ = 32, VK_LEFT_ = 37, VK_UP_ = 38, VK_RIGHT_ = 39,
    VK_DOWN_ = 40, VK_CANCEL_ = 3, VK_NUMPAD2_ = 98, VK_NUMPAD4_ = 100, VK_NUMPAD6_ = 102,
    VK_NUMPAD8_ = 104, VK_F3_ = 114, VK_F4_ = 115, VK_F12_ = 123
};

namespace plat {

enum EvType { EV_KEYDOWN, EV_KEYUP, EV_MOUSEDOWN, EV_MOUSEUP, EV_MOUSEMOVE, EV_DBLCLICK, EV_CLOSE };
enum { BTN_LEFT = 1, BTN_RIGHT = 2, BTN_MIDDLE = 3 };

struct InputEvent {
    EvType type;
    int key = 0;      // virtual key for key events
    bool ctrl = false, alt = false;
    int button = 0;   // mouse button
    int x = 0, y = 0; // game-area coordinates
};

struct Options {
    bool fullscreen = false;
    int scale = 0;            // 0 = automatic
    bool virtualTime = false; // deterministic clock for tests
    bool headless = false;    // no visible window (tests)
    std::string script;       // input script for automated tests
    bool smooth = false;      // linear filtering when scaling
};

bool init(const Options& o, const std::string& captionUtf8);
void shutdown();
void setCaption(const std::string& utf8);

// Millisecond clock.
uint32_t now();
// Yield to the OS for about 1 ms (the runtime's loops call this when idle).
void idle();
// Busy delay of ms milliseconds: the window stays alive, input is queued but not handled.
void delay(int ms);
// Wait until the start of the next display refresh (60 Hz).
void waitVBlank();

// Read pending OS events into the input queue (no game handling).
void pumpOS();
bool popInput(InputEvent& ev);
bool hasInput();
// Remove all queued keyboard events (presses and releases).
void discardKeyboardEvents();
// Current held state of a key (VK code), as last seen by pumpOS.
bool keyHeld(int vk);
// Current mouse position in game coordinates.
void mousePos(int& x, int& y);
bool mouseButtonHeld(int button);
void captureMouse(bool on);
// Busy/normal mouse pointer.
void setBusyCursor(bool busy);

// Frame output. The display module registers a callback that composes the frame
// when needed; showFrame() sends a 640x480 0x00RRGGBB frame to the host window.
void setRefreshCallback(void (*cb)(bool force));
void showFrame(const uint32_t* rgb);
// Request that the next pump re-composes the frame.
void requestRefresh();

bool isFullscreen();
void setFullscreen(bool on);

// Message boxes. Returns true for OK/Yes.
enum MsgIcon { MB_NONE, MB_ERROR, MB_WARNING, MB_QUESTION };
bool messageBox(const std::string& textUtf8, const std::string& captionUtf8, MsgIcon icon, bool yesNo);

// Save a screenshot of the last composed frame (BMP).
bool saveScreenshot(const std::string& path);
// True while a test script is running / finished.
bool scriptFinished();

std::string basePath();
// Wait for a file/folder to be dropped on the window. Returns false if the window was closed.
bool waitForDrop(std::string& pathUtf8);

}  // namespace plat
