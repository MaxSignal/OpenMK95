// Global run-time state of the RM95 runtime (00-README.md §5, 01-runtime.md §1).
#pragma once
#include <memory>
#include <vector>
#include "engine/data.h"
#include "core/gfx.h"

enum ScreenState {
    SS_PICTURE_BEFORE_MAP = -2, SS_BEFORE_START = -1, SS_MAP = 0, SS_PICTURE_OVER_MAP = 1,
    SS_PICTURE_ON_BLACK = 2, SS_ERASED = 3, SS_MOVIE = 4
};

enum { ROUTE_NONE = 0x1C, ROUTE_RUNNING = 0, ROUTE_REPEAT = 0x1D, ROUTE_BACK = 0x1E, ROUTE_PINGPONG = 0x1F };
enum { DISP_VISIBLE = 0x19, DISP_INVISIBLE = 0x1A, DISP_REMOVED = 0x1B };
enum { MT_FIXED = 0, MT_APPROACH = 1, MT_AVOID = 2, MT_RANDOM = 3, MT_PLAYER = 5, MT_BOARDING = 6, MT_ALIGHTING = 7, MT_AUTOWALK = 9 };
enum { MOTION_IDLE = -1, MOTION_WAIT = 17 };
enum { POST_NONE, POST_TELEPORTED, POST_BOARDED, POST_ALIGHTED };

// A map character (party member, map event or vehicle; 04-map-movement.md §3.2).
struct Chara {
    int x = -1, y = -1, destX = -1, destY = -1;
    int onScreen = 0;
    uint16_t eventId = 0;
    int facingOverride = 2, facing = 2, moveType = 5, motion = -1, graphic = 0, unused = 0;
    int substep = 0, animTimer = 0, animFrame = 0, layer = 1, speed = 2, speedCounter = 0;
    int vehicleType = 0, animType = 0x12, display = 0x19, cooldown = 1;
    // route
    std::vector<uint8_t> route;
    int routePos = 0, routeMode = ROUTE_NONE, routeStep = 1;
    // event object part
    int resumeIndex = 0;
    int page = -1;
    PageAttr attr;
    std::vector<uint8_t> pageRoutes[4];
    bool hasRoute() const { return routeMode != ROUTE_NONE; }
    int regX() const { return substep < 2 ? x : destX; }
    int regY() const { return substep < 2 ? y : destY; }
};

struct VehicleSlot {
    uint16_t id = 0;
    int32_t map = 0;
    std::unique_ptr<Chara> obj;
};

struct BgmRec {
    bool has = false;   // "present" / name exists
    sjis name;
    int32_t a = 0, b = 0;
};

struct HeroWait {
    int id = -1;
    int page = 0, index = 0;
    Chara* obj = nullptr;
    void clear() { id = -1; page = 0; index = 0; obj = nullptr; }
};

struct ActionRec {
    int command = 0, target = 0, item = 0, spell = 0;
};

struct Game {
    // shared run-time flags (01-runtime.md §1)
    bool quit = false, passAbort = false, arrival = false;
    int postPass = POST_NONE;
    bool battleLost = false, confirm = false, releaseLatch = true;
    int lastKey = -1;
    bool menuRequest = false, eventRunning = true, inBattle = false, displaySwitch = false, pageRefresh = false;
    uint32_t lastTick = 0;
    int moveControl = 0;
    int screenState = SS_ERASED;
    bool lastJoyEscape = false;

    // party (00-README.md §5)
    Battler party[8];
    int partySize = 0;
    int8_t equip[8][5];
    uint8_t learn[8][100];
    int16_t itemCount[100];
    int32_t money = 0;
    uint8_t switches[128];
    int partyActor[8];
    uint8_t partyMask = 0;
    std::unique_ptr<Chara> actorChar[8];   // one map character per actor
    Chara* partyChar[8];                   // party slot -> map character (slot 0 = vehicle while riding)
    int shown = 0;                         // party characters shown on the map
    int fightSize() const { return partySize < 4 ? partySize : 4; }

    // map
    int curMap = -1;
    int W = 0, H = 0;
    int mapType = 0;
    std::vector<uint16_t> tiles;
    std::vector<std::unique_ptr<Chara>> events;  // event list (list order)
    VehicleSlot vehicles[10];
    bool riding = false;
    int ox = 0, oy = 0, cpx = 0, cpy = 0;
    bool scrollFlag[4] = {false, false, false, false};
    Chara* grid[3][17][22];
    std::vector<uint16_t> areaTable;
    int encArea = -1, encSteps = 0;
    int trail[8];
    int scrollDir = -1, heroScrollSub = 0;
    Chara* passHero = nullptr;  // the hero fetched at the start of the main-loop pass

    // events
    HeroWait heroWait;
    bool suspend = false;
    int runningEventId = -1;

    // misc settings
    int16_t teleportList[100];
    int16_t teleportForbidden = 0, escapeMap = 0;
    bool saveEnabled = true;
    AudioRec slots[12];
    int32_t trans[3] = {0, 0, 0};
    uint8_t tint[4] = {100, 100, 100, 0};

    // mouse
    bool drag = false;
    int dragX = 0, dragY = 0;
    int autoWalkDir = 5, sameDirCount = 0;

    // battle action records (persist)
    ActionRec actions[8];

    // escape after battle
    int escapeAfterBattle = 0;
    bool battleEscapeForbidden = false;
};

extern Game G;

// switches
inline bool switchOn(int n) { return n >= 0 && n < 1024 && ((G.switches[n >> 3] >> (n & 7)) & 1); }
inline void setSwitch(int n, bool on) {
    if (n < 0 || n >= 1024) return;
    if (on) G.switches[n >> 3] |= (uint8_t)(1 << (n & 7)); else G.switches[n >> 3] &= (uint8_t)~(1 << (n & 7));
}

inline Chara* hero() { return G.partyChar[0]; }
inline const int DX[4] = {0, 1, 0, -1};
inline const int DY[4] = {-1, 0, 1, 0};
