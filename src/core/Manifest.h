#pragma once
// O manifest de um wallpaper (wallpaper.toml) e os temas que herdam dele.
// Ver PROJECT.md, secção 5. Lógica pura, testada em tests/test_manifest.cpp.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Color.h"

namespace ap {

inline constexpr int kMaxPaletteColors = 8; // limite do constant buffer da GPU
inline constexpr int kMaxParams = 16;

struct ParamSpec {
    std::string name;
    float defaultValue = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
    float value = 0.0f; // valor atual (default, depois overrides do tema/config), sempre dentro de [min,max]
};

float clampParam(const ParamSpec& p, float v);

enum class WallpaperMode { Shader, Script };

struct Manifest {
    // [meta]
    std::string name;
    std::string author;
    std::string version;
    std::string description;
    // [render]
    WallpaperMode mode = WallpaperMode::Shader;
    std::string entry = "main.hlsl";
    int fps = 30;
    int cell = 10;      // altura da célula em px lógicos = RESOLUÇÃO da grelha
    float scale = 10.0f; // px lógicos por unidade de mundo = ESCALA do efeito (independente de `cell`)
    std::string font = "Cascadia Mono";
    int fontWeight = 400;
    std::u32string charset = U" .:-=+*#%@"; // rampa: value 0..1 escolhe daqui
    std::u32string glyphs;                  // caracteres extra (sprites), via glyph(k) no shader
    // [colors]
    Rgba8 background{5, 7, 13, 255};
    std::vector<Rgba8> palette;
    // [params], pela ordem do ficheiro (a ordem define o índice na GPU)
    std::vector<ParamSpec> params;

    const ParamSpec* findParam(std::string_view name) const;
    ParamSpec* findParam(std::string_view name);
};

struct ManifestResult {
    Manifest manifest;
    std::string error; // vazio = ok; inclui a linha quando o erro é de sintaxe
    std::vector<std::string> warnings;
};

ManifestResult parseManifest(std::string_view tomlText);

// Um tema: herda de um wallpaper (`base`) e só altera valores, sem código.
struct Theme {
    std::string base;
    std::optional<std::string> font;
    std::optional<int> fontWeight;
    std::optional<int> cell;
    std::optional<float> scale;
    std::optional<int> fps;
    std::optional<std::u32string> charset;
    std::optional<Rgba8> background;
    std::optional<std::vector<Rgba8>> palette;
    std::vector<std::pair<std::string, float>> params;
};

struct ThemeResult {
    Theme theme;
    std::string error;
};

// `requireBase = false` para os ajustes do utilizador (que não precisam de `base`).
ThemeResult parseTheme(std::string_view tomlText, bool requireBase = true);

// Aplica o tema ao manifest. Devolve avisos (ex.: parâmetro que o wallpaper não tem).
std::vector<std::string> applyTheme(Manifest& manifest, const Theme& theme);

// O inverso de parseTheme: gera o .toml de um tema (para "Guardar como tema" e para os
// ajustes do utilizador). parseTheme(themeToToml(t)) devolve t.
std::string themeToToml(const Theme& theme, std::string_view headerComment = {});

bool themeIsEmpty(const Theme& theme);

// Um ficheiro .toml é um tema se tiver a chave `base` no topo.
bool looksLikeTheme(std::string_view tomlText);

} // namespace ap
