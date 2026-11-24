#pragma once
// Traduções da interface. A chave é o próprio texto em português (o idioma em que a interface
// foi escrita): tr(L"Aplicar") devolve "Apply" em inglês, ou o texto tal como está se não houver
// tradução (ou se o idioma for o português). Para acrescentar um idioma: um valor em Language,
// uma entrada em languages() e uma tabela como a de lang/en.inc.

#include <format>
#include <span>
#include <string>
#include <string_view>

namespace ap::i18n {

enum class Language { Portuguese, English };

struct LanguageInfo {
    Language id;
    const char* code;    // gravado na config: "pt", "en"
    const wchar_t* name; // no próprio idioma: "Português", "English"
};

std::span<const LanguageInfo> languages();
Language fromCode(std::string_view code); // desconhecido = o do sistema
const char* codeOf(Language language);
Language systemLanguage(); // o idioma do Windows, se o tivermos; senão inglês

void setLanguage(Language language);
Language language();

// O texto no idioma atual (ou `pt` se não houver tradução).
const wchar_t* tr(const wchar_t* pt);

// tr + std::format: trf(L"{} variantes", n).
template <class... Args>
std::wstring trf(const wchar_t* pt, const Args&... args) {
    return std::vformat(tr(pt), std::make_wformat_args(args...));
}

} // namespace ap::i18n
