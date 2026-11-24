#pragma once
// A aplicação: liga o desktop (WorkerW, monitores, pausas), o render ASCII, o wallpaper
// ativo, o rato, a bandeja e o launcher, e corre o loop principal.

#include <windows.h>
#include <dwrite_2.h>

#include <algorithm>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <wil/resource.h>

#include "app/Config.h"
#include "app/Tray.h"
#include "core/CommandLine.h"
#include "desktop/Autostart.h"
#include "desktop/OtherWallpaperApps.h"
#include "desktop/PauseManager.h"
#include "desktop/WorkerW.h"
#include "input/MouseInput.h"
#include "inputs/InputHub.h"
#include "render/Device.h"
#include "render/GpuTimer.h"
#include "render/GridRenderer.h"
#include "render/MonitorSurface.h"
#include "ui/AppControl.h"
#include "wallpaper/DirectoryWatcher.h"
#include "wallpaper/ShaderBackend.h"
#include "wallpaper/Wallpaper.h"

namespace ap::ui {
class Gfx;
class TrayFlyout;
class Launcher;
class ConfirmDialog;
} // namespace ap::ui

namespace ap::app {

// Mensagem registada que uma segunda instância envia à primeira para mostrar o launcher.
inline constexpr wchar_t kOpenLauncherMessage[] = L"OpenASCII.OpenLauncher";

struct Settings {
    int fps = 30;    // vem do wallpaper.toml
    int maxFps = 60; // limite global (config.toml)
    PauseRules pause;
};

class App final : public ui::AppControl {
public:
    explicit App(const Options& options);
    ~App() override;
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int run(); // devolve o código de saída

    // Usado pelo handler de crash para repor o wallpaper mesmo se tudo correr mal.
    static const std::wstring& originalWallpaper();

    // ---- ui::AppControl ----
    std::vector<ui::WallpaperInfo> wallpapers() override;
    std::filesystem::path activePath() const override { return wallpaperPath_; }
    void activate(const std::filesystem::path& path) override;
    bool inspect(const std::filesystem::path& path, Manifest& base, Manifest& effective, std::string& error) override;
    Theme overrides(const std::filesystem::path& path) override;
    void setOverrides(const std::filesystem::path& path, const Theme& overrides, bool persist) override;
    bool saveAsTheme(const std::filesystem::path& path, const std::wstring& name, std::wstring& error) override;
    bool createWallpaper(const std::wstring& name, const std::filesystem::path& templatePath,
                         std::filesystem::path& created, std::wstring& error) override;
    void openInEditor(const std::filesystem::path& wallpaperPath) override;
    void openFolder(const std::filesystem::path& path) override;
    std::vector<std::wstring> monospaceFonts() override;
    bool paused() const override { return pause_.global.user; }
    void setPaused(bool paused) override;
    ui::GlobalSettings settings() const override;
    void setSettings(const ui::GlobalSettings& settings) override;
    std::wstring lastError() const override;
    int currentFps() const override { return std::clamp(settings_.fps, 1, 240); }
    std::wstring otherAppsStatus() const override;
    bool inPlaylist(const std::filesystem::path& path) const override;
    void setInPlaylist(const std::filesystem::path& path, bool included) override;
    void openLauncher() override;
    void quit() override; // pode perguntar se retoma o Wallpaper Engine/Lively antes de sair
    void quitImmediately() override { PostQuitMessage(0); }
    std::filesystem::path loadingPath() const override { return queuedLoad_.empty() ? loadingPath_ : queuedLoad_; }
    void setModalLoop(bool active) override;
    bool firstRunPending() const override { return !config_.firstRunDone; }
    void finishFirstRun() override;

private:
    using WinEventHook = wil::unique_any<HWINEVENTHOOK, decltype(&::UnhookWinEvent), ::UnhookWinEvent>;
    using PowerNotify = wil::unique_any<HPOWERNOTIFY, decltype(&::UnregisterPowerSettingNotification),
                                        ::UnregisterPowerSettingNotification>;

    // Resultado de carregar + compilar um wallpaper (pode correr num thread de trabalho).
    struct LoadJob {
        std::filesystem::path path;
        wallpaper::LoadResult load;
        render::CompiledShader shader;
    };
    static LoadJob runLoadJob(std::filesystem::path path);

    bool init();
    void shutdown();
    bool createController();
    static LRESULT CALLBACK controllerProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT onMessage(UINT msg, WPARAM wp, LPARAM lp);
    void onTimer(UINT_PTR id);
    static void CALLBACK onWinEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    void showTrayFlyout();

    // Wallpaper
    void loadWallpaperSync(const std::filesystem::path& path);
    void loadWallpaperAsync(const std::filesystem::path& path, bool force = false); // force: hot reload
    void applyLoadJob(LoadJob job);
    void onManifestChanged(); // fps, atlas, canvas
    void configureInputs();   // [inputs] do wallpaper ativo: janelas, áudio, imagem, texto...
    void schedulePlaylist();  // (re)arma o temporizador da playlist
    void advancePlaylist();   // passa ao wallpaper seguinte da playlist
    const render::GlyphAtlas* referenceAtlas() const;
    int effectiveFps() const; // FPS do fundo: o fixo das definições, ou o do wallpaper

    // Outras apps de wallpaper
    void checkOtherApps();
    void suspendApps(const std::vector<desktop::OtherWallpaperApp>& apps);
    void resumeSuspendedApps();
    std::filesystem::path initialWallpaperPath() const;

    // Desktop
    bool attachDesktop();
    bool createWindowedSurface();
    std::unique_ptr<render::MonitorSurface> createSurface(const desktop::MonitorInfo& mon, unsigned index);
    void destroySurfaces();
    void syncLayout();
    void scheduleLayout();
    void scheduleReattach(UINT delayMs);
    void checkDesktopOrder(); // repõe as nossas janelas por cima do fundo do Windows
    static void CALLBACK onShellWinEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);

    // Render
    HRESULT prepareSurface(render::MonitorSurface& s); // atlas + grelha para o DPI e wallpaper atuais
    void prepareAllSurfaces();
    void rebuildCanvases();                           // um canvas estendido, ou um por monitor
    render::MonitorSurface* referenceSurface(const render::CellCanvas& canvas) const;
    void renderFrame();
    void renderAllNow();                              // um frame já, em todos (mesmo em pausa)
    void computeCanvas(render::CellCanvas& canvas, double time, double dt);
    HRESULT drawSurface(render::MonitorSurface& s, bool present = true);
    HRESULT presentSurface(render::MonitorSurface& s);
    wallpaper::FrameConstants makeFrame(const render::CellCanvas& canvas, const render::MonitorSurface& ref,
                                        double time, double dt) const;
    void drawError(render::CellCanvas& canvas, const render::MonitorSurface& ref);
    void armFrameTimer(bool restart);
    void renderIfDue(); // desenha um frame se o timer já disparou (sem esperar pelo loop)
    void onModalFrame(); // WM_APP_MODAL_FRAME: a espera do threadpool viu o timer disparar
    void stopFrameTimer();
    bool anyActive() const;
    void handleDeviceLost(HRESULT hr);
    bool recreateDeviceResources();
    double now() const;
    double qpcToSeconds(LONGLONG qpc) const;

    // Pausas
    void updatePause();

    // Bench
    void finishBench();

    Options options_;
    Settings settings_;
    Config config_;
    HINSTANCE instance_ = nullptr;
    HWND controller_ = nullptr;
    UINT taskbarCreated_ = 0;
    UINT openLauncherMsg_ = 0; // kOpenLauncherMessage
    bool exiting_ = false;

    desktop::WorkerW workerw_;
    desktop::PauseManager pause_;
    render::Device device_;
    render::GridPipeline pipeline_;
    Microsoft::WRL::ComPtr<IDWriteFactory2> dwrite_;
    std::vector<std::unique_ptr<render::MonitorSurface>> surfaces_;
    std::vector<std::unique_ptr<render::CellCanvas>> canvases_;
    Tray tray_;
    HICON trayIcon_ = nullptr;
    input::MouseInput mouse_;
    std::unique_ptr<ui::Gfx> gfx_;
    std::unique_ptr<ui::TrayFlyout> flyout_;
    std::unique_ptr<ui::Launcher> launcher_;
    std::unique_ptr<ui::ConfirmDialog> dialog_;
    std::vector<desktop::OtherWallpaperApp> suspendedApps_; // o que pausámos (para retomar)
    bool quitting_ = false;
    bool otherAppsChecked_ = false; // Wallpaper Engine/Lively: verificado uma vez, com um wallpaper à vista
    int modalLoops_ = 0;          // janelas nossas em loop modal (arrastar/redimensionar)
    PTP_WAIT modalWait_ = nullptr; // durante um loop modal: espera pelo timer de frames
    desktop::AutostartMode autostartCached_ = desktop::AutostartMode::Off; // lido só ao arrancar e ao mudar

    // Wallpaper ativo
    std::filesystem::path wallpaperPath_; // pasta ou tema pedido
    wallpaper::LoadedWallpaper wallpaper_;
    bool hasWallpaper_ = false;           // wallpaper_ tem um manifest utilizável
    wallpaper::ShaderProgram program_;
    std::string errorText_;               // mostrado num canto do ecrã
    wallpaper::DirectoryWatcher watcher_;
    std::future<LoadJob> pendingLoad_;
    std::filesystem::path loadingPath_; // a compilar agora (vazio = nada)
    std::filesystem::path queuedLoad_;  // pedido feito durante uma compilação (o mais recente)
    bool queuedForce_ = false;          // o pedido em fila é um hot reload (corre mesmo se for o atual)
    std::vector<std::wstring> fontCache_;
    inputs::InputHub inputs_;
    bool webcamAllowedSession_ = false; // "Ligar a webcam" sem "lembrar": só até fechar a app

    // Tempo e rato
    wil::unique_handle frameTimer_;
    bool frameTimerArmed_ = false;
    LONGLONG qpcFrequency_ = 1;
    LONGLONG qpcStart_ = 0;
    LONGLONG nextFrame_ = 0;
    double lastFrameTime_ = 0.0;
    uint32_t frameIndex_ = 0;
    POINT lastCursor_{};
    float mouseVelX_ = 0.0f, mouseVelY_ = 0.0f; // px/s suavizados
    // Por canvas: a "presença" suave do rato e a última posição em que esteve no fundo.
    struct MouseTrack {
        float presence = 0.0f;
        POINT last{};
        bool inside = false;
        uint32_t seenClick = 0;    // último clique (MouseInput::clickSerial) já visto por este canvas
        bool clickPending = false; // clique novo dentro deste canvas, ainda não entregue ao shader
    };
    std::unordered_map<const render::CellCanvas*, MouseTrack> mouseTracks_;

    std::vector<WinEventHook> winEventHooks_;
    WinEventHook shellOrderHook_;     // filhos da anfitriã, no processo do Explorer
    bool orderCheckPending_ = false;  // WM_APP_CHECK_ORDER já está na fila
    double orderFixWindowStart_ = 0.0;
    double lastOrderCheck_ = -1.0;    // renderFrame verifica a ordem Z no máximo a cada 50 ms
    int orderFixes_ = 0;              // correções da ordem Z na janela de 10 s atual
    PowerNotify batterySaverNotify_;
    bool sessionNotify_ = false;
    int reattachAttempts_ = 0;

    // --screenshot / --ui-screenshot
    bool captureRequested_ = false;

    // --bench
    bool benchmarking_ = false;
    std::vector<double> frameTimesMs_;
    LONGLONG benchStartQpc_ = 0;
    ULONGLONG benchStartCpu100ns_ = 0;

    // Diagnóstico de desempenho: o que o loop faz (acordar, mensagens, engasgos, Present).
    struct PerfCounters {
        uint64_t wakeups = 0, messages = 0, frames = 0, hitches = 0;
        double maxGapMs = 0, maxRenderMs = 0, maxPresentMs = 0, sumPresentMs = 0, skippedPresents = 0;
        LONGLONG lastFrameQpc = 0, windowStart = 0;
    };
    PerfCounters perf_;      // janela de 5 s (log com --verbose)
    PerfCounters benchPerf_; // o --bench inteiro
    render::GpuTimer gpuTimer_;
    std::vector<double> gpuMs_;
    void logPerfWindow();
};

} // namespace ap::app
