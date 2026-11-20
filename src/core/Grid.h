#pragma once
// Cálculo da grelha de células e mapeamento valor -> caractere.

namespace ap {

struct GridLayout {
    int cols = 0;
    int rows = 0;
    int cellW = 1; // px físicos
    int cellH = 1;
    int originX = 0; // margem à esquerda/topo quando a grelha não divide o ecrã por igual
    int originY = 0;

    int cellCount() const { return cols * rows; }
    friend constexpr bool operator==(const GridLayout&, const GridLayout&) = default;
};

// Quantas células cabem e onde começam, centrando a grelha (a margem fica com a cor de fundo).
GridLayout computeGrid(int widthPx, int heightPx, int cellW, int cellH);

// value em [0,1] -> índice no charset. Igual ao que o compute shader faz na GPU.
int valueToCharIndex(float value, int charsetSize);

} // namespace ap
