#pragma once
// A aplicação: liga o desktop (WorkerW, monitores, pausas), o render e a tray,
// e corre o loop principal.

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include <wil/resource.h>

#include "app/Tray.h"
#include "core/CommandLine.h"
#include "desktop/PauseManager.h"
#include "desktop/WorkerW.h"
#include "render/Device.h"
#include "render/MonitorSurface.h"

namespace ap::app {

struct Settings {
    int fps = 30;         // por defeito; os wallpapers poderão pedir outro (Fase 3)
    int maxFps = 60;      // limite global
    PauseRules pause;
};

class App {
public:
    explicit App(const Options& options);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int run(); // devolve o código de saída

    // Usado pelo handler de crash para repor o wallpaper mesmo se tudo correr mal.
    static const std::wstring& originalWallpaper();

private:
    using WinEventHook = wil::unique_any<HWINEVENTHOOK, decltype(&::UnhookWinEvent), ::UnhookWinEvent>;
    using PowerNotify = wil::unique_any<HPOWERNOTIFY, decltype(&::UnregisterPowerSettingNotification),
                                        ::UnregisterPowerSettingNotification>;

    bool init();
    void shutdown();
    bool createController();
    static LRESULT CALLBACK controllerProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT onMessage(UINT msg, WPARAM wp, LPARAM lp);
    void onTimer(UINT_PTR id);
    static void CALLBACK onWinEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);

    // Desktop
    bool attachDesktop();
    bool createWindowedSurface();
    std::unique_ptr<render::MonitorSurface> createSurface(const desktop::MonitorInfo& mon, unsigned index);
    void destroySurfaces();
    void syncLayout();
    void scheduleLayout();
    void scheduleReattach(UINT delayMs);

    // Render
    void renderFrame();
    void renderSurfaceNow(render::MonitorSurface& s);
    void armFrameTimer(bool restart);
    void stopFrameTimer();
    bool anyActive() const;
    void handleDeviceLost(HRESULT hr);
    bool recreateSwapChains();
    double now() const;

    // Pausas
    void updatePause();
    bool userPaused() const { return pause_.global.user; }

    Options options_;
    Settings settings_;
    HINSTANCE instance_ = nullptr;
    HWND controller_ = nullptr;
    UINT taskbarCreated_ = 0;
    bool exiting_ = false;

    desktop::WorkerW workerw_;
    desktop::PauseManager pause_;
    render::Device device_;
    std::vector<std::unique_ptr<render::MonitorSurface>> surfaces_;
    Tray tray_;
    HICON trayIcon_ = nullptr;

    wil::unique_handle frameTimer_;
    bool frameTimerArmed_ = false;
    LONGLONG qpcFrequency_ = 1;
    LONGLONG qpcStart_ = 0;
    LONGLONG nextFrame_ = 0;

    std::vector<WinEventHook> winEventHooks_;
    PowerNotify batterySaverNotify_;
    bool sessionNotify_ = false;
    int reattachAttempts_ = 0;
};

} // namespace ap::app
