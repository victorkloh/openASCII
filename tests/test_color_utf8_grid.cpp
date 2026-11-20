#include <doctest/doctest.h>

#include "core/Color.h"
#include "core/Grid.h"
#include "core/Utf8.h"

using namespace ap;

TEST_CASE("parseColor") {
    CHECK(parseColor("#05070d") == Rgba8{5, 7, 13, 255});
    CHECK(parseColor("#FFF") == Rgba8{255, 255, 255, 255});
    CHECK(parseColor("#11223380") == Rgba8{0x11, 0x22, 0x33, 0x80});
    CHECK_FALSE(parseColor("05070d"));
    CHECK_FALSE(parseColor("#05070"));
    CHECK_FALSE(parseColor("#zzzzzz"));
    CHECK_FALSE(parseColor(""));
    CHECK(Rgba8{1, 2, 3, 4}.packed() == 0x04030201u);
}

TEST_CASE("UTF-8") {
    CHECK(decodeUtf8(" .:@") == U" .:@");
    CHECK(decodeUtf8("·•●") == U"·•●");
    CHECK(decodeUtf8("⣿█") == U"⣿█");
    bool ok = true;
    CHECK(decodeUtf8("a\xFF" "b", &ok) == U"a�b");
    CHECK_FALSE(ok);
    CHECK(decodeUtf8("\xC0\xAF", &ok).front() == U'�'); // codificação longa demais
    CHECK(encodeUtf8(U"·█⣿") == "·█⣿");
}

TEST_CASE("computeGrid centra a grelha") {
    const auto g = computeGrid(1920, 1080, 7, 15);
    CHECK(g.cols == 274); // 274*7 = 1918
    CHECK(g.rows == 72);  // 72*15 = 1080
    CHECK(g.originX == 1);
    CHECK(g.originY == 0);

    const auto g2 = computeGrid(100, 50, 10, 20);
    CHECK(g2.cols == 10);
    CHECK(g2.rows == 2);
    CHECK(g2.originY == 5); // sobram 10 px -> 5 em cima, 5 em baixo

    CHECK(computeGrid(0, 0, 10, 10).cellCount() == 0);
    CHECK(computeGrid(100, 100, 0, -3).cellW == 1); // proteção contra divisão por zero
}

TEST_CASE("valueToCharIndex") {
    CHECK(valueToCharIndex(0.0f, 10) == 0);
    CHECK(valueToCharIndex(0.05f, 10) == 0);
    CHECK(valueToCharIndex(0.1f, 10) == 1);
    CHECK(valueToCharIndex(0.99f, 10) == 9);
    CHECK(valueToCharIndex(1.0f, 10) == 9);
    CHECK(valueToCharIndex(5.0f, 10) == 9);
    CHECK(valueToCharIndex(-1.0f, 10) == 0);
    CHECK(valueToCharIndex(0.5f, 0) == 0);
}
