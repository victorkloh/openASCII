#include "core/Grid.h"

#include <algorithm>
#include <cmath>

namespace ap {

GridLayout computeGrid(int widthPx, int heightPx, int cellW, int cellH) {
    GridLayout g;
    g.cellW = std::max(1, cellW);
    g.cellH = std::max(1, cellH);
    g.cols = std::max(0, widthPx) / g.cellW;
    g.rows = std::max(0, heightPx) / g.cellH;
    g.originX = (std::max(0, widthPx) - g.cols * g.cellW) / 2;
    g.originY = (std::max(0, heightPx) - g.rows * g.cellH) / 2;
    return g;
}

int valueToCharIndex(float value, int charsetSize) {
    const int n = std::max(1, charsetSize);
    if (!(value > 0.0f)) return 0; // também apanha NaN
    const float v = std::min(value, 1.0f);
    return std::min(static_cast<int>(v * static_cast<float>(n)), n - 1);
}

} // namespace ap
