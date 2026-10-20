#include "engine/data.h"
#include <cstdio>
#include <cstdlib>
#include "core/fsys.h"
#include "core/sjis.h"

void setBattlerName(Battler& b, const sjis& s) {
    memset(b.name, 0, sizeof b.name);
    memcpy(b.name, s.data(), std::min(s.size(), (size_t)20));
}

namespace data {

GameData gd;
sjis strings[150];
Item items[100];
Magic magic[100];
Battler monsters[100];
Troop troops[250];
uint8_t atr[1024];
std::vector<uint32_t> monstpicOffsets;
sjis rpgName;

static const char* kDefaultStrings[150] = {
    "アイテム", "魔法", "装備", "ステータス", "並び替え", "戦う", "逃げる", "自動戦闘", "未使用", "未使用",
    "武器", "鎧", "盾", "兜", "その他", "未使用", "未使用", "未使用", "未使用", "未使用",
    "正常", "毒", "麻痺", "沈黙", "混乱", "眠る", "気絶", "未使用", "未使用", "未使用",
    "は毒に冒された", "はすでに毒に冒されている", "の体から毒が消えた", "は麻痺した", "はすでに麻痺している",
    "は麻痺から回復した", "は沈黙した", "はすでに沈黙している", "の沈黙が解けた", "は混乱した",
    "はすでに混乱している", "は正気を取り戻した", "は眠らされた", "はすでに眠っている", "は目を醒ました",
    "は気絶した", "は復帰した", "失敗！", "は気絶している", "は麻痺していて動けない",
    "は眠っている", "は混乱している", "未使用", "未使用", "未使用", "未使用", "未使用", "未使用", "未使用", "未使用",
    "の呪文を唱えた", "しかし魔法は封じられている", "ＭＰが足りない！", "しかし不思議な力で呪文はかき消された！",
    "しかし何も起こらなかった", "しかし効果はなかった", "には効果がなかった", "には意味がなかった", "未使用", "未使用",
    "モンスターがあらわれた！", "会心の一撃！", "痛恨の一撃！", "はヒラリと身をかわした", "は素早くよけた",
    "は敵の攻撃を見切った", "は完全に防御した", "は攻撃を撥ね返した", "はダメージを受けない", "未使用",
    "はヒラリと身をかわした", "は素早くよけた", "はすばしっこく逃げ回る", "には歯が立たない！", "には全く効いていない！",
    "にダメージを与えられない！", "ﾎﾟｲﾝﾄのﾀﾞﾒｰｼﾞを受けた", "ﾎﾟｲﾝﾄのﾀﾞﾒｰｼﾞを与えた", "をやっつけた！", "未使用",
    "アイテムを持っていません", "装備できるアイテムを持っていません", "戦闘中に使用できるアイテムを持っていません",
    "戦闘時しか使用できません", "魔法を覚えていません", "フィールド上の魔法を覚えていません", "ここでは使えません",
    "移動先がありません", "未使用", "未使用",
    " 購入する ", " 売却する ", "やめる", "何をお買い上げになりますか？", "何をお売りになりますか？",
    "ほかにもご用がおありでしょうか", "売るアイテムはありません", "そのアイテムはもう持てません", "お金が足りません",
    "をお買い上げですね。",
    "そのｱｲﾃﾑを売ることはできません", "ですね。", " 泊まる ", " 泊まらない ", "気の毒だけどお金が足りないようだね。",
    "未使用", "未使用", "未使用", "未使用", "未使用",
    "を装備した", "を使った", "を使おうと思ったが、どこにも見当たらなかった", "は逃げ出した！", "逃げられない！",
    "しかし敵に回り込まれた！", "連続攻撃！", "は様子を見ている", "は逃げ出した", "モンスターはなにか落としていった",
    "を手に入れた！", "Go Go Go!", "は毒に冒されている", "の魔法を覚えた", "未使用", "未使用", "未使用", "未使用",
    "未使用", "未使用", "未使用", "未使用", "未使用", "未使用", "未使用", "未使用", "未使用", "未使用", "未使用", "未使用",
};

// ------------------------------------------------------------------ line read
bool lineRead(ByteReader& r, sjis& dst) {
    if (r.left() == 0) {
        if (!dst.empty()) dst.pop_back();
        return false;
    }
    std::string bytes;
    while (r.left() > 0 && bytes.size() < 255) {
        char c = (char)r.u8();
        bytes += c;
        if (c == '\n') break;
    }
    size_t nul = bytes.find('\0');
    if (nul != std::string::npos) bytes.resize(nul);
    if (!bytes.empty()) bytes.pop_back();
    dst = bytes;
    return true;
}

static sjis cstrN(const uint8_t* p, size_t n) {
    size_t k = 0;
    while (k < n && p[k]) k++;
    return sjis((const char*)p, k);
}

// C-like decimal conversion of the leading part of the version text (§3.2):
// leading white space, optional sign, digits, optional point and digits.
// Computed as mantissa / 10^k so that "0.96" gives exactly the double 0.96.
static double versionValue(const sjis& t) {
    size_t i = 0;
    while (i < t.size() && (t[i] == ' ' || (t[i] >= '\t' && t[i] <= '\r'))) i++;
    bool neg = false;
    if (i < t.size() && (t[i] == '+' || t[i] == '-')) { neg = t[i] == '-'; i++; }
    double mant = 0, scale = 1;
    while (i < t.size() && t[i] >= '0' && t[i] <= '9') { mant = mant * 10 + (t[i] - '0'); i++; }
    if (i < t.size() && t[i] == '.') {
        i++;
        while (i < t.size() && t[i] >= '0' && t[i] <= '9') { mant = mant * 10 + (t[i] - '0'); scale *= 10; i++; }
    }
    double v = mant / scale;
    return neg ? -v : v;
}

// ------------------------------------------------------------------ game file
bool loadGameFile(const sjis& name) {
    std::vector<uint8_t> f;
    if (!fsys::read(name, f)) return false;
    rpgName = name;
    gd = GameData();
    gd.maps.assign(500, MapRec());
    ByteReader r(f);
    uint8_t hdr[30] = {0}, ver[5] = {0};
    r.bytes(hdr, 30);
    r.bytes(ver, 5);
    gd.header = cstrN(hdr, 30);
    // version text (quirk: continues into header bytes 0x05..0x1C without a NUL)
    sjis vt;
    bool nul = false;
    for (int i = 0; i < 5; i++) { if (!ver[i]) { nul = true; break; } vt += (char)ver[i]; }
    if (!nul) for (int i = 5; i <= 0x1C; i++) { if (!hdr[i]) break; vt += (char)hdr[i]; }
    gd.versionText = vt;
    int nslots = versionValue(vt) < 0.96 ? 11 : 12;
    for (int s = 0; s < nslots; s++) {
        if (r.left() == 0) break;
        gd.slots[s].a = r.s32();
        gd.slots[s].b = r.s32();
        sjis nm;
        if (lineRead(r, nm)) { gd.slots[s].name = nm; gd.slots[s].hasName = true; }
    }
    for (int i = 0; i < 3; i++) gd.trans[i] = r.s32();
    uint8_t coin[21];
    r.bytes(coin, 21);
    gd.coin = cstrN(coin, 21);
    gd.itemMax = r.s32();
    gd.flags = r.u32();
    for (int a = 0; a < 8; a++) {
        uint8_t rec[0x3A5];
        r.bytes(rec, sizeof rec);
        Actor& A = gd.actors[a];
        A.graphic = rds32(rec + 0);
        A.startMember = rds32(rec + 4);
        A.name = cstrN(rec + 8, 20);  // an 18-byte name has its NUL in the following field
        A.level = rds32(rec + 0x1C); A.hp = rds32(rec + 0x20); A.mp = rds32(rec + 0x24);
        A.vit = rds32(rec + 0x28); A.pow = rds32(rec + 0x2C); A.intl = rds32(rec + 0x30);
        A.agi = rds32(rec + 0x34); A.luck = rds32(rec + 0x38); A.atk = rds32(rec + 0x3C); A.def = rds32(rec + 0x40);
        memcpy(A.curvePow, rec + 0x44, 100);
        memcpy(A.curveVit, rec + 0xA8, 100);
        memcpy(A.curveInt, rec + 0x10C, 100);
        memcpy(A.curveAgi, rec + 0x170, 100);
        memcpy(A.curveLuck, rec + 0x1D4, 100);
        memcpy(A.equip, rec + 0x2D8, 5);
        memcpy(A.learn, rec + 0x2DD, 100);
        memcpy(A.rates, rec + 0x341, 100);
    }
    for (int i = 0; i < 100; i++) gd.startItems[i] = r.s16v();
    gd.startMoney = r.s32();
    gd.startMap = r.s32();
    gd.startX = r.s32();
    gd.startY = r.s32();
    // map records
    uint16_t lastValue = 0;
    while (r.left() >= 4) {
        int32_t id = r.s32();
        uint8_t rec[0x51];
        r.bytes(rec, 0x51);
        MapRec tmp;
        bool valid = id >= 0 && id < 500;
        MapRec& m = valid ? gd.maps[id] : tmp;
        m = MapRec();
        m.name = cstrN(rec, 0x51);
        m.type = rds32(rec + 0x19);
        m.areaX = rds32(rec + 0x1D);
        m.areaY = rds32(rec + 0x21);
        m.w = rd16(rec + 0x25);
        m.h = rd16(rec + 0x27);
        m.parent = rds32(rec + 0x29);
        m.sibling = rd16(rec + 0x2D);
        m.child = rd16(rec + 0x2F);
        m.exitX = rds32(rec + 0x35);
        m.exitY = rds32(rec + 0x39);
        for (int k = 0; k < 10; k++) m.enc[k] = rd16(rec + 0x3D + 2 * k);
        if (!valid) { fprintf(stderr, "warning: map id %d out of range\n", id); gd.badMapIds++; }
        if (m.type != 2) {
            for (int k = 0; k < 4; k++) lineRead(r, m.chip[k]);
            m.hasChip = true;
            size_t cells = (size_t)m.w * m.h;
            m.tiles.assign(cells, 0);
            m.events.assign(cells, 0);
            // Tiles (§3.8 step 2). At the end of the file the original keeps reusing the
            // last value read; a run past the last cell is ignored.
            size_t pos = 0;
            while (pos < cells) {
                if (r.left() >= 2) lastValue = r.u16(); else gd.truncated = true;
                uint16_t v = lastValue;
                int rep = (v >> 10) + 1;
                for (int k = 0; k < rep && pos < cells; k++) m.tiles[pos++] = v & 0x3FF;
            }
            // Event placement (§3.8 step 3), read until the cell position reaches or passes W*H.
            pos = 0;
            while (pos < cells) {
                if (r.left() >= 2) lastValue = r.u16(); else gd.truncated = true;
                uint16_t v = lastValue;
                if (v & 0x8000) pos += (size_t)(v & 0x7FFF) + 1;
                else { m.events[pos] = v; pos++; }
            }
        }
    }
    if (r.fail) gd.truncated = true;
    return true;
}

// ------------------------------------------------------------------ strings.dat
void loadStrings() {
    std::vector<uint8_t> f;
    if (!fsys::read("strings.dat", f)) {
        for (int i = 0; i < 150; i++) strings[i] = sj::S(kDefaultStrings[i]);
        return;
    }
    if (f.empty()) {
        for (int i = 0; i < 150; i++) strings[i].clear();
        return;
    }
    ByteReader r(f);
    sjis buf;
    for (int i = 0; i < 150; i++) {
        lineRead(r, buf);
        strings[i] = buf;
    }
}

// ------------------------------------------------------------------ databases
// Copy record i (size sz) of a database file into rec; a record cut off by the end
// of the file keeps its available bytes, the rest is zero (§6: "use zeros").
static bool dbRecord(const std::vector<uint8_t>& f, int i, size_t sz, uint8_t* rec) {
    size_t o = (size_t)i * sz;
    memset(rec, 0, sz);
    if (o >= f.size()) return false;
    memcpy(rec, f.data() + o, std::min(sz, f.size() - o));
    return true;
}

bool loadEParty() {
    std::vector<uint8_t> f;
    if (!fsys::read("EPARTY.DAT", f)) return false;
    uint8_t rec[128];
    for (int t = 0; t < 250; t++) {
        Troop& T = troops[t];
        T = Troop();
        memset(T.m, 0, sizeof T.m);
        if (!dbRecord(f, t, 128, rec)) continue;
        const uint8_t* p = rec;
        T.bg = rds16(p);
        T.terrain = rds16(p + 2);
        T.count = rds32(p + 4);
        for (int k = 0; k < 10; k++) {
            T.m[k].id = rds32(p + 8 + 12 * k);
            T.m[k].x = rds32(p + 12 + 12 * k);
            T.m[k].y = rds32(p + 16 + 12 * k);
        }
    }
    return true;
}

bool scanMonstPic() {
    std::vector<uint8_t> f;
    if (!fsys::read("MONSTPIC.DAT", f)) return false;
    monstpicOffsets.clear();
    uint64_t off = 0;
    monstpicOffsets.push_back(0);
    while (off + 2 <= f.size()) {
        uint32_t w = rd16(f.data() + off);
        uint32_t h = off + 4 <= f.size() ? rd16(f.data() + off + 2) : 0;
        off += (uint64_t)w * h + 0x404;
        monstpicOffsets.push_back((uint32_t)std::min<uint64_t>(off, 0xFFFFFFFFu));
    }
    // the last offset points past the end (no record there)
    return true;
}

bool loadMonsterPic(int id, MonsterPic& out) {
    out = MonsterPic();
    std::vector<uint8_t> f;
    if (!fsys::read("MONSTPIC.DAT", f)) return false;
    if (id < 0 || id + 1 >= (int)monstpicOffsets.size()) return true;  // no picture
    size_t off = monstpicOffsets[id];
    if (off + 4 > f.size()) return true;
    int w = rd16(f.data() + off), h = rd16(f.data() + off + 2);
    if (w <= 0 || h <= 0) return true;
    out.w = w; out.h = h;
    out.idx.assign((size_t)w * h, 0);
    const uint8_t* p = f.data() + off + 4;
    size_t avail = f.size() - off - 4;
    // The w*h pixel bytes are laid into bottom-up rows padded to a multiple of 4 bytes
    // (§6.6): for widths that are not multiples of 4 this shears the picture (quirk);
    // bytes beyond the w*h stored ones are undefined in the original (0 here).
    size_t stride = ((size_t)w + 3) & ~(size_t)3;
    size_t npix = (size_t)w * h;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t q = (size_t)y * stride + x;
            out.idx[(size_t)(h - 1 - y) * w + x] = (q < npix && q < avail) ? p[q] : 0;
        }
    }
    out.tableRaw.assign(1024, 0);
    size_t tp = (size_t)w * h;
    for (size_t i = 0; i < 1024; i++) if (tp + i < avail) out.tableRaw[i] = p[tp + i];
    out.ok = true;
    return true;
}

void loadDatabases() {
    std::vector<uint8_t> f;
    memset(monsters, 0, sizeof monsters);
    if (fsys::read("CHARA.DAT", f)) {
        for (int i = 0; i < 100; i++) dbRecord(f, i, 0xB8, (uint8_t*)&monsters[i]);
    }
    for (auto& it : items) it = Item();
    for (auto& m : magic) m = Magic();
    uint8_t p[28];
    if (fsys::read("ITEM.DAT", f)) {
        for (int i = 0; i < 100; i++) {
            if (!dbRecord(f, i, 28, p)) break;
            items[i].price = rds16(p); items[i].type = rds16(p + 2); items[i].power = rds16(p + 4); items[i].mask = rds16(p + 6);
            items[i].name = cstrN(p + 8, 20);
        }
        // MAGIC.DAT is read only when ITEM.DAT could be read (§1, §6.3)
        if (fsys::read("MAGIC.DAT", f)) {
            for (int i = 0; i < 100; i++) {
                if (!dbRecord(f, i, 28, p)) break;
                magic[i].type = rds16(p); magic[i].anim = rds16(p + 2); magic[i].cost = rds16(p + 4); magic[i].power = rds16(p + 6);
                magic[i].name = cstrN(p + 8, 20);
            }
        }
    }
    // animation numbers (08-battle.md §5.4)
    int c0 = 0, c1 = 24, c11 = 20, c12 = 44;
    for (int s = 0; s < 100; s++) {
        unsigned t = (uint16_t)magic[s].type;
        int A;
        if (t == 0) { A = c0; c0 = c0 + 1 > 19 ? 0 : c0 + 1; }
        else if (t == 1) { A = c1; c1 = c1 + 1 > 47 ? 24 : c1 + 1; }
        else if (t == 11) { A = c11; c11 = c11 + 1 > 23 ? 20 : c11 + 1; }
        else if (t == 12) { A = c12; c12 = c12 + 1 > 48 ? 44 : c12 + 1; }
        else if (t < 11) A = (int)t + 46;
        else A = (int)t + 44;
        magic[s].animNo = ((uint16_t)magic[s].anim) | A;
    }
}

void loadAtr(const sjis chip[4]) {
    memset(atr, 0, sizeof atr);
    for (int k = 0; k < 4; k++) {
        std::vector<uint8_t> f;
        const sjis& n = chip[k];
        bool ok;
        if (n.size() >= 2 && n[1] == ':') {
            ok = fsys::readAbsolute(n + ".ATR", f);
            if (!ok) ok = fsys::read(sj::baseName(n) + ".ATR", f);  // optional fallback (§7)
        } else ok = fsys::read(n + ".ATR", f);
        if (!ok) continue;
        for (int i = 0; i < 256 && i < (int)f.size(); i++) atr[k * 256 + i] = f[i];
    }
}

// ------------------------------------------------------------------ events
std::string eventFileName(int id) {
    char b[32];
    snprintf(b, sizeof b, "eve%05d.dat", id);
    return b;
}

static bool parseHeaderAndRoutes(ByteReader& r, EventHeader& h) {
    if (r.left() < 0x88) return false;
    int32_t size = r.s32();
    if (size != 0x88) return false;
    r.skip(8);
    r.bytes(h.attr.raw, 0x58);
    return true;
}

bool readEventHeader(int id, EventHeader& out) {
    std::vector<uint8_t> f;
    if (!fsys::read(eventFileName(id), f)) return false;
    ByteReader r(f);
    if (!parseHeaderAndRoutes(r, out)) return false;
    r.pos = 0x88;
    for (int p = 0; p < 4; p++) {
        if (r.left() < 1) return false;
        int n = r.u8();
        if (r.left() < (size_t)n) return false;
        out.routes[p].assign(f.begin() + r.pos, f.begin() + r.pos + n);
        r.pos += n;
    }
    return true;
}

static bool g_slotUsed[256];
static sjis g_slots[256];
static const sjis kEmpty;

const sjis& eventString(int idx) {
    if (idx < 0 || idx > 255 || !g_slotUsed[idx]) return kEmpty;
    return g_slots[idx];
}

void clearEventStrings() {
    for (int i = 0; i < 256; i++) { g_slotUsed[i] = false; g_slots[i].clear(); }
}

bool readEventScript(int id, EventScript& out) {
    out = EventScript();
    std::vector<uint8_t> f;
    if (!fsys::read(eventFileName(id), f)) return false;
    ByteReader r(f);
    if (!parseHeaderAndRoutes(r, out.hdr)) return false;
    int32_t ncmd[4];
    for (int p = 0; p < 4; p++) ncmd[p] = rds32(f.data() + 0x74 + 4 * p);
    int32_t strSize = rds32(f.data() + 0x84);
    r.pos = 0x88;
    for (int p = 0; p < 4; p++) {
        if (r.left() < 1) return false;
        int n = r.u8();
        if (r.left() < (size_t)n) return false;
        out.hdr.routes[p].assign(f.begin() + r.pos, f.begin() + r.pos + n);
        r.pos += n;
    }
    for (int p = 0; p < 4; p++) {
        if (ncmd[p] < 0) return false;
        if (r.left() < (size_t)ncmd[p] * 20) return false;
        out.cmds[p].resize(ncmd[p]);
        for (int i = 0; i < ncmd[p]; i++) {
            Cmd& c = out.cmds[p][i];
            c.code = r.s32(); c.a1 = r.s32(); c.a2 = r.s32(); c.a3 = r.s32(); c.a4 = r.s32();
        }
    }
    // string list (02-data-formats.md §4.4)
    bool failed = false;
    int consumed = 0, prev = -1;
    for (;;) {
        if (r.left() < 1) { failed = true; break; }
        int b = r.u8();
        if (b == 0x1A) {
            if (prev != 25) break;
            if (consumed + 1 >= strSize) break;
            consumed += 1;
        }
        sjis s;
        bool term = false;
        while (r.left() > 0) {
            char c = (char)r.u8();
            if (!c) { term = true; break; }
            s += c;
        }
        if (!term) { failed = true; break; }
        consumed += 1 + (int)s.size() + 1;
        if (g_slotUsed[b]) break;  // duplicate index ends the list without failure
        g_slotUsed[b] = true;
        g_slots[b] = s;
        prev = b;
        if (consumed > strSize) failed = true;
    }
    // command move routes until the end of the file
    while (r.left() > 0) {
        int n = r.u8();
        size_t k = std::min((size_t)n, r.left());
        out.routes.emplace_back(f.begin() + r.pos, f.begin() + r.pos + k);
        r.pos += k;
    }
    return !failed;
}

}  // namespace data
