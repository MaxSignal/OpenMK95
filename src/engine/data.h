// Game data files (02-data-formats.md).
#pragma once
#include <array>
#include <string>
#include <vector>
#include "core/common.h"

struct AudioRec {
    bool hasName = false;
    sjis name;
    int32_t a = 0, b = 0;
};

struct Actor {
    int32_t graphic = 0, startMember = 0;
    sjis name;
    int32_t level = 1, hp = 0, mp = 0, vit = 0, pow = 0, intl = 0, agi = 0, luck = 0, atk = 0, def = 0;
    uint8_t curvePow[100], curveVit[100], curveInt[100], curveAgi[100], curveLuck[100];
    int8_t equip[5];
    uint8_t learn[100];
    uint8_t rates[100];
};

struct MapRec {
    sjis name;
    int32_t type = 0, areaX = -1, areaY = -1;
    uint16_t w = 0, h = 0;
    int32_t parent = -1;
    uint16_t sibling = 0, child = 0;
    int32_t exitX = 0, exitY = 0;
    uint16_t enc[10] = {0};
    sjis chip[4];
    bool hasChip = false;
    std::vector<uint16_t> tiles;   // w*h
    std::vector<uint16_t> events;  // w*h, 0 = none
};

#pragma pack(push, 1)
struct Battler {
    int16_t level, hp, mdef, maxhp, unused08, mp, maxmp, vit, pow, agi, intl, luck, def, atk, sleep;
    int32_t exp;
    int32_t gold;
    uint8_t dropItem, dropProb;
    uint16_t status;
    int16_t unused2a;
    char name[21];
    uint8_t rate[100];
    uint8_t pattern[10];
    int8_t actorId;
    int32_t z0, z1;
};
#pragma pack(pop)
static_assert(sizeof(Battler) == 0xB8, "battler record size");

inline sjis battlerName(const Battler& b) {
    size_t n = 0;
    while (n < 21 && b.name[n]) n++;
    return sjis(b.name, n);
}
void setBattlerName(Battler& b, const sjis& s);

struct Item {
    int16_t price = 0, type = 0, power = 0, mask = 0;
    sjis name;
};

struct Magic {
    int16_t type = 0, anim = 0, cost = 0, power = 0;
    sjis name;
    int animNo = 0;  // derived animation number (08-battle.md §5.4)
};

struct Troop {
    int16_t bg = 0, terrain = 0;
    int32_t count = 0;
    struct { int32_t id, x, y; } m[10];
};

struct GameData {
    sjis header, versionText;
    AudioRec slots[12];
    int32_t trans[3] = {0, 0, 0};
    sjis coin;
    int32_t itemMax = 0;
    uint32_t flags = 0;
    Actor actors[8];
    int16_t startItems[100] = {0};
    int32_t startMoney = 0, startMap = 0, startX = 0, startY = 0;
    std::vector<MapRec> maps;  // 500
    bool truncated = false;    // diagnostics: the file ended inside a record or stream
    int badMapIds = 0;         // diagnostics: map records with an id outside 0..499 (ignored)
    int messageSpeed() const { return (int)(flags & 0xFFFF); }
    bool transparentWindows() const { return (flags >> 16) & 1; }
    bool savingEnabled() const { return (flags >> 17) & 1; }
};

struct Cmd {
    int32_t code, a1, a2, a3, a4;
};

// Page attributes as stored in the event header at 0x0C..0x63 (88 bytes).
struct PageAttr {
    uint8_t raw[0x58];
    int trigger(int p) const { return raw[p]; }
    uint32_t cond(int p) const { return rd32(raw + 4 + 4 * p); }
    int display(int p) const { return raw[0x14 + p]; }
    int layer(int p) const { return raw[0x18 + p]; }
    int moveType(int p) const { return raw[0x1C + p]; }
    int animType(int p) const { return raw[0x20 + p]; }
    int speed(int p) const { return raw[0x24 + p]; }
    int vehicle(int p) const { return raw[0x28 + p]; }
    int condItem(int p) const { return rd16(raw + 0x2C + 2 * p); }
    int condParty(int p) const { return raw[0x34 + p]; }
    uint32_t condMoney(int p) const { return rd32(raw + 0x38 + 4 * p); }
    int condSwitch(int p) const { return rd16(raw + 0x48 + 2 * p); }
    int graphic(int p) const { return rd16(raw + 0x50 + 2 * p); }
};

struct EventHeader {
    PageAttr attr;
    std::vector<uint8_t> routes[4];
};

struct EventScript {
    EventHeader hdr;
    std::vector<Cmd> cmds[4];
    std::vector<std::vector<uint8_t>> routes;  // command move routes
};

struct MonsterPic {
    int w = 0, h = 0;
    std::vector<uint8_t> idx;  // top row first
    std::vector<uint8_t> tableRaw;  // 1024 bytes BGRx
    bool ok = false;
};

namespace data {

extern GameData gd;
extern sjis strings[150];
extern Item items[100];
extern Magic magic[100];
extern Battler monsters[100];
extern Troop troops[250];
extern uint8_t atr[1024];
extern std::vector<uint32_t> monstpicOffsets;
extern sjis rpgName;

// One "line read" (02-data-formats.md §2.5). Returns false when nothing could be
// read because the file was at its end; dst then keeps its previous content minus
// its last byte (the "remove the last byte" step is applied to the old content, §5).
bool lineRead(ByteReader& r, sjis& dst);

bool loadGameFile(const sjis& name);
void loadStrings();
bool loadEParty();
bool scanMonstPic();
void loadDatabases();  // CHARA.DAT, ITEM.DAT, MAGIC.DAT
void loadAtr(const sjis chip[4]);
bool loadMonsterPic(int id, MonsterPic& out);  // false = file missing

// Event files
std::string eventFileName(int id);
bool readEventHeader(int id, EventHeader& out);
// Full run read. Uses the persistent global string slots (quirk of §4.4).
bool readEventScript(int id, EventScript& out);
// String slots of the event being run
const sjis& eventString(int idx);
void clearEventStrings();

}  // namespace data
