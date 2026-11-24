#pragma once
// Configuração global em %APPDATA%\OpenASCII\config.toml.
// Por agora só o essencial; a config completa (wallpaper por monitor, overrides de
// params...) chega na Fase 5.

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ap::app {

struct Config {
    std::string wallpaper = "ripple";  // nome, ou caminho relativo a uma pasta de wallpapers
    std::string interaction = "desktop_only"; // "desktop_only" | "always"
    int maxFps = 60;
    bool pauseOnMaximized = true;
    bool pauseOnBatterySaver = true;
    bool spanMonitors = true; // um só efeito estendido por todos os monitores
    int fps = 60;             // FPS do fundo: 0 = automático (o que cada wallpaper pede); por defeito 60
    // Outras apps de wallpaper (Wallpaper Engine, Lively):
    std::string otherAppsOnStart = "ask"; // "ask" | "pause" | "ignore"
    std::string otherAppsOnExit = "ask";  // "ask" | "resume" | "keep"
    // Entradas dos wallpapers:
    std::string weatherCity;    // tempo real (Open-Meteo); vazio = nunca contacta a internet
    bool webcamAllowed = false; // o utilizador autorizou os wallpapers que usam a webcam
    // Playlist: trocar de wallpaper sozinho.
    std::vector<std::string> playlist; // nomes (ou caminhos) pela ordem
    int playlistMinutes = 0;           // 0 = desligada
    bool playlistShuffle = false;
    // Wallpaper por monitor: identificador do monitor -> wallpaper. Os que não estão aqui usam
    // o `wallpaper` geral.
    std::map<std::string, std::string> monitorWallpapers;
    // Idioma da interface ("pt", "en"...). Vazio = ainda não escolhido: a app pergunta ao arrancar.
    std::string language;
    // Os "primeiros passos" já foram vistos (concluídos ou saltados). Em falta = mostrar.
    bool firstRunDone = false;

    static std::filesystem::path path();
    static Config load(); // ficheiro em falta ou inválido = valores por defeito (e um aviso no log)
    bool save() const;
};

} // namespace ap::app
