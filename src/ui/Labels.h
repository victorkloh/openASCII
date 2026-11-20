#pragma once
// Nomes legíveis para os parâmetros dos wallpapers ("speed" -> "Velocidade").

#include <string>
#include <string_view>

namespace ap::ui {

inline std::wstring paramLabel(std::string_view name) {
    struct Known {
        std::string_view key;
        const wchar_t* label;
    };
    static constexpr Known known[] = {
        {"speed", L"Velocidade"},     {"damping", L"Duração das ondas"}, {"strength", L"Força do rato"},
        {"rain", L"Chuva"},           {"contrast", L"Contraste"},        {"density", L"Densidade"},
        {"radius", L"Raio do desvio"}, {"scale", L"Escala do relevo"},   {"parallax", L"Parallax"},
        {"clouds", L"Nuvens"},        {"trails", L"Rastos"},             {"animals", L"Animais"},
        {"birds", L"Pássaros"},       {"wind", L"Vento"},                {"daylength", L"Duração do dia"},
        {"storm", L"Tempestade"},     {"water", L"Nível da água"},
    };
    for (const Known& k : known) {
        if (k.key == name) return k.label;
    }
    std::wstring out;
    bool upper = true;
    for (char c : name) {
        if (c == '_' || c == '-') {
            out += L' ';
            continue;
        }
        out += upper && c >= 'a' && c <= 'z' ? static_cast<wchar_t>(c - 'a' + 'A') : static_cast<wchar_t>(c);
        upper = false;
    }
    return out;
}

} // namespace ap::ui
