#include "core/Color.h"

namespace ap {

namespace {

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

std::optional<Rgba8> parseColor(std::string_view text) {
    if (text.empty() || text.front() != '#') {
        return std::nullopt;
    }
    text.remove_prefix(1);
    int d[8] = {};
    if (text.size() != 3 && text.size() != 6 && text.size() != 8) {
        return std::nullopt;
    }
    for (size_t i = 0; i < text.size(); ++i) {
        d[i] = hexDigit(text[i]);
        if (d[i] < 0) return std::nullopt;
    }
    Rgba8 c;
    if (text.size() == 3) {
        // "#abc" = "#aabbcc"
        c.r = static_cast<uint8_t>(d[0] * 17);
        c.g = static_cast<uint8_t>(d[1] * 17);
        c.b = static_cast<uint8_t>(d[2] * 17);
    } else {
        c.r = static_cast<uint8_t>(d[0] * 16 + d[1]);
        c.g = static_cast<uint8_t>(d[2] * 16 + d[3]);
        c.b = static_cast<uint8_t>(d[4] * 16 + d[5]);
        if (text.size() == 8) c.a = static_cast<uint8_t>(d[6] * 16 + d[7]);
    }
    return c;
}

} // namespace ap
