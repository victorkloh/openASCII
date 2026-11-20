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
    Manifest manifest;    // já com o tema aplicado
    std::string source;   // conteúdo do ficheiro `entry`
    std::vector<std::string> warnings;
};

struct LoadResult {
    LoadedWallpaper wallpaper;
    std::string error; // vazio = ok
};

// Pastas onde procuramos wallpapers: ao lado do .exe e em %APPDATA%\AsciiPaper\wallpapers.
std::vector<fs::path> wallpaperRoots();

// "ripple" -> <raiz>/ripple; um caminho para pasta ou .toml é usado tal como está.
fs::path resolveWallpaper(const std::wstring& nameOrPath);

// Aceita uma pasta (com wallpaper.toml) ou um ficheiro de tema .toml.
LoadResult loadWallpaper(const fs::path& folderOrTheme);

struct CatalogEntry {
    std::string label; // para o menu
    fs::path path;     // pasta ou ficheiro de tema
};

// Todos os wallpapers e os seus temas (themes/*.toml), para o menu da tray.
std::vector<CatalogEntry> listWallpapers();

std::wstring toWide(std::string_view utf8);
std::string toUtf8Path(const fs::path& p);

} // namespace ap::wallpaper
