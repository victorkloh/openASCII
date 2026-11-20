#pragma once
// Conversão UTF-8 <-> code points (o charset dos wallpapers vem em UTF-8 do TOML).

#include <string>
#include <string_view>

namespace ap {

// Caracteres inválidos viram U+FFFD. `ok` (opcional) fica false se houve algum.
std::u32string decodeUtf8(std::string_view text, bool* ok = nullptr);
std::string encodeUtf8(char32_t cp);
std::string encodeUtf8(std::u32string_view text);

} // namespace ap
