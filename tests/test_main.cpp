// Test runner: runs every registered test (or only those whose name contains argv[1]).
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#include "test.h"

namespace test {

std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

namespace {
struct Failure {
    std::string what;
};
}  // namespace

void fail(const char* file, int line, const std::string& what) {
    throw Failure{std::string(file) + ":" + std::to_string(line) + ": " + what};
}

std::string samplesDir() {
    const char* e = std::getenv("OPENMK95_SAMPLES");
    return (e && *e) ? e : "samples";
}

std::string tmpDir() {
    const char* e = std::getenv("OPENMK95_TMP");
    if (e && *e) return e;
    std::error_code ec;
    auto p = std::filesystem::temp_directory_path(ec);
    auto u = p.u8string();
    return ec ? std::string(".") : std::string(u.begin(), u.end());
}

std::vector<std::string> sampleGames() {
    namespace fs = std::filesystem;
    std::vector<std::string> res;
    std::error_code ec;
    fs::path root = fs::u8path(samplesDir());
    if (!fs::is_directory(root, ec)) return res;
    for (auto it = fs::directory_iterator(root, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        std::error_code ec2;
        for (auto jt = fs::directory_iterator(it->path(), ec2); !ec2 && jt != fs::directory_iterator();
             jt.increment(ec2)) {
            auto ext = jt->path().extension().u8string();
            std::string e(ext.begin(), ext.end());
            for (auto& c : e) c = (char)std::tolower((unsigned char)c);
            if (e == ".rpg") {
                auto u = it->path().u8string();
                res.emplace_back(u.begin(), u.end());
                break;
            }
        }
    }
    std::sort(res.begin(), res.end());
    return res;
}

}  // namespace test

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int passed = 0, failed = 0, skipped = 0;
    for (auto& c : test::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        try {
            c.fn();
            passed++;
            std::printf("[ OK ] %s\n", c.name);
        } catch (const test::Skipped& s) {
            skipped++;
            std::printf("[SKIP] %s: %s\n", c.name, s.why.c_str());
        } catch (const test::Failure& f) {
            failed++;
            std::printf("[FAIL] %s: %s\n", c.name, f.what.c_str());
        } catch (const std::exception& e) {
            failed++;
            std::printf("[FAIL] %s: exception: %s\n", c.name, e.what());
        }
    }
    std::printf("%d passed, %d failed, %d skipped\n", passed, failed, skipped);
    return failed ? 1 : 0;
}
