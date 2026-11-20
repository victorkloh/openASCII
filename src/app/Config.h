#pragma once
// Configuração global em %APPDATA%\AsciiPaper\config.toml.
// Por agora só o essencial; a config completa (wallpaper por monitor, overrides de
// params...) chega na Fase 5.

#include <filesystem>
#include <string>

namespace ap::app {

struct Config {
    std::string wallpaper = "ripple";  // nome, ou caminho relativo a uma pasta de wallpapers
    std::string interaction = "desktop_only"; // "desktop_only" | "always"
    int maxFps = 60;
    bool pauseOnMaximized = true;
    bool pauseOnBatterySaver = true;

    static std::filesystem::path path();
    static Config load(); // ficheiro em falta ou inválido = valores por defeito (e um aviso no log)
    bool save() const;
};

} // namespace ap::app
