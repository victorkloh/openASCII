#pragma once
// Encontrar e carregar wallpapers (pastas com wallpaper.toml) e temas (.toml com `base`).

#include <filesystem>
#include <string>
#include <vector>

#include "core/Manifest.h"

namespace ap::wallpaper {

namespace fs = std::filesystem;

struct LoadedWallpaper {
    fs::path dir;         // pasta do wallpaper (base, se for um tema)
    fs::path themeFile;   // vazio se não for um tema
    fs::path watchDir;    // pasta a vigiar para hot reload
    std::string label;    // "ripple" ou "ripple / ember"
    Manifest baseManifest; // com o tema aplicado, SEM os ajustes do utilizador
    Manifest manifest;     // o que está a correr: base + ajustes do utilizador
    std::string source;   // conteúdo do ficheiro `entry`
    std::vector<std::string> warnings;
};

struct LoadResult {
    LoadedWallpaper wallpaper;
    std::string error; // vazio = ok
};

// Pastas onde procuramos wallpapers: ao lado do .exe e em %APPDATA%\OpenASCII\wallpapers.
std::vector<fs::path> wallpaperRoots();

// "ripple" -> <raiz>/ripple; um caminho para pasta ou .toml é usado tal como está.
fs::path resolveWallpaper(const std::wstring& nameOrPath);

// Aceita uma pasta (com wallpaper.toml) ou um ficheiro de tema .toml.
LoadResult loadWallpaper(const fs::path& folderOrTheme);

struct CatalogEntry {
    std::string label; // para o menu
    fs::path path;     // pasta ou ficheiro de tema
    std::string base;  // nome da pasta do wallpaper
    std::string theme; // nome do tema (vazio = o wallpaper em si)
    fs::path preview;  // preview.png (pode não existir)
    bool userOwned = false; // está em %APPDATA% (o utilizador pode editar)
    std::string title;       // meta.name
    std::string description; // meta.description
    std::string category;    // meta.category (vazio = "Outros")
};

// Chave estável e portável de um wallpaper: relativa à pasta de wallpapers quando possível
// ("ripple", "ripple/themes/ember.toml"); usada na config e nos ajustes do utilizador.
std::string wallpaperKey(const fs::path& path);

// Ficheiro com os ajustes do utilizador para um wallpaper (cores, resolução, params...).
fs::path overridesFile(const std::string& key);
Theme loadOverrides(const std::string& key);
bool saveOverrides(const std::string& key, const Theme& overrides);

// Pasta dos wallpapers do utilizador (%APPDATA%\OpenASCII\wallpapers), criada se preciso.
fs::path userWallpaperDir();

// Transforma um nome em nome de pasta: "Meu Fogo!" -> "meu-fogo".
std::string slugify(std::string_view name);

// Todos os wallpapers e os seus temas (themes/*.toml), para o menu da tray.
std::vector<CatalogEntry> listWallpapers();

std::wstring toWide(std::string_view utf8);
std::string toUtf8Path(const fs::path& p);

} // namespace ap::wallpaper
