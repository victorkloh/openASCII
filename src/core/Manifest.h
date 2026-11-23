#pragma once
// O manifest de um wallpaper (wallpaper.toml) e os temas que herdam dele.
// Ver PROJECT.md, secção 5. Lógica pura, testada em tests/test_manifest.cpp.

#include <array>
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

// Pós-processamento aplicado ao desenhar as células em píxeis (secção [post]), 0..1 cada.
struct PostFx {
    float glow = 0;       // halo à volta dos caracteres claros (bloom)
    float scanlines = 0;  // linhas horizontais de monitor CRT
    float curvature = 0;  // ecrã abaulado de CRT
    float vignette = 0;   // escurece os cantos
    float aberration = 0; // aberração cromática (vermelho e azul desalinhados)
    float grain = 0;      // grão de filme
};
inline constexpr std::array<std::string_view, 6> kPostFxNames = {"glow",     "scanlines",  "curvature",
                                                                 "vignette", "aberration", "grain"};
float& postFxValue(PostFx& fx, size_t index);
float postFxValue(const PostFx& fx, size_t index);
bool postFxActive(const PostFx& fx);

struct Manifest {
    // [meta]
    std::string name;
    std::string author;
    std::string version;
    std::string description;
    std::string category; // agrupa no launcher: "Paisagens", "Simulações", "3D e espaço"...
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
    // true = SÓ os caracteres do charset aparecem: os sprites (glyph(k)) e o texto (asciiGlyph)
    // são trocados pelo caractere do charset com a densidade de tinta mais parecida. É o que o
    // launcher liga quando escolhes um conjunto ("Usar só estes caracteres").
    bool charsetOnly = false;
    int substeps = 1;                       // dispatches por frame (simulações rápidas: fluido, areia)
    // [inputs]: o que o wallpaper usa (só se lê/captura o que é pedido)
    bool useWindows = false; // janelas abertas como obstáculos
    bool useIcons = false;   // ícones do ambiente de trabalho como obstáculos
    bool useAudio = false;   // áudio do sistema (espectro)
    bool useSystem = false;  // CPU/RAM
    bool useWeather = false; // tempo real da cidade configurada
    std::string source;      // imagem, GIF, vídeo (relativo à pasta) ou "webcam"
    std::string text;        // texto literal, "file:<f>", "folder:<pasta>" ou "lines:<f>" (uma linha de cada vez)
    float textInterval = 15; // "lines:": segundos por linha
    // [post]
    PostFx post;
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
    std::optional<bool> charsetOnly; // charset_only
    std::optional<Rgba8> background;
    std::optional<std::vector<Rgba8>> palette;
    std::optional<std::string> source; // trocar a imagem/vídeo (ex.: a foto escolhida pelo utilizador)
    std::optional<std::string> text;   // trocar o texto
    std::array<std::optional<float>, kPostFxNames.size()> post; // [post]
    // [inputs]: um tema pode ligar entradas que o wallpaper sabe usar (ex.: "beat" liga o áudio).
    std::optional<bool> useWindows, useIcons, useAudio, useSystem, useWeather;
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
