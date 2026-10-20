// Tests for src/core: random numbers, Shift_JIS text, file-name rules, font metrics.
#include "core/font.h"
#include "core/rng.h"
#include "core/sjis.h"
#include "test.h"

// spec 01 §6 and 11 §E.1: the first ten outputs after program start.
TEST(rng_first_ten_outputs) {
    rng::setState(1);
    const uint32_t expect[10] = {5540u,       221838220u,  695693412u, 1410949376u, 822182988u,
                                 1990930872u, 236698092u,  2108515212u, 1428137313u, 1164941360u};
    for (uint32_t e : expect) CHECK_EQ(rng::R(), e);
}

// spec 01 §6: where the modulus can be 0 no number is drawn; negative n gives R mod |n|.
TEST(rng_modulus_rules) {
    rng::setState(1);
    uint64_t before = rng::drawCount;
    CHECK_EQ(rng::rnd(0), 0);
    CHECK_EQ(rng::drawCount, before);
    CHECK_EQ(rng::state(), (uint64_t)1);
    CHECK_EQ(rng::rnd(7), (int)(5540u % 7));
    CHECK_EQ(rng::rnd(-5), (int)(221838220u % 5));
    CHECK_EQ(rng::drawCount, before + 2);
}

TEST(sjis_utf8_round_trip) {
    sjis s = sj::S("ゲームフォルダ ﾃｽﾄ ABC");
    CHECK_EQ(s.size(), (size_t)(7 * 2 + 1 + 3 + 1 + 3));
    CHECK(sj::toUtf8(s) == "ゲームフォルダ ﾃｽﾄ ABC");
    sjis out;
    CHECK(sj::fromUtf8("表示", out));
    CHECK(out == std::string("\x95\x5C\x8E\xA6"));
    CHECK(!sj::fromUtf8("\xE2\x98\x83", out));  // U+2603 has no CP932 mapping
}

TEST(sjis_next_character) {
    sjis s = sj::S("aソ");
    size_t i = 0;
    CHECK_EQ(sj::next(s, i), (int)'a');
    CHECK_EQ(sj::next(s, i), 0x835C);
    CHECK_EQ(i, s.size());
}

// spec 02 §2.2: case-insensitive name matching; trail bytes are not case-folded.
TEST(file_name_keys) {
    CHECK(sj::foldName("Mchip0.bmp") == "MCHIP0.BMP");
    sjis dji = sj::S("ヂ.wav");  // 83 61: the trail byte is 'a'
    CHECK(sj::foldName(dji) == sjis("\x83\x61.WAV"));
    CHECK(sj::nameKey(sj::S("ﾀｲﾄﾙ.bmp")) == sj::nameKeyUtf8("ﾀｲﾄﾙ.BMP"));
    CHECK(sj::nameKey(sj::S("表紙.Bmp")) == sj::nameKeyUtf8("表紙.bmp"));
    CHECK(sj::nameKey(sj::S("ヂ")) != sj::nameKey(sj::S("ヅ")));
}

// spec 02 §2.4: byte-wise base-name reduction (quirk: trail byte 0x5C is a separator).
TEST(base_name_reduction) {
    CHECK(sj::baseName("E:\\rpg\\mid\\TOMO.mid") == "TOMO.mid");
    CHECK(sj::baseName("TITLE.BMP") == "TITLE.BMP");
    CHECK(sj::baseName(sj::S("ソード.wav")) == sj::S("ード.wav"));
    CHECK(sj::baseName(sj::S("C:\\Program Files\\RPGﾂｸｰﾙ95\\商王伝\\カーソル1.wav")) == sj::S("ル1.wav"));
    CHECK(sj::baseName(sj::S("E:\\Program Files\\RPGﾂｸｰﾙ95\\スグル\\予告挨拶.bmp")) == sj::S("告挨拶.bmp"));
    CHECK(sj::baseName(sj::S("ﾃｽﾄ/a.bmp")) == sj::S("ﾃｽﾄ/a.bmp"));
}

// spec 06 §2: reference font metrics (W = 17 for double-byte, 9 for single-byte characters).
TEST(font_advance_widths) {
    CHECK_EQ(font::advance('A'), 9);
    CHECK_EQ(font::advance(0xB1), 9);    // half-width katakana
    CHECK_EQ(font::advance(0x8341), 17);  // ア
    CHECK_EQ(font::advance('\n'), 0);
    CHECK_EQ(font::advanceRegular('A'), 8);
    CHECK_EQ(font::textWidth(sj::S("はい AB")), 17 * 2 + 9 * 3);
    CHECK_EQ(font::MaxW, 17);
    CHECK_EQ(font::LineH, 18);
}
