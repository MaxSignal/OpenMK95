// Minimal unit-test framework for OpenMK95.
#pragma once
#include <cstdio>
#include <string>
#include <vector>

namespace test {

struct Case {
    const char* name;
    void (*fn)();
};
std::vector<Case>& registry();

struct Register {
    Register(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

// Thrown by SKIP(): the test needs something that is not available (for example game data).
struct Skipped {
    std::string why;
};

void fail(const char* file, int line, const std::string& what);

// Root of the sample games ($OPENMK95_SAMPLES, default ./samples): <root>/<game folder>/…
std::string samplesDir();
// Scratch directory ($OPENMK95_TMP, default: the system temporary directory).
std::string tmpDir();
// Game folders (folders containing a .RPG file) directly under samplesDir(), sorted.
std::vector<std::string> sampleGames();

}  // namespace test

#define TEST(name)                                                   \
    static void test_##name();                                       \
    static test::Register test_reg_##name(#name, &test_##name);      \
    static void test_##name()

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) test::fail(__FILE__, __LINE__, #cond);          \
    } while (0)

#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        auto test_a_ = (a);                                                                \
        auto test_b_ = (b);                                                                \
        if (!(test_a_ == test_b_))                                                         \
            test::fail(__FILE__, __LINE__,                                                 \
                       std::string(#a " == " #b " (") + std::to_string(test_a_) + " vs " + \
                           std::to_string(test_b_) + ")");                                 \
    } while (0)

#define SKIP(why) throw test::Skipped{why}
