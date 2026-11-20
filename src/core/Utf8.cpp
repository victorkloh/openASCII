#include "core/Utf8.h"

namespace ap {

std::u32string decodeUtf8(std::string_view s, bool* ok) {
    std::u32string out;
    bool good = true;
    size_t i = 0;
    while (i < s.size()) {
        const auto b0 = static_cast<unsigned char>(s[i]);
        int len = 0;
        char32_t cp = 0;
        if (b0 < 0x80) {
            len = 1, cp = b0;
        } else if ((b0 & 0xE0) == 0xC0) {
            len = 2, cp = b0 & 0x1F;
        } else if ((b0 & 0xF0) == 0xE0) {
            len = 3, cp = b0 & 0x0F;
        } else if ((b0 & 0xF8) == 0xF0) {
            len = 4, cp = b0 & 0x07;
        } else {
            out += U'�';
            good = false;
            ++i;
            continue;
        }
        if (i + len > s.size()) {
            out += U'�';
            good = false;
            break;
        }
        bool valid = true;
        for (int k = 1; k < len; ++k) {
            const auto b = static_cast<unsigned char>(s[i + k]);
            if ((b & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (b & 0x3F);
        }
        // Rejeitar codificações longas demais, surrogates e valores fora do Unicode.
        static constexpr char32_t minForLen[] = {0, 0, 0x80, 0x800, 0x10000};
        if (!valid || cp < minForLen[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            out += U'�';
            good = false;
            i += valid ? len : 1;
            continue;
        }
        out += cp;
        i += len;
    }
    if (ok) *ok = good;
    return out;
}

std::string encodeUtf8(char32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
}

std::string encodeUtf8(std::u32string_view text) {
    std::string out;
    for (char32_t c : text) out += encodeUtf8(c);
    return out;
}

} // namespace ap
