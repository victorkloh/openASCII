#include "ui/I18n.h"

#include <windows.h>

#include <array>
#include <unordered_map>

namespace ap::i18n {

namespace {

constexpr std::array<LanguageInfo, 2> kLanguages = {{
    {Language::Portuguese, "pt", L"Português"},
    {Language::English, "en", L"English"},
}};

Language g_language = Language::Portuguese;

// Português -> inglês. Gerada a partir dos textos da interface; um texto em falta aparece em
// português (nunca vazio).
const std::unordered_map<std::wstring_view, const wchar_t*>& english() {
    static const std::unordered_map<std::wstring_view, const wchar_t*> table = {
#include "ui/lang/en.inc"
    };
    return table;
}

} // namespace

std::span<const LanguageInfo> languages() { return kLanguages; }

Language fromCode(std::string_view code) {
    for (const auto& l : kLanguages)
        if (code == l.code) return l.id;
    return systemLanguage();
}

const char* codeOf(Language language) {
    for (const auto& l : kLanguages)
        if (l.id == language) return l.code;
    return "pt";
}

Language systemLanguage() {
    // Português de Portugal, do Brasil, etc. -> português; o resto -> inglês.
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_PORTUGUESE ? Language::Portuguese : Language::English;
}

void setLanguage(Language language) { g_language = language; }
Language language() { return g_language; }

const wchar_t* tr(const wchar_t* pt) {
    if (g_language == Language::Portuguese || !pt) return pt;
    const auto& table = english();
    const auto it = table.find(pt);
    return it != table.end() ? it->second : pt;
}

} // namespace ap::i18n
