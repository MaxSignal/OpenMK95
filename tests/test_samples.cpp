// Tests that read the sample games ($OPENMK95_SAMPLES): spec 11 §E.2 (file parsing, save files).
// Skipped when no sample games are available (as in CI).
#include <cstring>
#include <filesystem>

#include "core/fsys.h"
#include "core/sjis.h"
#include "engine/data.h"
#include "engine/display.h"
#include "engine/game.h"
#include "platform/platform.h"
#include "test.h"

namespace {

bool loadGame(const std::string& dir) {
    if (!fsys::setGameDir(dir)) return false;
    std::vector<sjis> rpgs = fsys::listWithExt(".RPG");
    if (rpgs.empty() || !data::loadGameFile(rpgs.back())) return false;
    data::loadStrings();
    if (!data::loadEParty() || !data::scanMonstPic()) return false;
    data::loadDatabases();
    return true;
}

// Event ids of the eveNNNNN.dat files of the current game folder.
std::vector<int> eventIds() {
    std::vector<int> ids;
    for (const sjis& n : fsys::listWithExt(".DAT")) {
        sjis f = sj::foldName(n);
        if (f.size() == 12 && f.compare(0, 3, "EVE") == 0) {
            bool digits = true;
            for (int i = 3; i < 8; i++) digits = digits && f[i] >= '0' && f[i] <= '9';
            if (digits) ids.push_back(std::atoi(f.substr(3, 5).c_str()));
        }
    }
    return ids;
}

void initHeadless() {
    static bool done = false;
    if (done) return;
    plat::Options o;
    o.headless = true;
    o.virtualTime = true;
    if (!plat::init(o, "unit_tests")) SKIP("SDL could not be initialised");
    disp::init();
    ui::init();
    done = true;
}

}  // namespace

TEST(samples_all_files_parse) {
    auto games = test::sampleGames();
    if (games.empty()) SKIP("no sample games in " + test::samplesDir());
    size_t events = 0, rejected = 0;
    for (auto& g : games) {
        if (!loadGame(g)) test::fail(__FILE__, __LINE__, "cannot load " + g);
        for (int id : eventIds()) {
            EventScript s;
            data::clearEventStrings();
            events++;
            if (!data::readEventScript(id, s)) rejected++;
        }
        for (int m = 0; m < 400; m++) {
            MonsterPic pic;
            data::loadMonsterPic(m, pic);  // must not crash; missing pictures are allowed
        }
    }
    std::printf("       %zu games, %zu event files (%zu rejected by the format rules)\n", games.size(), events,
                rejected);
    CHECK(events > 0);
}

// Loading a save file and saving again gives a file of the same size; a second cycle is
// byte-identical (spec 10, 11 §E.2).
TEST(samples_save_round_trip) {
    auto games = test::sampleGames();
    if (games.empty()) SKIP("no sample games in " + test::samplesDir());
    size_t saves = 0;
    for (auto& g : games) {
        if (!loadGame(g)) test::fail(__FILE__, __LINE__, "cannot load " + g);
        for (const sjis& name : fsys::listWithExt(".SAV")) {
            std::vector<uint8_t> file, a, b;
            CHECK(fsys::read(name, file));
            initHeadless();
            if (save::readSaveData(file) != 0) test::fail(__FILE__, __LINE__, "cannot read " + fsys::displayPath(name));
            save::buildSave(a);
            CHECK_EQ(a.size(), file.size());
            CHECK_EQ(save::readSaveData(a), 0);
            save::buildSave(b);
            CHECK(a == b);
            saves++;
        }
    }
    if (!saves) SKIP("no save files in the sample games");
    std::printf("       %zu save files\n", saves);
}
