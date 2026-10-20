// RM95 runtime reimplementation: program start (01-runtime.md §2, §9.1).
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include "engine/display.h"
#include "core/fsys.h"
#include "engine/game.h"
#include "platform/platform.h"
#include "engine/runtime.h"
#include "core/sjis.h"
#include "sound/snd.h"

namespace rt { extern int g_testBattle; }
static std::string U8(const char* sjisUtf8) { return sjisUtf8; }

static void usage() {
    fprintf(stderr,
            "OpenMK95 - clean-room RPG Maker 95 game runtime\n"
            "usage: openmk95 [options] [game folder]\n"
            "  --fullscreen          start in full screen (F4 / Alt+Enter toggles)\n"
            "  --scale N             window scale factor\n"
            "  --smooth              linear filtering when scaling\n"
            "  --soundfont FILE      SoundFont (.sf2) for MIDI music\n"
            "  --midi auto|sf2|native|builtin|none\n"
            "  --volume N            master volume 0..100\n"
            "  --headless --virtual-time --script FILE   (automated tests)\n"
            "Without a folder the folder of the executable or the current folder is used\n"
            "if it contains a .RPG file.\n");
}

static bool hasRpg(const std::string& dir) {
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(std::filesystem::u8path(dir), ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        auto ext = it->path().extension().u8string();
        std::string e(ext.begin(), ext.end());
        for (auto& c : e) c = (char)tolower((unsigned char)c);
        if (e == ".rpg") return true;
    }
    return false;
}

static void initParty() {
    using data::gd;
    int n = 0;
    int order[8], k = 0;
    for (int a = 0; a < 8; a++) if (gd.actors[a].startMember) order[k++] = a;
    n = k;
    for (int a = 0; a < 8; a++) if (!gd.actors[a].startMember) order[k++] = a;
    for (int r = 0; r < 8; r++) {
        int a = order[r];
        const Actor& A = gd.actors[a];
        Battler& b = G.party[r];
        memset(&b, 0, sizeof b);
        b.actorId = (int8_t)a;
        b.level = s16(A.level);
        b.hp = b.maxhp = s16(A.hp);
        b.mp = b.maxmp = s16(A.mp);
        b.vit = s16(A.vit); b.pow = s16(A.pow); b.agi = s16(A.agi); b.intl = s16(A.intl); b.luck = s16(A.luck);
        b.def = s16(A.def); b.atk = s16(A.atk);
        b.exp = (A.level - 1) * (A.level - 1) * 10;
        setBattlerName(b, A.name);
        memcpy(b.rate, A.rates, 100);
        int e4 = A.equip[4];
        b.mdef = (e4 >= 0 && e4 < 100) ? data::items[e4].power : 0;
        memcpy(G.equip[a], A.equip, 5);
        memcpy(G.learn[a], A.learn, 100);
    }
    G.partySize = n;
    for (int i = 0; i < 8; i++) G.partyActor[i] = i < n ? order[i] : 0;
    for (int i = 0; i < 100; i++) G.itemCount[i] = gd.startItems[i];
    G.money = gd.startMoney;
}

int main(int argc, char** argv) {
    plat::Options opt;
    std::string dir, soundfont, midiBackend = "auto";
    int volume = 100;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--fullscreen") opt.fullscreen = true;
        else if (a == "--scale") opt.scale = atoi(next().c_str());
        else if (a == "--smooth") opt.smooth = true;
        else if (a == "--soundfont") soundfont = next();
        else if (a == "--midi") midiBackend = next();
        else if (a == "--volume") volume = atoi(next().c_str());
        else if (a == "--test-battle") rt::g_testBattle = atoi(next().c_str());
        else if (a == "--headless") opt.headless = true;
        else if (a == "--virtual-time") opt.virtualTime = true;
        else if (a == "--script") opt.script = next();
        else if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (!a.empty() && a[0] == '-' && a != "-") { fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 1; }
        else dir = a;
    }
    bool askFolder = false;
    if (dir.empty()) {
        char* bp = SDL_GetBasePath();
        std::string base = bp ? bp : "";
        if (bp) SDL_free(bp);
        if (!base.empty() && hasRpg(base)) dir = base;
        else if (hasRpg(".")) dir = ".";
        else if (!opt.headless) askFolder = true;
        else dir = ".";
    }
    if (askFolder) {
        if (!plat::init(opt, "OpenMK95")) return 1;
        disp::init();
        std::vector<uint8_t> px(640 * 480, 0);
        Rect clip(0, 0, 640, 480);
        ui::drawLabelText(px.data(), 640, 480, clip, Rect(40, 180, 600, 300),
                          sj::S("RPGツクール95 のゲームフォルダ（.RPG ファイルのある\nフォルダ）をこのウィンドウにドロップしてください。"),
                          ui::CENTRE_WRAP, 255);
        std::vector<uint32_t> rgb(640 * 480);
        for (size_t i = 0; i < rgb.size(); i++) rgb[i] = px[i] ? 0xFFFFFF : 0x000040;
        plat::showFrame(rgb.data());
        for (;;) {
            std::string p;
            if (!plat::waitForDrop(p)) { plat::shutdown(); return 0; }
            std::error_code ec;
            auto fp = std::filesystem::u8path(p);
            if (!std::filesystem::is_directory(fp, ec)) { auto u = fp.parent_path().u8string(); p = std::string(u.begin(), u.end()); }
            if (hasRpg(p)) { dir = p; break; }
        }
        plat::setCaption("");
    }
    if (!fsys::setGameDir(dir)) {
        fprintf(stderr, "cannot open game folder %s\n", dir.c_str());
        usage();
        return 1;
    }
    // ---- initial state (01 §2.4 step 1)
    memset(G.switches, 0, sizeof G.switches);
    memset(G.teleportList, 0, sizeof G.teleportList);
    for (auto& a : G.actions) a = ActionRec();
    G.eventRunning = true;
    G.releaseLatch = true;
    G.curMap = -1;
    G.encArea = -1;
    for (int i = 0; i < 8; i++) G.partyChar[i] = nullptr;

    if (askFolder) plat::setCaption(fsys::gameDirNameUtf8());
    else if (!plat::init(opt, fsys::gameDirNameUtf8())) {
        plat::messageBox(sj::toUtf8(sj::S("DirectDrawの作成に失敗しました。\nｹﾞｰﾑの実行を中止します")), sj::toUtf8(sj::S("RPGﾂｸｰﾙ95")), plat::MB_NONE, false);
        return 1;
    }
    std::vector<sjis> rpgs = fsys::listWithExt(".RPG");
    if (rpgs.size() > 1) fprintf(stderr, "warning: several .RPG files, using the last one\n");
    const std::string cap95 = sj::toUtf8(sj::S("RPGﾂｸｰﾙ95"));
    if (rpgs.empty() || fsys::exists("suicide.exe")) {
        data::loadStrings();
        plat::messageBox(sj::toUtf8(sj::S("このプログラムは単独では動作しません。")), sj::toUtf8(sj::S("RPGﾂｸｰﾙ95 - Game.exe")), plat::MB_NONE, false);
        plat::shutdown();
        return 1;
    }
    if (!data::loadGameFile(rpgs.back())) {
        plat::messageBox("Can't open file!!!", fsys::displayPath(rpgs.back()), plat::MB_NONE, false);
        plat::shutdown();
        return 1;
    }
    for (int s = 0; s < 12; s++) G.slots[s] = data::gd.slots[s];
    for (int i = 0; i < 3; i++) G.trans[i] = data::gd.trans[i];
    data::loadStrings();
    if (!data::loadEParty()) {
        plat::messageBox(sj::toUtf8(sj::S("ﾌｧｲﾙ[EPARTY.DAT]が見つかりません")), cap95, plat::MB_ERROR, false);
        plat::shutdown();
        return 1;
    }
    if (!data::scanMonstPic()) {
        plat::messageBox(sj::toUtf8(sj::S("ﾌｧｲﾙ[MONSTPIC.DAT]が見つかりません")), cap95, plat::MB_ERROR, false);
        plat::shutdown();
        return 1;
    }
    data::loadDatabases();
    initParty();

    // ---- main routine (01 §9.1)
    snd::setSoundFontPath(soundfont);
    snd::setMidiBackend(midiBackend);
    snd::setMasterVolume(volume);
    G.encSteps = 0;
    G.lastTick = 0;
    G.heroScrollSub = 0;
    G.arrival = true;
    disp::init();
    ui::init();
    ui::transparentMode = data::gd.transparentWindows();
    const MapRec* sm = (data::gd.startMap >= 0 && data::gd.startMap < 500) ? &data::gd.maps[data::gd.startMap] : nullptr;
    sjis chips[4] = {"MCHIP0", "MCHIP1", "MCHIP2", "MCHIP3"};
    if (sm && sm->hasChip) for (int k = 0; k < 4; k++) chips[k] = sm->chip[k];
    disp::loadMapGraphics(chips);
    data::loadAtr(chips);
    audio::init();
    G.battleLost = false;
    mapx::setupPartyCharacters();
    disp::back.fill(0);
    disp::blackFillVisible();
    G.screenState = SS_ERASED;
    G.heroWait.clear();
    G.riding = false;
    G.moveControl = 0;
    G.saveEnabled = data::gd.savingEnabled();
    G.eventRunning = true;
    fprintf(stderr, "openmk95: game '%s' (%s, data version %s), MIDI: %s\n", fsys::gameDirNameUtf8().c_str(),
            sj::toUtf8(data::rpgName).c_str(), data::gd.versionText.c_str(), snd::midiBackendDescription().c_str());
    rt::runTitle();
    // program end (01 §3.3)
    disp::visible.fill(0);
    disp::markDirty();
    audio::stopAllMusic();
    snd::shutdown();
    plat::shutdown();
    (void)U8;
    return 0;
}
