#pragma once
// O que a interface (menu da bandeja e launcher) pode pedir à aplicação. A App implementa
// isto; a UI não conhece D3D, monitores nem o loop principal.

#include <filesystem>
#include <string>
#include <vector>

#include "core/Color.h"
#include "core/Manifest.h"

namespace ap::ui {

struct WallpaperInfo {
    std::wstring label;   // "ripple" ou "ripple / ember"
    std::wstring base;    // "ripple"
    std::wstring theme;   // "ember" (vazio = o wallpaper em si)
    std::filesystem::path path;
    std::filesystem::path preview;
    bool userOwned = false;
    std::wstring title;       // nome bonito (meta.name)
    std::wstring description;
    std::wstring category;    // vazio = "Outros"
};

struct GlobalSettings {
    bool spanMonitors = true;
    bool interactionAlways = false;
    bool pauseOnMaximized = true;
    bool pauseOnBatterySaver = true;
    bool autostart = false;
    bool autostartFast = false; // arranque rápido (tarefa agendada) em vez da entrada Run
    int maxFps = 60;
    int fps = 0;              // FPS do fundo: 0 = automático
    int otherAppsOnStart = 0; // 0 perguntar, 1 pausar sempre, 2 ignorar
    int otherAppsOnExit = 0;  // 0 perguntar, 1 retomar sempre, 2 deixar pausado
    std::wstring weatherCity; // tempo real dos wallpapers (vazio = nunca vai à internet)
    bool webcamAllowed = false;
    int playlistMinutes = 0;  // 0 = playlist desligada
    bool playlistShuffle = false;
    int playlistCount = 0;    // só leitura: quantos wallpapers estão na playlist
};

class AppControl {
public:
    virtual ~AppControl() = default;

    // Catálogo
    virtual std::vector<WallpaperInfo> wallpapers() = 0;
    virtual std::filesystem::path activePath() const = 0;
    virtual void activate(const std::filesystem::path& path) = 0;
    // O wallpaper que está a ser preparado (compilado) agora, ou vazio. A interface mostra
    // "A preparar" para ninguém pensar que o clique se perdeu.
    virtual std::filesystem::path loadingPath() const = 0;

    // Inspecionar um wallpaper sem o ativar: o manifest com tema (base) e com os ajustes
    // do utilizador por cima (effective).
    virtual bool inspect(const std::filesystem::path& path, Manifest& base, Manifest& effective,
                         std::string& error) = 0;
    virtual Theme overrides(const std::filesystem::path& path) = 0;
    // Aplica ao vivo (se for o ativo). `persist` = gravar no disco (ao largar um slider).
    virtual void setOverrides(const std::filesystem::path& path, const Theme& overrides, bool persist) = 0;

    // Criar e editar
    virtual bool saveAsTheme(const std::filesystem::path& path, const std::wstring& name, std::wstring& error) = 0;
    virtual bool createWallpaper(const std::wstring& name, const std::filesystem::path& templatePath,
                                 std::filesystem::path& created, std::wstring& error) = 0;
    virtual void openInEditor(const std::filesystem::path& wallpaperPath) = 0;
    virtual void openFolder(const std::filesystem::path& path) = 0;
    virtual std::vector<std::wstring> monospaceFonts() = 0;

    // Estado e definições
    virtual bool paused() const = 0;
    virtual void setPaused(bool paused) = 0;
    virtual GlobalSettings settings() const = 0;
    virtual void setSettings(const GlobalSettings& settings) = 0;
    virtual std::wstring lastError() const = 0; // erro do wallpaper ativo (vazio = ok)
    virtual int currentFps() const = 0;         // FPS a que o fundo está a desenhar agora
    virtual std::wstring otherAppsStatus() const = 0; // ex.: "Wallpaper Engine pausado" (vazio = nada)

    // Playlist (trocar de wallpaper sozinho de N em N minutos).
    virtual bool inPlaylist(const std::filesystem::path& path) const = 0;
    virtual void setInPlaylist(const std::filesystem::path& path, bool included) = 0;

    // Primeiros passos: mostrados no launcher na primeira vez que a app arranca.
    virtual bool firstRunPending() const = 0;
    virtual void finishFirstRun() = 0; // concluídos ou saltados: não voltam a aparecer

    // Janelas
    virtual void openLauncher() = 0;
    virtual void quit() = 0;          // pelo utilizador: pode perguntar se retoma outras apps
    virtual void quitImmediately() = 0; // pedido de fora (taskkill): sai já, sem perguntas
    virtual void setModalLoop(bool active) = 0; // uma janela nossa entrou/saiu de um loop modal
};

} // namespace ap::ui
