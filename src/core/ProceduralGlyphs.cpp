#include "core/ProceduralGlyphs.h"

#include <algorithm>

namespace ap {

namespace {

struct Canvas {
    int w, h, stride;
    std::span<uint8_t> px;

    // Preenche [x0,x1) × [y0,y1), recortado à célula; guarda o máximo (para sobreposições).
    void fill(int x0, int y0, int x1, int y1, uint8_t v = 255) const {
        x0 = std::clamp(x0, 0, w), x1 = std::clamp(x1, 0, w);
        y0 = std::clamp(y0, 0, h), y1 = std::clamp(y1, 0, h);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                uint8_t& p = px[static_cast<size_t>(y) * static_cast<size_t>(stride) + static_cast<size_t>(x)];
                p = std::max(p, v);
            }
    }
};

// Frações em oitavos, arredondadas a píxeis inteiros.
int eighths(int size, int n) { return (size * n + 4) / 8; }

bool blockElement(char32_t cp, const Canvas& c) {
    const int w = c.w, h = c.h, hw = w / 2, hh = h / 2;
    if (cp == 0x2580) return c.fill(0, 0, w, hh), true;                         // ▀
    if (cp >= 0x2581 && cp <= 0x2588) {                                          // ▁..█
        return c.fill(0, h - eighths(h, static_cast<int>(cp - 0x2580)), w, h), true;
    }
    if (cp >= 0x2589 && cp <= 0x258F) {                                          // ▉..▏
        return c.fill(0, 0, eighths(w, static_cast<int>(0x2590 - cp)), h), true;
    }
    if (cp == 0x2590) return c.fill(hw, 0, w, h), true;                          // ▐
    if (cp == 0x2591) return c.fill(0, 0, w, h, 64), true;                       // ░
    if (cp == 0x2592) return c.fill(0, 0, w, h, 128), true;                      // ▒
    if (cp == 0x2593) return c.fill(0, 0, w, h, 192), true;                      // ▓
    if (cp == 0x2594) return c.fill(0, 0, w, eighths(h, 1)), true;               // ▔
    if (cp == 0x2595) return c.fill(w - eighths(w, 1), 0, w, h), true;           // ▕
    if (cp >= 0x2596 && cp <= 0x259F) {
        // Quadrantes: bits UL=1, UR=2, LL=4, LR=8.
        static constexpr uint8_t quads[] = {4, 8, 1, 1 | 4 | 8, 1 | 8, 1 | 2 | 4, 1 | 2 | 8, 2, 2 | 4, 2 | 4 | 8};
        const uint8_t q = quads[cp - 0x2596];
        if (q & 1) c.fill(0, 0, hw, hh);
        if (q & 2) c.fill(hw, 0, w, hh);
        if (q & 4) c.fill(0, hh, hw, h);
        if (q & 8) c.fill(hw, hh, w, h);
        return true;
    }
    return false;
}

// Linhas de caixa: cada caractere = 4 braços (cima, direita, baixo, esquerda),
// com peso 0 (nenhum), 1 (fino) ou 2 (grosso).
struct Arms {
    char32_t cp;
    uint8_t up, right, down, left;
};

constexpr Arms kBox[] = {
    {0x2500, 0, 1, 0, 1}, {0x2501, 0, 2, 0, 2}, {0x2502, 1, 0, 1, 0}, {0x2503, 2, 0, 2, 0}, // ─━│┃
    {0x250C, 0, 1, 1, 0}, {0x250F, 0, 2, 2, 0}, {0x2510, 0, 0, 1, 1}, {0x2513, 0, 0, 2, 2}, // ┌┏┐┓
    {0x2514, 1, 1, 0, 0}, {0x2517, 2, 2, 0, 0}, {0x2518, 1, 0, 0, 1}, {0x251B, 2, 0, 0, 2}, // └┗┘┛
    {0x251C, 1, 1, 1, 0}, {0x2523, 2, 2, 2, 0}, {0x2524, 1, 0, 1, 1}, {0x252B, 2, 0, 2, 2}, // ├┣┤┫
    {0x252C, 0, 1, 1, 1}, {0x2533, 0, 2, 2, 2}, {0x2534, 1, 1, 0, 1}, {0x253B, 2, 2, 0, 2}, // ┬┳┴┻
    {0x253C, 1, 1, 1, 1}, {0x254B, 2, 2, 2, 2},                                             // ┼╋
    {0x2574, 0, 0, 0, 1}, {0x2575, 1, 0, 0, 0}, {0x2576, 0, 1, 0, 0}, {0x2577, 0, 0, 1, 0}, // ╴╵╶╷
    {0x2578, 0, 0, 0, 2}, {0x2579, 2, 0, 0, 0}, {0x257A, 0, 2, 0, 0}, {0x257B, 0, 0, 2, 0}, // ╸╹╺╻
    {0x256D, 0, 1, 1, 0}, {0x256E, 0, 0, 1, 1}, {0x256F, 1, 0, 0, 1}, {0x2570, 1, 1, 0, 0}, // ╭╮╯╰ (como cantos)
};

const Arms* findBox(char32_t cp) {
    for (const Arms& a : kBox)
        if (a.cp == cp) return &a;
    return nullptr;
}

void boxDrawing(const Arms& a, const Canvas& c) {
    const int light = std::max(1, (c.h + 8) / 16);
    const int heavy = std::max(light + 1, light * 2);
    auto thick = [&](uint8_t weight) { return weight == 2 ? heavy : light; };
    // Centro da célula: a linha vertical ocupa [vx, vx+t) e a horizontal [hy, hy+t).
    auto vx = [&](int t) { return (c.w - t) / 2; };
    auto hy = [&](int t) { return (c.h - t) / 2; };
    // A espessura do cruzamento é a do braço mais grosso, para os braços se juntarem bem.
    const int tv = thick(std::max(a.up, a.down));
    const int th = thick(std::max(a.left, a.right));
    if (a.left) c.fill(0, hy(thick(a.left)), vx(tv) + tv, hy(thick(a.left)) + thick(a.left));
    if (a.right) c.fill(vx(tv), hy(thick(a.right)), c.w, hy(thick(a.right)) + thick(a.right));
    if (a.up) c.fill(vx(thick(a.up)), 0, vx(thick(a.up)) + thick(a.up), hy(th) + th);
    if (a.down) c.fill(vx(thick(a.down)), hy(th), vx(thick(a.down)) + thick(a.down), c.h);
}

void braille(char32_t cp, const Canvas& c) {
    // Bits Unicode -> (coluna, linha) dos 8 pontos.
    static constexpr int col[8] = {0, 0, 0, 1, 1, 1, 0, 1};
    static constexpr int row[8] = {0, 1, 2, 0, 1, 2, 3, 3};
    const unsigned bits = static_cast<unsigned>(cp - 0x2800);
    const int subW = c.w / 2, subH = c.h / 4;
    const int dot = std::max(1, std::min(subW, subH) * 2 / 3);
    for (int i = 0; i < 8; ++i) {
        if (!(bits & (1u << i))) continue;
        const int x0 = col[i] * subW + (subW - dot) / 2;
        const int y0 = row[i] * subH + (subH - dot) / 2;
        c.fill(x0, y0, x0 + dot, y0 + dot);
    }
}

} // namespace

bool isProceduralGlyph(char32_t cp) {
    return (cp >= 0x2580 && cp <= 0x259F) || (cp >= 0x2800 && cp <= 0x28FF) || findBox(cp) != nullptr;
}

bool rasterizeProceduralGlyph(char32_t cp, int w, int h, std::span<uint8_t> pixels, int stride) {
    // A última linha só precisa de `w` bytes (a imagem pode ser uma célula dentro de um atlas maior).
    const size_t needed = static_cast<size_t>(stride) * static_cast<size_t>(h - 1) + static_cast<size_t>(w);
    if (w <= 0 || h <= 0 || stride < w || pixels.size() < needed) {
        return false;
    }
    const Canvas c{w, h, stride, pixels};
    if (blockElement(cp, c)) return true;
    if (cp >= 0x2800 && cp <= 0x28FF) {
        braille(cp, c);
        return true;
    }
    if (const Arms* a = findBox(cp)) {
        boxDrawing(*a, c);
        return true;
    }
    return false;
}

} // namespace ap
