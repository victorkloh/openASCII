#pragma once
// Cores RGBA de 8 bits por canal e parsing de "#rgb", "#rrggbb", "#rrggbbaa".

#include <cstdint>
#include <optional>
#include <string_view>

namespace ap {

struct Rgba8 {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    // Empacotado como a GPU o lê (R no byte menos significativo).
    constexpr uint32_t packed() const {
        return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
    }
    friend constexpr bool operator==(const Rgba8&, const Rgba8&) = default;
};

std::optional<Rgba8> parseColor(std::string_view text);

} // namespace ap
