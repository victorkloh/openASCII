#pragma once
// Geometria básica em píxeis físicos. Sem dependências de Win32 para poder ser testada.

#include <span>

namespace ap {

struct Point {
    int x = 0;
    int y = 0;
    friend constexpr bool operator==(const Point&, const Point&) = default;
};

struct Rect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    constexpr int width() const { return right - left; }
    constexpr int height() const { return bottom - top; }
    constexpr bool empty() const { return width() <= 0 || height() <= 0; }
    friend constexpr bool operator==(const Rect&, const Rect&) = default;
};

constexpr Rect offset(const Rect& r, int dx, int dy) {
    return {r.left + dx, r.top + dy, r.right + dx, r.bottom + dy};
}

// Converte um retângulo em coordenadas de ecrã para as coordenadas cliente de uma
// janela-pai cuja origem cliente está em `parentOrigin` (também em coordenadas de ecrã).
//
// Exemplo: o WorkerW cobre o ecrã virtual inteiro, cuja origem pode ser negativa
// (monitor à esquerda do principal). Um monitor em (-1920, 0) fica em (0, 0) no WorkerW.
constexpr Rect toParentSpace(const Rect& screen, Point parentOrigin) {
    return offset(screen, -parentOrigin.x, -parentOrigin.y);
}

// `outer` cobre `inner` por completo?
constexpr bool covers(const Rect& outer, const Rect& inner) {
    return outer.left <= inner.left && outer.top <= inner.top &&
           outer.right >= inner.right && outer.bottom >= inner.bottom;
}

constexpr Rect intersect(const Rect& a, const Rect& b) {
    Rect r{a.left > b.left ? a.left : b.left, a.top > b.top ? a.top : b.top,
           a.right < b.right ? a.right : b.right, a.bottom < b.bottom ? a.bottom : b.bottom};
    return r.empty() ? Rect{} : r;
}

// Menor retângulo que contém todos os retângulos (ex.: o ecrã virtual a partir dos monitores).
Rect boundingBox(std::span<const Rect> rects);

// Converte px lógicos (a 96 DPI = 100%) para px físicos, arredondando ao inteiro mais próximo.
// 6 px a 144 DPI (150%) = 9 px. Usamos inteiros para nunca termos meio píxel (blur/costuras).
int scaleByDpi(int logicalPx, unsigned dpi);

} // namespace ap
