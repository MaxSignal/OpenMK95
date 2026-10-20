#include "platform/platform.h"
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <sstream>

namespace plat {

static Options g_opt;
static SDL_Window* g_win = nullptr;
static SDL_Renderer* g_ren = nullptr;
static SDL_Texture* g_tex = nullptr;
static std::deque<InputEvent> g_queue;
static bool g_keys[256];
static bool g_mouseBtn[4];
static int g_mx = 0, g_my = 0;
static uint32_t g_vclock = 0;     // virtual clock (ms)
static void (*g_refresh)(bool) = nullptr;
static bool g_refreshReq = true;
static uint32_t g_lastShow = 0;
static std::vector<uint32_t> g_lastFrame(640 * 480);
static bool g_fullscreen = false;
static SDL_Cursor* g_busyCursor = nullptr;
static SDL_Cursor* g_arrowCursor = nullptr;
static SDL_GameController* g_pad = nullptr;
static bool g_padHeld[256];

// ---------------------------------------------------------------- test scripts
struct ScriptCmd { std::string op; std::vector<std::string> a; };
static std::vector<ScriptCmd> g_script;
static size_t g_scriptPos = 0;
static uint32_t g_scriptNext = 0;
static bool g_scriptActive = false;
struct PendingUp { uint32_t t; int key; };
static std::vector<PendingUp> g_pendingUps;

static int keyByName(const std::string& n) {
    static const struct { const char* n; int vk; } tab[] = {
        {"Return", VK_RETURN_}, {"Enter", VK_RETURN_}, {"Space", VK_SPACE_}, {"Escape", VK_ESCAPE_},
        {"Esc", VK_ESCAPE_}, {"Up", VK_UP_}, {"Down", VK_DOWN_}, {"Left", VK_LEFT_}, {"Right", VK_RIGHT_},
        {"Shift", VK_SHIFT_}, {"F3", VK_F3_}, {"F4", VK_F4_}, {"KP2", VK_NUMPAD2_}, {"KP4", VK_NUMPAD4_},
        {"KP6", VK_NUMPAD6_}, {"KP8", VK_NUMPAD8_}, {"A", 'A'}, {"Z", 'Z'}, {"X", 'X'},
    };
    for (auto& t : tab) if (n == t.n) return t.vk;
    if (n.size() == 1) return toupper((unsigned char)n[0]);
    return 0;
}

static void loadScript(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ss(line);
        ScriptCmd c;
        if (!(ss >> c.op) || c.op[0] == '#') continue;
        std::string w;
        while (ss >> w) c.a.push_back(w);
        // "repeat N cmd..." expands
        if (c.op == "repeat" && c.a.size() >= 2) {
            int n = atoi(c.a[0].c_str());
            ScriptCmd r; r.op = c.a[1]; r.a.assign(c.a.begin() + 2, c.a.end());
            for (int i = 0; i < n; i++) g_script.push_back(r);
            continue;
        }
        g_script.push_back(c);
    }
    g_scriptActive = !g_script.empty();
}

static void pushKey(int vk, bool down) {
    if (vk <= 0 || vk > 255) return;
    InputEvent e;
    e.type = down ? EV_KEYDOWN : EV_KEYUP;
    e.key = vk;
    g_keys[vk] = down;
    g_queue.push_back(e);
}

static void pushMouse(EvType t, int btn, int x, int y) {
    InputEvent e;
    e.type = t;
    e.button = btn;
    e.x = x; e.y = y;
    g_mx = x; g_my = y;
    if (t == EV_MOUSEDOWN || t == EV_DBLCLICK) g_mouseBtn[btn & 3] = true;
    if (t == EV_MOUSEUP) g_mouseBtn[btn & 3] = false;
    g_queue.push_back(e);
}

static void runScript() {
    uint32_t t = now();
    for (size_t i = 0; i < g_pendingUps.size();) {
        if ((int32_t)(t - g_pendingUps[i].t) >= 0) { pushKey(g_pendingUps[i].key, false); g_pendingUps.erase(g_pendingUps.begin() + i); }
        else i++;
    }
    if (!g_scriptActive) return;
    while (g_scriptPos < g_script.size() && (int32_t)(t - g_scriptNext) >= 0) {
        ScriptCmd& c = g_script[g_scriptPos++];
        auto arg = [&](size_t k) { return k < c.a.size() ? c.a[k] : std::string(); };
        if (c.op == "wait") { g_scriptNext = t + (uint32_t)atoi(arg(0).c_str()); }
        else if (c.op == "down") pushKey(keyByName(arg(0)), true);
        else if (c.op == "up") pushKey(keyByName(arg(0)), false);
        else if (c.op == "press") {
            int k = keyByName(arg(0));
            int hold = c.a.size() > 1 ? atoi(arg(1).c_str()) : 60;
            pushKey(k, true);
            g_pendingUps.push_back({t + (uint32_t)hold, k});
            g_scriptNext = t + (uint32_t)hold + 40;
        } else if (c.op == "click" || c.op == "rclick" || c.op == "dblclick") {
            int x = atoi(arg(0).c_str()), y = atoi(arg(1).c_str());
            int b = c.op == "rclick" ? BTN_RIGHT : BTN_LEFT;
            pushMouse(EV_MOUSEMOVE, 0, x, y);
            pushMouse(c.op == "dblclick" ? EV_DBLCLICK : EV_MOUSEDOWN, b, x, y);
            pushMouse(EV_MOUSEUP, b, x, y);
        } else if (c.op == "move") {
            pushMouse(EV_MOUSEMOVE, 0, atoi(arg(0).c_str()), atoi(arg(1).c_str()));
        } else if (c.op == "shot") {
            if (g_refresh) g_refresh(true);
            saveScreenshot(arg(0));
        } else if (c.op == "log") {
            std::string s;
            for (auto& w : c.a) s += w + " ";
            fprintf(stderr, "[script %u] %s\n", t, s.c_str());
        } else if (c.op == "quit") {
            InputEvent e; e.type = EV_CLOSE; e.key = 1;  // key=1: forced quit without asking
            g_queue.push_back(e);
        }
    }
    if (g_scriptPos >= g_script.size()) g_scriptActive = false;
}

bool scriptFinished() { return !g_scriptActive && g_pendingUps.empty(); }

// ---------------------------------------------------------------- key mapping
static int mapKey(SDL_Keycode k, SDL_Scancode sc) {
    switch (sc) {
        case SDL_SCANCODE_KP_2: return VK_NUMPAD2_;
        case SDL_SCANCODE_KP_4: return VK_NUMPAD4_;
        case SDL_SCANCODE_KP_6: return VK_NUMPAD6_;
        case SDL_SCANCODE_KP_8: return VK_NUMPAD8_;
        case SDL_SCANCODE_KP_ENTER: return VK_RETURN_;
        default: break;
    }
    switch (k) {
        case SDLK_RETURN: return VK_RETURN_;
        case SDLK_SPACE: return VK_SPACE_;
        case SDLK_ESCAPE: return VK_ESCAPE_;
        case SDLK_UP: return VK_UP_;
        case SDLK_DOWN: return VK_DOWN_;
        case SDLK_LEFT: return VK_LEFT_;
        case SDLK_RIGHT: return VK_RIGHT_;
        case SDLK_LSHIFT: case SDLK_RSHIFT: return VK_SHIFT_;
        case SDLK_LCTRL: case SDLK_RCTRL: return VK_CONTROL_;
        case SDLK_F3: return VK_F3_;
        case SDLK_F4: return VK_F4_;
        case SDLK_F12: return VK_F12_;
        case SDLK_PAUSE: return VK_PAUSE_;
        case SDLK_TAB: return VK_TAB_;
        case SDLK_BACKSPACE: return VK_BACK_;
        default: break;
    }
    if (k >= 'a' && k <= 'z') return (int)(k - 32);
    if (k >= '0' && k <= '9') return (int)k;
    // Every other key is an ordinary key press for the runtime (01 §4.2: it is stored as the last key on
    // the map, clears the release latch otherwise, ends mouse auto-walk, closes a lingering message
    // window; its release re-arms the latch). Windows VK codes; F10 and Alt are system keys and never
    // reach the key handler (they are not mapped).
    if (k >= SDLK_F1 && k <= SDLK_F12 && k != SDLK_F10) return 112 + (int)(k - SDLK_F1);
    switch (sc) {
        case SDL_SCANCODE_KP_0: return 96;
        case SDL_SCANCODE_KP_1: return 97;
        case SDL_SCANCODE_KP_3: return 99;
        case SDL_SCANCODE_KP_5: return 101;
        case SDL_SCANCODE_KP_7: return 103;
        case SDL_SCANCODE_KP_9: return 105;
        case SDL_SCANCODE_KP_MULTIPLY: return 106;
        case SDL_SCANCODE_KP_PLUS: return 107;
        case SDL_SCANCODE_KP_MINUS: return 109;
        case SDL_SCANCODE_KP_PERIOD: return 110;
        case SDL_SCANCODE_KP_DIVIDE: return 111;
        case SDL_SCANCODE_INSERT: return 45;
        case SDL_SCANCODE_DELETE: return 46;
        case SDL_SCANCODE_HOME: return 36;
        case SDL_SCANCODE_END: return 35;
        case SDL_SCANCODE_PAGEUP: return 33;
        case SDL_SCANCODE_PAGEDOWN: return 34;
        case SDL_SCANCODE_CAPSLOCK: return 20;
        case SDL_SCANCODE_NUMLOCKCLEAR: return 144;
        case SDL_SCANCODE_SCROLLLOCK: return 145;
        case SDL_SCANCODE_SEMICOLON: return 0xBA;
        case SDL_SCANCODE_EQUALS: return 0xBB;
        case SDL_SCANCODE_COMMA: return 0xBC;
        case SDL_SCANCODE_MINUS: return 0xBD;
        case SDL_SCANCODE_PERIOD: return 0xBE;
        case SDL_SCANCODE_SLASH: return 0xBF;
        case SDL_SCANCODE_GRAVE: return 0xC0;
        case SDL_SCANCODE_LEFTBRACKET: return 0xDB;
        case SDL_SCANCODE_BACKSLASH: return 0xDC;
        case SDL_SCANCODE_RIGHTBRACKET: return 0xDD;
        case SDL_SCANCODE_APOSTROPHE: return 0xDE;
        case SDL_SCANCODE_INTERNATIONAL1: return 0xE2;
        case SDL_SCANCODE_INTERNATIONAL3: return 0xDC;
        default: break;
    }
    return 0;
}

static void winToGame(int wx, int wy, int& gx, int& gy) {
    // SDL_RenderSetLogicalSize already converts mouse coordinates for us.
    gx = wx; gy = wy;
}

static void padButton(int vk, bool down) {
    if (g_padHeld[vk] == down) return;
    g_padHeld[vk] = down;
    pushKey(vk, down);
}

// ---------------------------------------------------------------- init
bool init(const Options& o, const std::string& caption) {
    g_opt = o;
    memset(g_keys, 0, sizeof g_keys);
    memset(g_padHeld, 0, sizeof g_padHeld);
    if (o.headless) SDL_setenv("SDL_VIDEODRIVER", "offscreen", 0);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, o.smooth ? "1" : "0");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    int scale = o.scale;
    if (scale <= 0) {
        scale = 1;
        SDL_DisplayMode dm;
        if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.w >= 1600 && dm.h >= 1100) scale = 2;
    }
    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (o.headless) flags = SDL_WINDOW_HIDDEN;
    g_win = SDL_CreateWindow(caption.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640 * scale, 480 * scale, flags);
    if (!g_win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false; }
    g_ren = SDL_CreateRenderer(g_win, -1, 0);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    if (!g_ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return false; }
    SDL_RenderSetLogicalSize(g_ren, 640, 480);
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, 640, 480);
    g_busyCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
    g_arrowCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    if (o.fullscreen) setFullscreen(true);
    SDL_StopTextInput();
    if (!o.script.empty()) loadScript(o.script);
    g_scriptNext = now();
    return true;
}

void shutdown() {
    if (g_pad) SDL_GameControllerClose(g_pad);
    if (g_tex) SDL_DestroyTexture(g_tex);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    SDL_Quit();
}

void setCaption(const std::string& s) { if (g_win) SDL_SetWindowTitle(g_win, s.c_str()); }

uint32_t now() { return g_opt.virtualTime ? g_vclock : SDL_GetTicks(); }

static void maybeRefresh(bool force) {
    if (!g_refresh) return;
    uint32_t t = now();
    if (force || (g_refreshReq && (t - g_lastShow >= 4 || g_opt.virtualTime))) {
        g_refreshReq = false;
        g_lastShow = t;
        g_refresh(force);
    }
}

void requestRefresh() { g_refreshReq = true; }

void pumpOS() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT: { InputEvent ev; ev.type = EV_CLOSE; g_queue.push_back(ev); break; }
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                int vk = mapKey(e.key.keysym.sym, e.key.keysym.scancode);
                bool down = e.type == SDL_KEYDOWN;
                // Ctrl+Break: SDL reports Pause with Ctrl
                if (vk == VK_PAUSE_ && (e.key.keysym.mod & KMOD_CTRL)) vk = VK_CANCEL_;
                if (vk <= 0) break;
                g_keys[vk] = down;
                InputEvent ev;
                ev.type = down ? EV_KEYDOWN : EV_KEYUP;
                ev.key = vk;
                ev.ctrl = (e.key.keysym.mod & KMOD_CTRL) != 0;
                ev.alt = (e.key.keysym.mod & KMOD_ALT) != 0;
                g_queue.push_back(ev);
                break;
            }
            case SDL_MOUSEMOTION: {
                InputEvent ev; ev.type = EV_MOUSEMOVE;
                winToGame(e.motion.x, e.motion.y, ev.x, ev.y);
                g_mx = ev.x; g_my = ev.y;
                g_queue.push_back(ev);
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                int b = e.button.button == SDL_BUTTON_LEFT ? BTN_LEFT : e.button.button == SDL_BUTTON_RIGHT ? BTN_RIGHT : BTN_MIDDLE;
                InputEvent ev;
                winToGame(e.button.x, e.button.y, ev.x, ev.y);
                g_mx = ev.x; g_my = ev.y;
                ev.button = b;
                if (e.type == SDL_MOUSEBUTTONDOWN) {
                    ev.type = (e.button.clicks >= 2 && (e.button.clicks % 2) == 0) ? EV_DBLCLICK : EV_MOUSEDOWN;
                    g_mouseBtn[b] = true;
                } else {
                    ev.type = EV_MOUSEUP;
                    g_mouseBtn[b] = false;
                }
                g_queue.push_back(ev);
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
                if (!g_pad) g_pad = SDL_GameControllerOpen(e.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (g_pad && SDL_GameControllerFromInstanceID(e.cdevice.which) == g_pad) { SDL_GameControllerClose(g_pad); g_pad = nullptr; }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP: {
                bool d = e.type == SDL_CONTROLLERBUTTONDOWN;
                switch (e.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_UP: padButton(VK_UP_, d); break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: padButton(VK_DOWN_, d); break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: padButton(VK_LEFT_, d); break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: padButton(VK_RIGHT_, d); break;
                    case SDL_CONTROLLER_BUTTON_A: padButton(VK_RETURN_, d); break;
                    case SDL_CONTROLLER_BUTTON_B: padButton(VK_ESCAPE_, d); break;
                    case SDL_CONTROLLER_BUTTON_X: padButton(VK_SHIFT_, d); break;
                    default: break;
                }
                break;
            }
            case SDL_CONTROLLERAXISMOTION: {
                const int T = 16000;
                if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                    padButton(VK_LEFT_, e.caxis.value < -T);
                    padButton(VK_RIGHT_, e.caxis.value > T);
                } else if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    padButton(VK_UP_, e.caxis.value < -T);
                    padButton(VK_DOWN_, e.caxis.value > T);
                }
                break;
            }
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_EXPOSED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) g_refreshReq = true;
                break;
            default: break;
        }
    }
    runScript();
    maybeRefresh(false);
}

void idle() {
    if (g_opt.virtualTime) g_vclock += 1;
    else SDL_Delay(1);
    pumpOS();
}

void delay(int ms) {
    if (ms <= 0) { pumpOS(); return; }
    uint32_t start = now();
    if (g_opt.virtualTime) {
        g_vclock += (uint32_t)ms;
        pumpOS();
        return;
    }
    while ((int)(now() - start) < ms) {
        pumpOS();
        uint32_t el = now() - start;
        if ((int)el < ms - 1) SDL_Delay(1);
    }
}

void waitVBlank() {
    // 60 Hz boundaries of the millisecond clock.
    uint32_t t = now();
    uint32_t frame = (uint32_t)((uint64_t)t * 60 / 1000) + 1;
    uint32_t target = (uint32_t)((uint64_t)frame * 1000 / 60);
    if (g_opt.virtualTime) { g_vclock = target; pumpOS(); return; }
    while ((int32_t)(target - now()) > 0) {
        pumpOS();
        if ((int32_t)(target - now()) > 1) SDL_Delay(1);
    }
}

bool popInput(InputEvent& ev) {
    if (g_queue.empty()) return false;
    ev = g_queue.front();
    g_queue.pop_front();
    return true;
}

bool hasInput() { return !g_queue.empty(); }

void discardKeyboardEvents() {
    // all keyboard messages still pending at the OS level are discarded too (01 §5.2)
    pumpOS();
    std::deque<InputEvent> keep;
    for (auto& e : g_queue)
        if (e.type != EV_KEYDOWN && e.type != EV_KEYUP) keep.push_back(e);
    g_queue.swap(keep);
}

bool keyHeld(int vk) { return vk > 0 && vk < 256 && g_keys[vk]; }
void mousePos(int& x, int& y) { x = g_mx; y = g_my; }
bool mouseButtonHeld(int b) { return g_mouseBtn[b & 3]; }
void captureMouse(bool on) { SDL_CaptureMouse(on ? SDL_TRUE : SDL_FALSE); }
void setBusyCursor(bool busy) { SDL_SetCursor(busy ? g_busyCursor : g_arrowCursor); }

void setRefreshCallback(void (*cb)(bool)) { g_refresh = cb; }

void showFrame(const uint32_t* rgb) {
    memcpy(g_lastFrame.data(), rgb, 640 * 480 * 4);
    if (g_opt.headless || !g_tex) return;
    SDL_UpdateTexture(g_tex, nullptr, rgb, 640 * 4);
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, nullptr, nullptr);
    SDL_RenderPresent(g_ren);
}

bool isFullscreen() { return g_fullscreen; }

void setFullscreen(bool on) {
    if (!g_win || g_opt.headless) { g_fullscreen = on; return; }
    SDL_SetWindowFullscreen(g_win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    g_fullscreen = on;
    g_refreshReq = true;
}

bool messageBox(const std::string& text, const std::string& caption, MsgIcon icon, bool yesNo) {
    fprintf(stderr, "[msgbox] %s: %s\n", caption.c_str(), text.c_str());
    if (g_opt.headless || g_scriptActive) return true;
    SDL_MessageBoxButtonData btns[2];
    int n = 0;
    if (yesNo) {
        btns[0] = {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "はい"};
        btns[1] = {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "いいえ"};
        n = 2;
    } else {
        btns[0] = {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "OK"};
        n = 1;
    }
    SDL_MessageBoxData d;
    memset(&d, 0, sizeof d);
    d.flags = icon == MB_ERROR ? SDL_MESSAGEBOX_ERROR : icon == MB_WARNING ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_INFORMATION;
    d.window = g_win;
    d.title = caption.c_str();
    d.message = text.c_str();
    d.numbuttons = n;
    d.buttons = btns;
    int hit = 0;
    if (SDL_ShowMessageBox(&d, &hit) != 0) return true;
    // drop the keys that were held when the box closed
    memset(g_keys, 0, sizeof g_keys);
    return hit == 1;
}

bool saveScreenshot(const std::string& path) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(g_lastFrame.data(), 640, 480, 32, 640 * 4, SDL_PIXELFORMAT_RGB888);
    if (!s) return false;
    int r = SDL_SaveBMP(s, path.c_str());
    SDL_FreeSurface(s);
    return r == 0;
}

bool waitForDrop(std::string& path) {
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return false;
            if (e.type == SDL_DROPFILE) { path = e.drop.file; SDL_free(e.drop.file); return true; }
            if (e.type == SDL_WINDOWEVENT && g_refresh) g_refresh(true);
        }
        SDL_Delay(10);
    }
}

std::string basePath() {
    char* p = SDL_GetBasePath();
    std::string r = p ? p : "";
    if (p) SDL_free(p);
    return r;
}

}  // namespace plat
