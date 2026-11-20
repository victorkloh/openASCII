#include <doctest/doctest.h>

#include "core/ProceduralGlyphs.h"

#include <vector>

using namespace ap;

namespace {
struct Img {
    int w, h;
    std::vector<uint8_t> px;
    Img(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_ * h_), 0) {}
    uint8_t at(int x, int y) const { return px[static_cast<size_t>(y * w + x)]; }
    int count() const {
        int n = 0;
        for (auto v : px) n += v > 0;
        return n;
    }
};

Img draw(char32_t cp, int w = 9, int h = 18) {
    Img img(w, h);
    REQUIRE(rasterizeProceduralGlyph(cp, w, h, img.px, w));
    return img;
}
} // namespace

TEST_CASE("quais caracteres são procedurais") {
    CHECK(isProceduralGlyph(U'█'));
    CHECK(isProceduralGlyph(U'░'));
    CHECK(isProceduralGlyph(U'─'));
    CHECK(isProceduralGlyph(U'┼'));
    CHECK(isProceduralGlyph(U'⣿'));
    CHECK_FALSE(isProceduralGlyph(U'A'));
    CHECK_FALSE(isProceduralGlyph(U' '));
    Img img(4, 4);
    CHECK_FALSE(rasterizeProceduralGlyph(U'A', 4, 4, img.px, 4));
}

TEST_CASE("blocos ocupam a célula inteira, sem costuras") {
    const Img full = draw(U'█');
    CHECK(full.count() == 9 * 18);
    CHECK(full.at(0, 0) == 255);
    CHECK(full.at(8, 17) == 255);

    const Img upper = draw(U'▀');
    CHECK(upper.at(4, 0) == 255);
    CHECK(upper.at(4, 8) == 255);
    CHECK(upper.at(4, 9) == 0);
    // ▀ + ▄ = █ (sem píxeis em falta nem sobrepostos), mesmo com altura ímpar
    const Img top = draw(U'▀', 7, 15), bottom = draw(U'▄', 7, 15);
    for (int y = 0; y < 15; ++y)
        for (int x = 0; x < 7; ++x) CHECK((top.at(x, y) > 0) != (bottom.at(x, y) > 0));
}

TEST_CASE("sombreados") {
    CHECK(draw(U'░').at(3, 3) == 64);
    CHECK(draw(U'▒').at(3, 3) == 128);
    CHECK(draw(U'▓').at(3, 3) == 192);
}

TEST_CASE("linhas de caixa tocam nas bordas certas") {
    const Img h = draw(U'─');
    CHECK(h.at(0, 8) == 255);
    CHECK(h.at(8, 8) == 255);
    CHECK(h.at(4, 0) == 0);

    const Img corner = draw(U'┌');
    CHECK(corner.at(8, 8) == 255);  // braço direito chega à borda
    CHECK(corner.at(4, 17) == 255); // braço de baixo chega à borda
    CHECK(corner.at(0, 8) == 0);    // sem braço esquerdo
    CHECK(corner.at(4, 0) == 0);    // sem braço de cima

    const Img heavy = draw(U'━', 9, 32);
    CHECK(heavy.count() > draw(U'─', 9, 32).count());
}

TEST_CASE("célula dentro de um atlas maior (stride > largura)") {
    // Atlas de 3 células de 4x6 lado a lado; desenhar █ na do meio.
    const int w = 4, h = 6, stride = 12;
    std::vector<uint8_t> atlas(static_cast<size_t>(stride * h), 0);
    std::span<uint8_t> cell(atlas.data() + 4, static_cast<size_t>(stride * (h - 1) + w));
    REQUIRE(rasterizeProceduralGlyph(U'█', w, h, cell, stride));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < stride; ++x) {
            const bool inMiddle = x >= 4 && x < 8;
            CHECK((atlas[static_cast<size_t>(y * stride + x)] == 255) == inMiddle);
        }
}

TEST_CASE("braille") {
    CHECK(draw(U'⠀').count() == 0); // em branco
    const Img all = draw(U'⣿', 10, 20);
    const Img one = draw(U'⠁', 10, 20);   // ponto 1 (canto superior esquerdo)
    CHECK(all.count() == one.count() * 8);
    CHECK(one.at(2, 2) == 255);
}
