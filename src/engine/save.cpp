// Save files (10-save-files.md) and the in-game save/load file chooser.
#include <algorithm>
#include <cstdio>
#include "engine/display.h"
#include "core/fsys.h"
#include "engine/game.h"
#include "platform/platform.h"
#include "engine/runtime.h"
#include "core/sjis.h"

namespace save {

static const uint8_t kIdent[9] = {0x52, 0x50, 0x47, 0xC2, 0xB8, 0xB0, 0xD9, 0x39, 0x35};
static std::string captionU8() { return sj::toUtf8(sj::S("RPGﾂｸｰﾙ95")); }

// ------------------------------------------------------------------ writing
static void writeCharState(ByteWriter& w, const Chara& c) {
    int v[16] = {c.facingOverride, c.facing, c.moveType, c.motion, c.graphic, c.unused, c.substep, c.animTimer,
                 c.animFrame, c.layer, c.speed, c.speedCounter, c.vehicleType, c.animType, c.display, c.cooldown};
    for (int i = 0; i < 16; i++) w.u32((uint32_t)v[i]);
}

static void writePartyChar(ByteWriter& w, const Chara& c) {
    w.u8(0x50); w.u8(0x50); w.u8(0x46); w.u8(0x00);
    w.u32(c.x); w.u32(c.y); w.u32(c.destX); w.u32(c.destY); w.u32(c.onScreen);
    w.u16(0);
    writeCharState(w, c);
    w.u32((uint32_t)c.route.size()); w.u32(c.routePos); w.u32(c.routeMode); w.u32(c.routeStep);
    w.u32(0);
}

static void writeVehicle(ByteWriter& w, const Chara& c) {
    w.u32(c.x); w.u32(c.y); w.u32(c.destX); w.u32(c.destY); w.u32(c.onScreen);
    w.u16(c.eventId);
    writeCharState(w, c);
    bool hasRoute = c.hasRoute();
    w.u32(hasRoute ? (uint32_t)c.route.size() : 0); w.u32(c.routePos); w.u32(c.routeMode); w.u32(c.routeStep);
    w.u32(hasRoute ? 1 : 0);
    if (hasRoute) w.bytes(c.route.data(), c.route.size());
    w.u32(c.resumeIndex);
    w.u8((uint8_t)(int8_t)c.page);
    w.bytes(c.attr.raw, 0x58);
    for (int p = 0; p < 4; p++) {
        size_t n = std::min<size_t>(255, c.pageRoutes[p].size());
        w.u8((uint8_t)n);
        w.bytes(c.pageRoutes[p].data(), n);
    }
}

static void writeAudio(ByteWriter& w, bool has, const sjis& name, int32_t a, int32_t b) {
    w.u32(0); w.u32(a); w.u32(b);
    sjis n = has ? name : sjis("(null)");
    w.bytes(n.data(), n.size());
    w.u8(0x0A);
}

// A vehicle entry gets an object in part 2 exactly when it holds an object and has a
// non-zero event id, so that the writer's test (object marker) and the reader's test
// (event id) agree (10 §5.2).
static bool vehicleHasObject(int k) { return G.vehicles[k].obj && G.vehicles[k].id != 0; }

// Serialise the current state exactly as in 10 §1.
void buildSave(std::vector<uint8_t>& out) {
    ByteWriter w;
    // part 1
    w.bytes(kIdent, 9);
    w.zeros(21);
    w.bytes("1.23", 4); w.u8(0);
    w.u32((uint32_t)G.curMap);
    for (int k = 0; k < 10; k++) {
        w.u16(G.vehicles[k].id);
        w.u32((uint32_t)G.vehicles[k].map);
        w.u32(vehicleHasObject(k) ? 1 : 0);
    }
    w.u32(G.riding ? 1 : 0);
    // part 2
    for (int k = 0; k < 10; k++) if (vehicleHasObject(k)) writeVehicle(w, *G.vehicles[k].obj);
    // part 3
    w.u32((uint32_t)G.shown);
    w.u32((uint32_t)G.partySize);
    for (int i = 0; i < 8; i++) w.u8((uint8_t)(int8_t)G.partyActor[i]);
    for (int a = 0; a < 8; a++) {
        static const Chara none{};
        writePartyChar(w, G.actorChar[a] ? *G.actorChar[a] : none);
    }
    size_t blk = w.buf.size();
    w.zeros(0x1140);
    uint8_t* B = w.buf.data() + blk;
    wr32(B + 0x00C, (uint32_t)G.fightSize());
    memcpy(B + 0x030, G.party, 8 * 0xB8);
    memcpy(B + 0x5F4, G.equip, 40);
    memcpy(B + 0x61C, G.learn, 800);
    wr32(B + 0x968, (uint32_t)G.money);
    for (int i = 0; i < 100; i++) w.u16((uint16_t)G.itemCount[i]);
    w.u32((uint32_t)G.money);
    w.bytes(G.switches, 128);
    // part 4
    for (int s = 0; s < 12; s++) writeAudio(w, G.slots[s].hasName, G.slots[s].name, G.slots[s].a, G.slots[s].b);
    writeAudio(w, audio::cur.has, audio::cur.name, audio::cur.a, audio::cur.b);
    // part 5
    for (int i = 0; i < 3; i++) w.u32((uint32_t)G.trans[i]);
    w.bytes(G.tint, 4);
    const Palette& P = disp::haveRememberedMapPal ? disp::rememberedMapPal : disp::committed;
    // palette entry flags: 4 (as in every corpus file; meaningless for loading)
    for (int i = 0; i < 256; i++) { w.u8(P.c[i].r); w.u8(P.c[i].g); w.u8(P.c[i].b); w.u8(4); }
    for (int i = 0; i < 100; i++) w.u16((uint16_t)G.teleportList[i]);
    w.u16((uint16_t)G.teleportForbidden);
    w.u16((uint16_t)G.escapeMap);
    w.u32((uint32_t)G.moveControl);
    out.swap(w.buf);
}

bool writeSave(const sjis& fname) {
    std::vector<uint8_t> buf;
    buildSave(buf);
    return fsys::write(fname, buf);
}

// ------------------------------------------------------------------ reading
static bool readTextLine(ByteReader& r, sjis& out) {
    std::string b;
    while (r.left() > 0 && b.size() < 255) {
        char c = (char)r.u8();
        b += c;
        if (c == '\n') break;
    }
    if (b.empty()) return false;
    b.pop_back();
    out = b;
    return true;
}

static void readCharState(ByteReader& r, Chara& c) {
    int v[16];
    for (int i = 0; i < 16; i++) v[i] = r.s32();
    c.facingOverride = v[0]; c.facing = v[1]; c.moveType = v[2]; c.motion = v[3]; c.graphic = v[4]; c.unused = v[5];
    c.substep = v[6]; c.animTimer = v[7]; c.animFrame = v[8]; c.layer = v[9]; c.speed = v[10]; c.speedCounter = v[11];
    c.vehicleType = v[12]; c.animType = v[13]; c.display = v[14]; c.cooldown = v[15];
}

int readSave(const sjis& fname) {
    std::vector<uint8_t> f;
    if (!fsys::read(fname, f)) return 1;
    return readSaveData(f);
}

int readSaveData(const std::vector<uint8_t>& f) {
    ByteReader r(f);
    uint8_t id[30];
    r.bytes(id, 30);
    id[29] = 0;
    if (memcmp(id, kIdent, 9) != 0 || id[9] != 0) return 2;
    r.skip(5);
    // parse into temporaries first (a truncated file is rejected as a whole)
    int32_t curMap = r.s32();
    struct V { uint16_t id; int32_t map; uint32_t marker; } vt[10];
    for (int k = 0; k < 10; k++) { vt[k].id = r.u16(); vt[k].map = r.s32(); vt[k].marker = r.u32(); }
    bool riding = r.s32() != 0;
    std::unique_ptr<Chara> vobj[10];
    for (int k = 0; k < 10; k++) {
        if (vt[k].id == 0) continue;
        std::unique_ptr<Chara> c(new Chara());
        c->x = r.s32(); c->y = r.s32(); c->destX = r.s32(); c->destY = r.s32(); c->onScreen = r.s32();
        c->eventId = r.u16();
        readCharState(r, *c);
        int len = r.s32(); c->routePos = r.s32(); c->routeMode = r.s32(); c->routeStep = r.s32();
        uint32_t marker = r.u32();
        if (marker) {
            // the route bytes are present only when the route marker is non-zero
            if (len < 0 || (size_t)len > r.left()) { r.fail = true; break; }
            c->route.assign(f.begin() + r.pos, f.begin() + r.pos + len);
            r.pos += len;
        } else if (c->routeMode != ROUTE_NONE) {
            // a route mode without route bytes cannot run
            c->routeMode = ROUTE_NONE; c->routePos = 0; c->routeStep = 1;
        }
        c->resumeIndex = r.s32();
        c->page = (int8_t)r.u8();
        r.bytes(c->attr.raw, 0x58);
        for (int p = 0; p < 4; p++) {
            int n = r.u8();
            if ((size_t)n > r.left()) { r.fail = true; break; }
            c->pageRoutes[p].assign(f.begin() + r.pos, f.begin() + r.pos + n);
            r.pos += n;
        }
        vobj[k] = std::move(c);
    }
    int shown = r.s32();
    int psize = r.s32();
    int8_t pa[8];
    r.bytes(pa, 8);
    Chara pc[8];
    for (int a = 0; a < 8; a++) {
        r.skip(4);
        pc[a].x = r.s32(); pc[a].y = r.s32(); pc[a].destX = r.s32(); pc[a].destY = r.s32(); pc[a].onScreen = r.s32();
        pc[a].eventId = r.u16();
        readCharState(r, pc[a]);
        r.s32(); pc[a].routePos = r.s32(); pc[a].routeMode = r.s32(); pc[a].routeStep = r.s32();
        r.u32();  // route marker: meaningless in these records
        // party characters have no move route after loading (10 §2.2 step 5, §2.3)
        pc[a].route.clear();
        if (pc[a].routeMode != ROUTE_NONE) { pc[a].routeMode = ROUTE_NONE; pc[a].routePos = 0; pc[a].routeStep = 1; }
    }
    std::vector<uint8_t> blk(0x1140);
    r.bytes(blk.data(), 0x1140);
    int16_t items[100];
    for (int i = 0; i < 100; i++) items[i] = r.s16v();
    int32_t money = r.s32();
    uint8_t sw[128];
    r.bytes(sw, 128);
    AudioRec slots[12];
    BgmRec curBgm;
    for (int s = 0; s < 13; s++) {
        r.skip(4);
        int32_t a = r.s32(), b = r.s32();
        sjis n;
        if (!readTextLine(r, n)) r.fail = true;
        if (s < 12) { slots[s].a = a; slots[s].b = b; slots[s].name = n; slots[s].hasName = true; }
        else { curBgm.a = a; curBgm.b = b; curBgm.name = n; curBgm.has = true; }
    }
    int32_t trans[3];
    for (int i = 0; i < 3; i++) trans[i] = r.s32();
    uint8_t tint[4];
    r.bytes(tint, 4);
    Palette pal;
    for (int i = 0; i < 256; i++) { pal.c[i].r = r.u8(); pal.c[i].g = r.u8(); pal.c[i].b = r.u8(); r.u8(); }
    int16_t tl[100];
    for (int i = 0; i < 100; i++) tl[i] = r.s16v();
    int16_t lock = r.s16v(), esc = r.s16v();
    int32_t mc = r.s32();
    if (r.fail || psize < 0 || psize > 8) return 2;

    // ---- apply (10 §2.2 step 3 onwards)
    disp::fadeScreenToBlack(5);
    mapx::clearGrid();        // no cell may refer to the objects deleted now
    mapx::deleteMapEvents();
    for (int k = 0; k < 10; k++) { G.vehicles[k].obj.reset(); G.vehicles[k].id = 0; G.vehicles[k].map = 0; }
    G.curMap = curMap;
    for (int k = 0; k < 10; k++) {
        if (vt[k].id == 0) continue;
        G.vehicles[k].id = vt[k].id;
        G.vehicles[k].map = vt[k].map;
        G.vehicles[k].obj = std::move(vobj[k]);
    }
    G.riding = riding;
    G.partySize = psize;
    for (int i = 0; i < 8; i++) G.partyActor[i] = (pa[i] >= 0 && pa[i] < 8) ? pa[i] : 0;
    for (int a = 0; a < 8; a++) { G.actorChar[a].reset(new Chara(pc[a])); }
    memcpy(G.party, blk.data() + 0x030, 8 * 0xB8);
    memcpy(G.equip, blk.data() + 0x5F4, 40);
    memcpy(G.learn, blk.data() + 0x61C, 800);
    memcpy(G.itemCount, items, sizeof items);
    G.money = money;
    memcpy(G.switches, sw, 128);
    for (int s = 0; s < 12; s++) G.slots[s] = slots[s];
    audio::cur = curBgm;
    for (int i = 0; i < 3; i++) G.trans[i] = trans[i];
    memcpy(G.tint, tint, 4);
    for (int i = 0; i < 256; i++) disp::committed.c[i] = pal.c[i];
    memcpy(G.teleportList, tl, sizeof tl);
    G.teleportForbidden = lock;
    G.escapeMap = esc;
    G.moveControl = mc;
    // fix-ups (10 §2.3)
    G.heroWait.clear();
    G.postPass = POST_NONE;
    G.partyMask = 0;
    for (int i = 0; i < G.partySize; i++) G.partyMask |= (uint8_t)(1 << G.partyActor[i]);
    for (int i = 0; i < 8; i++) G.partyChar[i] = i < G.partySize ? G.actorChar[G.partyActor[i]].get() : nullptr;
    if (G.partySize == 0) G.partyChar[0] = G.actorChar[0].get();
    if (G.riding && G.vehicles[0].obj) G.partyChar[0] = G.vehicles[0].obj.get();
    G.shown = std::max(1, shown);
    if (!G.riding) G.shown = std::max(1, G.partySize);
    G.passHero = G.partyChar[0];
    for (int k = 0; k < 8; k++)
        for (int j = 0; j < 100; j++) {
            uint8_t d = data::gd.actors[k].learn[j];
            if (!(G.learn[k][j] == 1 && d == 0)) G.learn[k][j] = d;
        }
    disp::haveRememberedMapPal = false;
    return 0;
}

// ------------------------------------------------------------------ chooser
// A list in window slot 3 (unused by the original). The program behaves as during
// an event while it is shown.
static int chooseFromList(const std::vector<sjis>& rows, unsigned disableMask, int width) {
    int n = (int)rows.size();
    sjis text;
    for (int i = 0; i < n; i++) text += rows[i] + (i + 1 < n ? "\n" : "");
    ui::listHelper(-1, 60, width, n, text, 0, ui::LEFT_WRAP, 3, disableMask, false);
    // start on the first selectable row (row 0 is the disabled heading)
    if (ui::Window* w = ui::get(3)) {
        int first = 0;
        while (first < n - 1 && (disableMask & (1u << first))) first++;
        ui::moveCursor(w, first);
    }
    rt::wait(0);
    int sel = ui::selectedIndex;
    ui::destroy(3);
    rt::flushKeysWait();
    return sel;
}

static std::vector<sjis> listSaves() {
    std::vector<sjis> v = fsys::listWithExt(".SAV");
    std::sort(v.begin(), v.end());
    return v;
}

static sjis newSaveName() {
    for (int i = 1; i < 1000; i++) {
        sjis n = fmt("save%02d.sav", i);
        if (!fsys::exists(n)) return n;
    }
    return "save.sav";
}

static int pickFile(bool saving, sjis& out) {
    std::vector<sjis> files = listSaves();
    std::vector<sjis> entries;
    if (saving) entries.push_back(sj::S("（新しいファイル）"));
    for (auto& f : files) entries.push_back(f);
    if (entries.empty()) { rt::popup(-1, sj::S("ファイルがありません")); return -1; }
    const int perPage = 12;
    int page = 0;
    int pages = ((int)entries.size() + perPage - 1) / perPage;
    for (;;) {
        std::vector<sjis> rows;
        rows.push_back(sj::S(saving ? "【ゲームを保存する】" : "【ゲームを読み込む】"));
        int first = page * perPage;
        int cnt = std::min(perPage, (int)entries.size() - first);
        for (int i = 0; i < cnt; i++) rows.push_back(entries[first + i]);
        bool more = pages > 1;
        if (more) rows.push_back(sj::S("次のページ ▼"));
        int sel = chooseFromList(rows, 1u, 240);
        if (G.quit || sel < 0) return -1;
        if (more && sel == (int)rows.size() - 1) { page = (page + 1) % pages; continue; }
        if (sel == 0) continue;
        int idx = first + sel - 1;
        if (saving && idx == 0) { out = newSaveName(); return 1; }
        out = entries[idx];
        return 0;
    }
}

bool saveDialog() {
    bool prev = G.eventRunning;
    G.eventRunning = true;
    for (;;) {
        sjis name;
        int r = pickFile(true, name);
        if (r < 0) break;
        if (r == 0) {
            std::vector<sjis> q = {sj::S(fmt("%s を上書きしますか？", sj::toUtf8(name).c_str()).c_str()), sj::S("はい"), sj::S("いいえ")};
            int s = chooseFromList(q, 1u, 240);
            if (s != 1) continue;
        }
        if (!writeSave(name)) {
            plat::messageBox(sj::toUtf8(sj::S("データを書き込むことができません")), captionU8(), plat::MB_NONE, false);
            continue;
        }
        break;
    }
    G.eventRunning = prev;
    return true;
}

bool loadDialog() {
    bool prev = G.eventRunning;
    G.eventRunning = true;
    bool loaded = false;
    sjis name;
    if (pickFile(false, name) >= 0) {
        int r = readSave(name);
        if (r == 1) plat::messageBox(sj::toUtf8(sj::S("ファイルを開くことができません")), captionU8(), plat::MB_NONE, false);
        else if (r == 2) plat::messageBox(sj::toUtf8(sj::S("現バージョンでこのデータを\n読み込むことはできません")), captionU8(), plat::MB_WARNING, false);
        else loaded = true;
    }
    G.eventRunning = prev;
    return loaded;
}

void afterLoadFromMenu() {
    disp::fadeScreenToBlack(5);
    disp::reloadMapPaletteWithTint(false);
    audio::stopAllMusic();
    G.passAbort = true;
    int M = G.curMap;
    G.curMap = -2;
    Chara* h = hero();
    int x = h->x, y = h->y;
    BgmRec m = audio::cur;  // copy: playBGM updates audio::cur
    int32_t ab[2] = {m.a, m.b};
    audio::playBGM(m.name, ab, true);
    mapx::teleport(M, x, y);
}

}  // namespace save
