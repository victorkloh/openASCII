#include "core/Geometry.h"

#include <algorithm>

namespace ap {

Rect boundingBox(std::span<const Rect> rects) {
    if (rects.empty()) {
        return {};
    }
    Rect box = rects.front();
    for (const Rect& r : rects.subspan(1)) {
        box.left = std::min(box.left, r.left);
        box.top = std::min(box.top, r.top);
        box.right = std::max(box.right, r.right);
        box.bottom = std::max(box.bottom, r.bottom);
    }
    return box;
}

int scaleByDpi(int logicalPx, unsigned dpi) {
    // Arredondamento simétrico: soma metade do divisor com o sinal certo.
    const long long num = static_cast<long long>(logicalPx) * dpi;
    const long long half = num >= 0 ? 48 : -48;
    return static_cast<int>((num + half) / 96);
}

} // namespace ap
