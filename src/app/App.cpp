#include "app/App.h"

#include "core/Canvas.h"
#include "core/Utf8.h"
#include "desktop/MonitorManager.h"
#include "desktop/Wallpaper.h"
#include "render/Screenshot.h"
#include "resource.h"
#include "ui/Gfx.h"
#include "ui/Launcher.h"
#include "ui/TrayFlyout.h"
#include "util/Log.h"
#include "util/Win32.h"
#include "version.h"

#include <dbt.h>
#include <dwrite_2.h>
#include <psapi.h>
#include <shellapi.h>
#include <wtsapi32.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <regex>
#include <set>
#include <sstream>

namespace ap::app {

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kControllerClass[] = L"AsciiPaperController";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr UINT WM_APP_TRAY = WM_APP + 10;
constexpr UINT WM_APP_LOAD_DONE = WM_APP + 11; // terminou a compilação em segundo plano

enum TimerId : UINT_PTR {
    TIMER_LAYOUT = 1,      // mudança de monitores/DPI (com debounce)
    TIMER_LAYOUT_LATE,     // segunda passagem: o Explorer pode redimensionar o WorkerW depois de nós
    TIMER_PAUSE_CHECK,     // reavaliar pausas de janela (1 Hz, só quando é preciso)
    TIMER_PAUSE_DEBOUNCE,  // depois de um evento de janela
    TIMER_REATTACH,        // voltar a ligar ao Explorer
    TIMER_DEVICE_RETRY,    // recriar o dispositivo D3D depois de um device lost
    TIMER_HOT_RELOAD,      // debounce: os editores gravam em vários passos
    TIMER_BENCH,           // fim do --bench
    TIMER_SCREENSHOT,      // --screenshot: gravar o próximo frame
};

App* g_app = nullptr;
std::wstring g_originalWallpaper;

LONGLONG qpcNow() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return li.QuadPart;
}

std::wstring widen(std::string_view s) { return wallpaper::toWide(s); }
std::string narrow(std::wstring_view s) { return win::toUtf8(s); }

void toFloat4(Rgba8 c, float out[4]) {
    out[0] = c.r / 255.0f;
    out[1] = c.g / 255.0f;
    out[2] = c.b / 255.0f;
    out[3] = c.a / 255.0f;
}

ULONGLONG processCpu100ns() {
    FILETIME create{}, exit{}, kernel{}, user{};
    GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);
    auto toU = [](FILETIME f) { return (static_cast<ULONGLONG>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    return toU(kernel) + toU(user);
}

bool samePath(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return !a.empty() && !b.empty() && fs::equivalent(a, b, ec);
}

bool autostartEnabled() {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD size = sizeof(buf);
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, L"AsciiPaper", RRF_RT_REG_SZ, nullptr, buf, &size) ==
           ERROR_SUCCESS;
}

void setAutostart(bool on) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH * 2] = {};
        GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
        const std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
        RegSetValueExW(key, L"AsciiPaper", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                       static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, L"AsciiPaper");
    }
    RegCloseKey(key);
}

// Modelo para "Novo wallpaper" em branco.
constexpr const char* kTemplateToml = R"toml([meta]
name = "{NAME}"
author = ""
version = "1.0.0"
description = "O meu wallpaper"

[render]
mode = "shader"
entry = "main.hlsl"
fps = 30
cell = 12
scale = 10
font = "Cascadia Mono"
charset = " .:-=+*#%@"

[colors]
background = "#05070d"
palette = ["#05070d", "#1e3a8a", "#38bdf8", "#e0f2fe"]

[params]
speed = { default = 1.0, min = 0.0, max = 4.0 }
)toml";

constexpr const char* kTemplateHlsl = R"hlsl(// O meu wallpaper. Grava este ficheiro e o AsciiPaper recarrega-o sozinho.
// Guia: docs/CREATING_WALLPAPERS.md

CellOut cell(CellIn i)
{
    // i.world e mouse.world estão em unidades de mundo: mudar a resolução da grelha
    // não muda o tamanho do efeito.
    float2 center = mouse.inside ? mouse.world : worldSize * 0.5;
    float d = length(i.world - center);
    float v = sin(d * 0.5 - time * 3.0 * param("speed")) * exp(-d * 0.03);

    CellOut o;
    o.value = v * 0.5 + 0.5;
    o.fg = palette(o.value);
    o.bg = float4(0, 0, 0, 0);
    o.state = float4(0, 0, 0, 0);
    o.glyph = 0;
    return o;
}
)hlsl";

} // namespace

const std::wstring& App::originalWallpaper() { return g_originalWallpaper; }

App::App(const Options& options) : options_(options) {
    instance_ = GetModuleHandleW(nullptr);
    g_app = this;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    qpcFrequency_ = f.QuadPart;
    qpcStart_ = qpcNow();
}

App::~App() {
    shutdown();
    g_app = nullptr;
}

double App::now() const { return qpcToSeconds(qpcNow()); }

double App::qpcToSeconds(LONGLONG qpc) const {
    return static_cast<double>(qpc - qpcStart_) / static_cast<double>(qpcFrequency_);
}

// ---------------------------------------------------------------------------
// Arranque e paragem
// ---------------------------------------------------------------------------

bool App::init() {
    const auto awareness = GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext());
    log::info(L"AsciiPaper {} a arrancar (modo {}, DPI awareness {})", ASCIIPAPER_VERSION_WSTR,
              options_.windowed ? L"janela" : L"wallpaper", static_cast<int>(awareness));

    config_ = Config::load();
    settings_.maxFps = config_.maxFps;
    settings_.pause.pauseOnMaximized = config_.pauseOnMaximized;
    settings_.pause.pauseOnBatterySaver = config_.pauseOnBatterySaver;
    mouse_.mode = config_.interaction == "always" ? input::InteractionMode::Always
                                                  : input::InteractionMode::DesktopOnly;

    // Timer de alta resolução (Windows 10 1803+): acorda a horas certas sem precisar de
    // timeBeginPeriod, que aumentaria o consumo do sistema inteiro.
    frameTimer_.reset(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                             TIMER_ALL_ACCESS));
    if (!frameTimer_) {
        log::warn(L"Timer de alta resolução indisponível; a usar um timer normal");
        frameTimer_.reset(CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS));
    }
    if (!frameTimer_) {
        log::error(L"CreateWaitableTimerEx falhou (erro {})", GetLastError());
        return false;
    }

    if (!render::MonitorSurface::registerClass(instance_) || !createController()) {
        return false;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf())))) {
        log::error(L"DirectWrite indisponível");
        return false;
    }
    if (FAILED(device_.create()) || FAILED(pipeline_.create(device_.device()))) {
        return false;
    }
    mouse_.create(instance_);

    // A interface (menu da bandeja, launcher) é desenhada com Direct2D, sob pedido.
    gfx_ = std::make_unique<ui::Gfx>();
    if (FAILED(gfx_->init())) {
        log::warn(L"Direct2D indisponível: sem menu próprio nem launcher");
        gfx_.reset();
    } else {
        flyout_ = std::make_unique<ui::TrayFlyout>(*gfx_, *this);
        launcher_ = std::make_unique<ui::Launcher>(*gfx_, *this);
        flyout_->create(instance_);
        launcher_->create(instance_);
    }

    // O wallpaper é carregado e compilado ANTES de criar as janelas: assim o primeiro
    // frame já é o wallpaper a sério (sem flash preto nem cor provisória).
    wallpaper::loadShaderLibrary();
    loadWallpaperSync(initialWallpaperPath());

    // Modos que só desenham a interface num PNG e saem.
    if (!options_.uiScreenshot.empty() || !options_.flyoutScreenshot.empty()) {
        HRESULT hr = E_FAIL;
        if (gfx_ && !options_.uiScreenshot.empty()) {
            hr = launcher_->renderToPng(options_.uiScreenshot, options_.uiPage);
        }
        if (gfx_ && !options_.flyoutScreenshot.empty()) hr = flyout_->renderToPng(options_.flyoutScreenshot);
        log::info(L"Screenshot da interface: {}", SUCCEEDED(hr) ? L"ok" : win::hrToString(hr));
        PostQuitMessage(SUCCEEDED(hr) ? 0 : 1);
        return true;
    }

    if (options_.benchSeconds > 0) {
        benchmarking_ = true;
        benchStartQpc_ = qpcNow();
        benchStartCpu100ns_ = processCpu100ns();
        frameTimesMs_.reserve(static_cast<size_t>(options_.benchSeconds) * 64);
        SetTimer(controller_, TIMER_BENCH, static_cast<UINT>(options_.benchSeconds) * 1000u, nullptr);
        log::info(L"--bench: a medir durante {} s (pausas ignoradas)", options_.benchSeconds);
    }

    if (options_.windowed) {
        const bool ok = createWindowedSurface();
        if (ok && options_.openLauncher) openLauncher();
        return ok;
    }

    // ---- Modo wallpaper ----
    g_originalWallpaper = desktop::currentWallpaper();
    log::info(L"Wallpaper original do Windows: \"{}\"", g_originalWallpaper);

    // Explorer reiniciou -> a barra de tarefas envia esta mensagem a todas as janelas de topo.
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    // Sem isto, um processo elevado (admin) não receberia a mensagem de um Explorer normal.
    ChangeWindowMessageFilterEx(controller_, taskbarCreated_, MSGFLT_ALLOW, nullptr);

    sessionNotify_ = WTSRegisterSessionNotification(controller_, NOTIFY_FOR_THIS_SESSION) != FALSE;
    batterySaverNotify_.reset(RegisterPowerSettingNotification(controller_, &GUID_POWER_SAVING_STATUS,
                                                               DEVICE_NOTIFY_WINDOW_HANDLE));

    // Eventos de janela do sistema todo, entregues pela nossa fila de mensagens
    // (OUTOFCONTEXT: não injetamos nada nos outros processos).
    const DWORD events[][2] = {
        {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND},
        {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
    };
    for (const auto& e : events) {
        WinEventHook hook(SetWinEventHook(e[0], e[1], nullptr, onWinEvent, 0, 0,
                                          WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS));
        if (hook) winEventHooks_.push_back(std::move(hook));
    }

    trayIcon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                              GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                              LR_DEFAULTCOLOR));
    tray_.add(controller_, WM_APP_TRAY, trayIcon_);

    if (!attachDesktop()) {
        log::warn(L"Desktop ainda não está pronto; a tentar de novo");
        scheduleReattach(1000);
    }
    if (options_.openLauncher) openLauncher();
    return true;
}

void App::shutdown() {
    if (exiting_) {
        return;
    }
    exiting_ = true;
    stopFrameTimer();
    mouse_.setEnabled(false);
    watcher_.stop();
    if (pendingLoad_.valid()) pendingLoad_.wait();
    launcher_.reset();
    flyout_.reset();
    gfx_.reset();
    winEventHooks_.clear();
    batterySaverNotify_.reset();
    if (sessionNotify_) {
        WTSUnRegisterSessionNotification(controller_);
        sessionNotify_ = false;
    }
    tray_.remove();
    destroySurfaces();
    canvases_.clear();
    program_.releaseDeviceObjects();
    pipeline_.reset();
    device_.reset();
    if (!options_.windowed && !g_originalWallpaper.empty()) {
        // Nunca deixar o ecrã preto: o Explorer volta a pintar o wallpaper original.
        desktop::restoreWallpaper(g_originalWallpaper);
        log::info(L"Wallpaper original reposto");
    }
    if (controller_) {
        DestroyWindow(controller_);
        controller_ = nullptr;
    }
    if (trayIcon_) {
        DestroyIcon(trayIcon_);
        trayIcon_ = nullptr;
    }
    log::info(L"AsciiPaper terminou");
}

bool App::createController() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = controllerProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kControllerClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    // Janela de topo invisível, e NÃO message-only: as janelas message-only não recebem
    // mensagens em broadcast (WM_DISPLAYCHANGE, WM_SETTINGCHANGE, TaskbarCreated...).
    controller_ = CreateWindowExW(WS_EX_TOOLWINDOW, kControllerClass, L"AsciiPaper", WS_POPUP, 0, 0, 0, 0,
                                  nullptr, nullptr, instance_, this);
    if (!controller_) {
        log::error(L"Janela controladora falhou (erro {})", GetLastError());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Loop principal
// ---------------------------------------------------------------------------

int App::run() {
    if (!init()) {
        log::error(L"Arranque falhou; ver mensagens acima");
        shutdown();
        return 1;
    }

    for (;;) {
        // Esperamos por: o timer de frames (só se houver algo a desenhar), alterações na
        // pasta do wallpaper (hot reload) e mensagens. Em pausa só acordamos com mensagens.
        HANDLE handles[2];
        DWORD count = 0;
        int timerIndex = -1, watchIndex = -1;
        if (frameTimerArmed_) {
            timerIndex = static_cast<int>(count);
            handles[count++] = frameTimer_.get();
        }
        if (watcher_.active()) {
            watchIndex = static_cast<int>(count);
            handles[count++] = watcher_.event();
        }
        const DWORD r = MsgWaitForMultipleObjectsEx(count, handles, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        const int signaled = r < WAIT_OBJECT_0 + count ? static_cast<int>(r - WAIT_OBJECT_0) : -2;
        if (signaled >= 0 && signaled == timerIndex) {
            frameTimerArmed_ = false;
            renderFrame();
            if (anyActive()) {
                armFrameTimer(false);
            }
        } else if (signaled >= 0 && signaled == watchIndex) {
            if (watcher_.consume()) {
                SetTimer(controller_, TIMER_HOT_RELOAD, 150, nullptr);
            }
        }

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                shutdown();
                return static_cast<int>(msg.wParam);
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

void App::armFrameTimer(bool restart) {
    const int fps = std::clamp(settings_.fps, 1, settings_.maxFps);
    const LONGLONG period = qpcFrequency_ / fps;
    const LONGLONG t = qpcNow();
    if (restart || nextFrame_ == 0) {
        nextFrame_ = t;
    }
    nextFrame_ += period;
    if (nextFrame_ <= t) {
        // Ficámos para trás (ex.: o PC esteve ocupado): não disparar uma rajada de frames.
        nextFrame_ = t + period;
    }
    LONGLONG due100ns = (nextFrame_ - t) * 10'000'000 / qpcFrequency_;
    if (due100ns < 1) due100ns = 1;
    LARGE_INTEGER li;
    li.QuadPart = -due100ns; // negativo = relativo a agora
    SetWaitableTimer(frameTimer_.get(), &li, 0, nullptr, nullptr, FALSE);
    frameTimerArmed_ = true;
}

void App::stopFrameTimer() {
    if (frameTimer_) {
        CancelWaitableTimer(frameTimer_.get());
    }
    frameTimerArmed_ = false;
    nextFrame_ = 0;
}

bool App::anyActive() const {
    return std::any_of(surfaces_.begin(), surfaces_.end(), [](const auto& s) {
        return s->hasSwapChain() && !any(s->pause);
    });
}

// ---------------------------------------------------------------------------
// Wallpaper: carregar, compilar, hot reload, ajustes
// ---------------------------------------------------------------------------

fs::path App::initialWallpaperPath() const {
    const std::wstring wanted = !options_.wallpaper.empty() ? options_.wallpaper : widen(config_.wallpaper);
    fs::path p = wallpaper::resolveWallpaper(wanted);
    if (p.empty() && options_.wallpaper.empty() && config_.wallpaper != "ripple") {
        log::warn(L"Wallpaper \"{}\" da config não encontrado; a usar \"ripple\"", wanted);
        p = wallpaper::resolveWallpaper(L"ripple");
    }
    if (p.empty()) {
        log::error(L"Wallpaper \"{}\" não encontrado em nenhuma pasta de wallpapers", wanted);
        p = fs::path(wanted);
    }
    return p;
}

App::LoadJob App::runLoadJob(fs::path path) {
    LoadJob job;
    job.path = std::move(path);
    job.load = wallpaper::loadWallpaper(job.path);
    if (job.load.error.empty()) {
        // Os ajustes do utilizador (cores, resolução, params...) vão por cima do tema.
        auto& w = job.load.wallpaper;
        const Theme overrides = wallpaper::loadOverrides(wallpaper::wallpaperKey(job.path));
        for (auto& warning : applyTheme(w.manifest, overrides)) w.warnings.push_back(warning);
        job.shader = wallpaper::compileWallpaperShader(w.manifest, w.source, w.manifest.entry);
    }
    return job;
}

void App::loadWallpaperSync(const fs::path& path) {
    const LONGLONG t0 = qpcNow();
    applyLoadJob(runLoadJob(path));
    log::info(L"Wallpaper carregado e compilado em {:.0f} ms", (qpcNow() - t0) * 1000.0 / qpcFrequency_);
}

void App::loadWallpaperAsync(const fs::path& path) {
    if (pendingLoad_.valid()) {
        // Já há uma compilação a decorrer: tentamos outra vez quando acabar.
        SetTimer(controller_, TIMER_HOT_RELOAD, 200, nullptr);
        return;
    }
    // Compilar shaders pode demorar dezenas de ms. Fazemo-lo noutro thread porque o nosso
    // thread de UI está ligado ao do Explorer (janelas filhas): se bloquearmos, o desktop
    // bloqueia connosco.
    const HWND notify = controller_;
    pendingLoad_ = std::async(std::launch::async, [path, notify] {
        LoadJob job = runLoadJob(path);
        PostMessageW(notify, WM_APP_LOAD_DONE, 0, 0);
        return job;
    });
}

void App::applyLoadJob(LoadJob job) {
    wallpaperPath_ = job.path; // o pedido; o hot reload vigia este, mesmo que tenha falhado
    const auto& load = job.load;
    std::string error;
    if (!load.error.empty()) {
        error = load.error;
    } else if (!job.shader.ok()) {
        error = job.shader.errors;
    }

    if (!error.empty()) {
        // Manter o último estado bom e mostrar o erro no ecrã.
        log::error(L"Wallpaper {}: {}", job.path.wstring(), widen(error));
        errorText_ = error;
        if (!hasWallpaper_ && load.error.empty()) {
            // Nunca houve um estado bom: usar pelo menos as cores/fonte deste manifest.
            wallpaper_ = load.wallpaper;
            hasWallpaper_ = true;
            wallpaperPath_ = job.path;
        }
    } else if (FAILED(program_.create(device_.device(), job.shader.blob.Get()))) {
        errorText_ = "CreateComputeShader falhou (ver log)";
    } else {
        if (!job.shader.errors.empty()) log::debug(L"Avisos do compilador: {}", widen(job.shader.errors));
        wallpaper_ = load.wallpaper;
        hasWallpaper_ = true;
        wallpaperPath_ = job.path;
        errorText_.clear();
        for (const auto& w : wallpaper_.warnings) log::warn(L"{}: {}", widen(wallpaper_.label), widen(w));
        log::info(L"Wallpaper ativo: {} ({} fps, célula {} px, escala {}, fonte \"{}\")", widen(wallpaper_.label),
                  wallpaper_.manifest.fps, wallpaper_.manifest.cell, wallpaper_.manifest.scale,
                  widen(wallpaper_.manifest.font));
    }

    // Hot reload: vigiar a pasta do wallpaper (mesmo com erro, para apanhar a correção).
    const fs::path watch = !load.wallpaper.watchDir.empty() ? load.wallpaper.watchDir
                           : !load.wallpaper.dir.empty()   ? load.wallpaper.dir
                                                           : wallpaper_.watchDir;
    if (!watch.empty()) watcher_.start(watch);

    onManifestChanged();
    if (flyout_) flyout_->refresh();
    if (launcher_) launcher_->refresh();
}

void App::onManifestChanged() {
    if (!hasWallpaper_) return;
    settings_.fps = wallpaper_.manifest.fps;
    prepareAllSurfaces();
    rebuildCanvases();
    renderAllNow();
    if (controller_ && !surfaces_.empty()) updatePause(); // o fps pode ter mudado
}

void App::activate(const fs::path& path) {
    config_.wallpaper = wallpaper::wallpaperKey(path);
    if (!config_.save()) log::warn(L"Não consegui gravar {}", Config::path().wstring());
    log::info(L"Wallpaper escolhido: {}", widen(config_.wallpaper));
    loadWallpaperAsync(path);
}

std::vector<ui::WallpaperInfo> App::wallpapers() {
    std::vector<ui::WallpaperInfo> out;
    for (const auto& e : wallpaper::listWallpapers()) {
        out.push_back({widen(e.label), widen(e.base), widen(e.theme), e.path, e.preview, e.userOwned});
    }
    return out;
}

bool App::inspect(const fs::path& path, Manifest& base, Manifest& effective, std::string& error) {
    if (samePath(path, wallpaperPath_) && hasWallpaper_) {
        base = wallpaper_.baseManifest;
        effective = wallpaper_.manifest;
        return true;
    }
    const auto r = wallpaper::loadWallpaper(path);
    if (!r.error.empty()) {
        error = r.error;
        return false;
    }
    base = r.wallpaper.baseManifest;
    effective = base;
    applyTheme(effective, wallpaper::loadOverrides(wallpaper::wallpaperKey(path)));
    return true;
}

Theme App::overrides(const fs::path& path) { return wallpaper::loadOverrides(wallpaper::wallpaperKey(path)); }

void App::setOverrides(const fs::path& path, const Theme& t, bool persist) {
    if (persist) wallpaper::saveOverrides(wallpaper::wallpaperKey(path), t);
    if (!samePath(path, wallpaperPath_) || !hasWallpaper_) return;
    // Ao vivo: parâmetros e cores são só constantes (instantâneo); fonte, resolução e
    // charset reconstroem o atlas. Nada disto precisa de recompilar o shader.
    const Manifest before = wallpaper_.manifest;
    wallpaper_.manifest = wallpaper_.baseManifest;
    applyTheme(wallpaper_.manifest, t);
    const Manifest& m = wallpaper_.manifest;
    const bool layoutChanged = m.cell != before.cell || m.font != before.font || m.fontWeight != before.fontWeight ||
                               m.charset != before.charset || m.fps != before.fps;
    if (layoutChanged) {
        onManifestChanged();
    } else if (!anyActive()) {
        renderAllNow(); // em pausa: mostrar a mudança na mesma
    }
}

bool App::saveAsTheme(const fs::path& path, const std::wstring& name, std::wstring& error) {
    Manifest base, effective;
    std::string err;
    if (!inspect(path, base, effective, err)) {
        error = widen(err);
        return false;
    }
    // O tema guarda o que difere do wallpaper original (tema atual + ajustes).
    const auto loaded = wallpaper::loadWallpaper(path);
    const fs::path baseDir = loaded.wallpaper.dir;
    Theme t = wallpaper::loadOverrides(wallpaper::wallpaperKey(path));
    if (!loaded.wallpaper.themeFile.empty()) {
        std::ifstream in(loaded.wallpaper.themeFile, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        Theme current = parseTheme(ss.str()).theme;
        // Os ajustes ganham ao tema atual.
        if (!t.font) t.font = current.font;
        if (!t.fontWeight) t.fontWeight = current.fontWeight;
        if (!t.cell) t.cell = current.cell;
        if (!t.scale) t.scale = current.scale;
        if (!t.fps) t.fps = current.fps;
        if (!t.charset) t.charset = current.charset;
        if (!t.background) t.background = current.background;
        if (!t.palette) t.palette = current.palette;
        for (const auto& p : current.params) {
            if (std::none_of(t.params.begin(), t.params.end(), [&](const auto& q) { return q.first == p.first; })) {
                t.params.push_back(p);
            }
        }
    }
    t.base = wallpaper::toUtf8Path(baseDir.filename());
    const std::string slug = wallpaper::slugify(narrow(name));
    const fs::path file = baseDir / L"themes" / (widen(slug) + L".toml");
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << themeToToml(t, "Tema criado no launcher do AsciiPaper");
    if (!out) {
        error = L"Não consegui escrever " + file.wstring();
        return false;
    }
    log::info(L"Tema gravado: {}", file.wstring());
    return true;
}

bool App::createWallpaper(const std::wstring& name, const fs::path& templatePath, fs::path& created,
                          std::wstring& error) {
    const std::string displayName = narrow(name);
    const fs::path dir = wallpaper::userWallpaperDir() / widen(wallpaper::slugify(displayName));
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        error = L"Já existe um wallpaper com esse nome.";
        return false;
    }
    fs::create_directories(dir, ec);
    std::string toml, hlsl;
    if (!templatePath.empty()) {
        const auto loaded = wallpaper::loadWallpaper(templatePath);
        const fs::path src = loaded.wallpaper.dir.empty() ? templatePath : loaded.wallpaper.dir;
        std::ifstream t(src / L"wallpaper.toml", std::ios::binary), h(src / L"main.hlsl", std::ios::binary);
        std::stringstream a, b;
        a << t.rdbuf();
        b << h.rdbuf();
        toml = a.str();
        hlsl = b.str();
        // Trocar o nome e limpar o autor no [meta].
        toml = std::regex_replace(toml, std::regex(R"(name\s*=\s*"[^"]*")"), "name = \"" + displayName + "\"",
                                  std::regex_constants::format_first_only);
        toml = std::regex_replace(toml, std::regex(R"(author\s*=\s*"[^"]*")"), "author = \"\"",
                                  std::regex_constants::format_first_only);
    }
    if (toml.empty() || hlsl.empty()) {
        toml = std::regex_replace(std::string(kTemplateToml), std::regex("\\{NAME\\}"), displayName);
        hlsl = kTemplateHlsl;
    }
    std::ofstream(dir / L"wallpaper.toml", std::ios::binary) << toml;
    std::ofstream(dir / L"main.hlsl", std::ios::binary) << hlsl;
    created = dir;
    log::info(L"Wallpaper criado: {}", dir.wstring());
    return true;
}

void App::openInEditor(const fs::path& wallpaperPath) {
    const auto loaded = wallpaper::loadWallpaper(wallpaperPath);
    const fs::path dir = loaded.wallpaper.dir.empty() ? wallpaperPath : loaded.wallpaper.dir;
    const fs::path file = dir / widen(loaded.wallpaper.manifest.entry.empty() ? "main.hlsl"
                                                                              : loaded.wallpaper.manifest.entry);
    // "edit" usa o editor associado; se não houver associação para .hlsl, o Bloco de notas.
    auto ok = [](HINSTANCE h) { return reinterpret_cast<INT_PTR>(h) > 32; };
    if (!ok(ShellExecuteW(nullptr, L"edit", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) &&
        !ok(ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL))) {
        ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + file.wstring() + L"\"").c_str(), nullptr,
                      SW_SHOWNORMAL);
    }
}

void App::openFolder(const fs::path& path) {
    std::error_code ec;
    const fs::path dir = fs::is_directory(path, ec) ? path : path.parent_path();
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::vector<std::wstring> App::monospaceFonts() {
    if (!fontCache_.empty()) return fontCache_;
    Microsoft::WRL::ComPtr<IDWriteFontCollection> fonts;
    if (FAILED(dwrite_->GetSystemFontCollection(&fonts))) return {};
    std::set<std::wstring> names;
    for (UINT32 i = 0; i < fonts->GetFontFamilyCount(); ++i) {
        Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
        Microsoft::WRL::ComPtr<IDWriteFont> font;
        Microsoft::WRL::ComPtr<IDWriteFont1> font1;
        Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> strings;
        if (FAILED(fonts->GetFontFamily(i, &family)) || FAILED(family->GetFont(0, &font)) ||
            FAILED(font.As(&font1)) || !font1->IsMonospacedFont() || font->GetSimulations() != 0 ||
            FAILED(family->GetFamilyNames(&strings))) {
            continue;
        }
        UINT32 index = 0;
        BOOL exists = FALSE;
        strings->FindLocaleName(L"en-us", &index, &exists);
        if (!exists) index = 0;
        UINT32 len = 0;
        strings->GetStringLength(index, &len);
        std::wstring name(len + 1, L'\0');
        strings->GetString(index, name.data(), len + 1);
        name.resize(len);
        if (!name.empty() && name[0] != L'@') names.insert(name);
    }
    fontCache_.assign(names.begin(), names.end());
    return fontCache_;
}

void App::setPaused(bool p) {
    pause_.global.user = p;
    updatePause();
    if (flyout_) flyout_->refresh();
}

ui::GlobalSettings App::settings() const {
    ui::GlobalSettings s;
    s.spanMonitors = config_.spanMonitors;
    s.interactionAlways = config_.interaction == "always";
    s.pauseOnMaximized = config_.pauseOnMaximized;
    s.pauseOnBatterySaver = config_.pauseOnBatterySaver;
    s.maxFps = config_.maxFps;
    s.autostart = autostartEnabled();
    return s;
}

void App::setSettings(const ui::GlobalSettings& s) {
    const bool spanChanged = s.spanMonitors != config_.spanMonitors;
    config_.spanMonitors = s.spanMonitors;
    config_.interaction = s.interactionAlways ? "always" : "desktop_only";
    config_.pauseOnMaximized = s.pauseOnMaximized;
    config_.pauseOnBatterySaver = s.pauseOnBatterySaver;
    config_.maxFps = std::clamp(s.maxFps, 1, 240);
    config_.save();
    if (s.autostart != autostartEnabled()) setAutostart(s.autostart);

    settings_.maxFps = config_.maxFps;
    settings_.pause.pauseOnMaximized = config_.pauseOnMaximized;
    settings_.pause.pauseOnBatterySaver = config_.pauseOnBatterySaver;
    mouse_.mode = s.interactionAlways ? input::InteractionMode::Always : input::InteractionMode::DesktopOnly;
    if (spanChanged) {
        rebuildCanvases();
        renderAllNow();
    }
    updatePause();
}

std::wstring App::lastError() const { return widen(errorText_); }

void App::openLauncher() {
    if (launcher_) launcher_->show();
}

void App::showTrayFlyout() {
    if (!flyout_) return;
    NOTIFYICONIDENTIFIER id{};
    id.cbSize = sizeof(id);
    id.hWnd = controller_;
    id.uID = 1;
    RECT anchor{};
    if (FAILED(Shell_NotifyIconGetRect(&id, &anchor))) {
        POINT pt{};
        GetCursorPos(&pt);
        anchor = {pt.x, pt.y, pt.x, pt.y};
    }
    flyout_->toggle(anchor);
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

HRESULT App::prepareSurface(render::MonitorSurface& s) {
    if (!s.hasSwapChain() || !hasWallpaper_) return E_FAIL;
    const Manifest& m = wallpaper_.manifest;
    render::AtlasDesc desc;
    desc.font = widen(m.font);
    desc.fontDir = wallpaper_.dir;
    desc.weight = m.fontWeight;
    desc.cellHeightPx = scaleByDpi(m.cell, s.monitor.dpi ? s.monitor.dpi : 96);
    desc.charset = m.charset;
    desc.glyphs = m.glyphs;
    if (!s.atlas.valid() || s.atlas.key() != desc.key()) {
        if (HRESULT hr = s.atlas.build(device_.device(), dwrite_.Get(), desc); FAILED(hr)) return hr;
    }
    s.grid.setLayout(s.width(), s.height(), s.atlas);
    return S_OK;
}

void App::prepareAllSurfaces() {
    for (auto& s : surfaces_) prepareSurface(*s);
}

void App::rebuildCanvases() {
    std::vector<render::MonitorSurface*> ready;
    for (auto& s : surfaces_) {
        s->canvas = nullptr;
        if (s->hasSwapChain() && s->atlas.valid()) ready.push_back(s.get());
    }
    if (ready.empty()) {
        canvases_.clear();
        return;
    }
    auto monitorOf = [](const render::MonitorSurface* s) { return CanvasMonitor{s->monitor.rect, s->grid.layout()}; };
    const bool span = config_.spanMonitors && !options_.windowed && ready.size() > 1;
    const size_t count = span ? 1 : ready.size();
    if (canvases_.size() != count) {
        canvases_.clear();
        for (size_t i = 0; i < count; ++i) canvases_.push_back(std::make_unique<render::CellCanvas>());
    }
    if (span) {
        std::vector<CanvasMonitor> ms;
        size_t primary = 0;
        for (size_t i = 0; i < ready.size(); ++i) {
            ms.push_back(monitorOf(ready[i]));
            if (ready[i]->monitor.primary) primary = i;
        }
        canvases_[0]->resize(device_.device(), computeSpanCanvas(ms, primary));
        for (auto* s : ready) s->canvas = canvases_[0].get();
    } else {
        for (size_t i = 0; i < ready.size(); ++i) {
            canvases_[i]->resize(device_.device(), computeSingleCanvas(monitorOf(ready[i])));
            ready[i]->canvas = canvases_[i].get();
        }
    }
}

render::MonitorSurface* App::referenceSurface(const render::CellCanvas& canvas) const {
    render::MonitorSurface* first = nullptr;
    for (const auto& s : surfaces_) {
        if (s->canvas != &canvas) continue;
        if (s->monitor.primary) return s.get();
        if (!first) first = s.get();
    }
    return first;
}

wallpaper::FrameConstants App::makeFrame(const render::CellCanvas& canvas, const render::MonitorSurface& ref,
                                         double time, double dt) const {
    wallpaper::FrameConstants f{};
    const CanvasLayout& c = canvas.layout();
    const Manifest& m = wallpaper_.manifest;
    const float cw = static_cast<float>(c.cellW), ch = static_cast<float>(c.cellH);

    f.time = static_cast<float>(time);
    f.dt = static_cast<float>(dt);
    f.gridW = static_cast<uint32_t>(c.cols);
    f.gridH = static_cast<uint32_t>(c.rows);

    auto toCellX = [&](LONG x) { return static_cast<float>(x - c.originPx.x) / cw; };
    auto toCellY = [&](LONG y) { return static_cast<float>(y - c.originPx.y) / ch; };
    const POINT p = mouse_.cursor;
    bool onCanvas = false;
    for (const auto& s : surfaces_) {
        if (s->canvas != &canvas) continue;
        const Rect& r = s->monitor.rect;
        onCanvas = onCanvas || (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom);
    }
    const bool inside = onCanvas && mouse_.overDesktop;
    f.mouseX = toCellX(p.x);
    f.mouseY = toCellY(p.y);
    f.mouseVX = mouseVelX_ / cw;
    f.mouseVY = mouseVelY_ / ch;
    f.inside = inside ? 1u : 0u;
    f.buttons = inside ? mouse_.buttons : 0u;
    f.scroll = inside ? mouse_.scroll : 0.0f;
    if (mouse_.clickQpc != 0) {
        f.clickX = toCellX(mouse_.clickPos.x);
        f.clickY = toCellY(mouse_.clickPos.y);
        f.clickTime = static_cast<float>(qpcToSeconds(mouse_.clickQpc));
        f.clickButton = mouse_.clickButton;
    } else {
        f.clickX = f.clickY = -10000.0f;
        f.clickTime = -1000.0f;
    }

    f.charsetSize = static_cast<uint32_t>(ref.atlas.charsetSize());
    f.glyphCount = static_cast<uint32_t>(ref.atlas.glyphCount());
    toFloat4(m.background, f.background);
    f.paletteCount = static_cast<uint32_t>(std::min<size_t>(m.palette.size(), 8));
    for (uint32_t i = 0; i < f.paletteCount; ++i) toFloat4(m.palette[i], f.palette[i]);
    f.frameIndex = frameIndex_;
    f.cellAspect = cw / ch;
    for (size_t i = 0; i < m.params.size() && i < 16; ++i) f.params[i] = m.params[i].value;
    f.cellPxW = cw;
    f.cellPxH = ch;
    f.resolutionW = static_cast<float>(c.cols) * cw;
    f.resolutionH = static_cast<float>(c.rows) * ch;
    // Unidade de mundo = `scale` px lógicos no monitor de referência. Não depende de `cell`:
    // mudar a resolução da grelha deixa o efeito do mesmo tamanho, só mais ou menos "pixelado".
    const float unitPx = std::max(1.0f, m.scale) * static_cast<float>(ref.monitor.dpi ? ref.monitor.dpi : 96) / 96.0f;
    f.worldPerCellX = cw / unitPx;
    f.worldPerCellY = ch / unitPx;
    f.monitorCount = static_cast<uint32_t>(std::min<size_t>(c.monitorCells.size(), 8));
    for (uint32_t i = 0; i < f.monitorCount; ++i) {
        const Rect& r = c.monitorCells[i];
        f.monitors[i][0] = r.left;
        f.monitors[i][1] = r.top;
        f.monitors[i][2] = r.right;
        f.monitors[i][3] = r.bottom;
    }
    return f;
}

void App::drawError(render::CellCanvas& canvas, const render::MonitorSurface& ref) {
    // No canto superior esquerdo do monitor de referência.
    const Point p0 = canvasCellFor(canvas.layout(), CanvasMonitor{ref.monitor.rect, ref.grid.layout()}, 0, 0);
    const int col = std::max(0, p0.x) + 1;
    const int row = std::max(0, p0.y) + 1;
    const Rgba8 fg{255, 120, 120, 255}, bg{24, 4, 6, 235}, dim{200, 200, 210, 255};
    canvas.writeText(device_.context(), ref.atlas, col, row,
                     " AsciiPaper: erro no wallpaper (o último estado bom continua) ", dim, bg);
    std::istringstream lines(errorText_);
    std::string line;
    int r = row + 1;
    while (std::getline(lines, line) && r < row + 13) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        canvas.writeText(device_.context(), ref.atlas, col, r++, " " + line + " ", fg, bg);
    }
}

void App::computeCanvas(render::CellCanvas& canvas, double time, double dt) {
    render::MonitorSurface* ref = referenceSurface(canvas);
    if (!ref || !canvas.valid()) return;
    auto* ctx = device_.context();
    if (program_.valid()) {
        program_.dispatch(ctx, canvas, makeFrame(canvas, *ref, time, dt));
    } else {
        canvas.clear(ctx);
    }
    if (!errorText_.empty()) drawError(canvas, *ref);
}

HRESULT App::drawSurface(render::MonitorSurface& s) {
    if (!s.hasSwapChain() || !s.canvas || !s.atlas.valid()) return S_OK;
    auto* ctx = device_.context();
    s.grid.draw(device_.device(), ctx, pipeline_, s.rtv(), s.atlas, *s.canvas, s.monitor.rect,
                wallpaper_.manifest.background);
    if (captureRequested_) {
        // Antes do Present: com o flip model o back buffer fica indefinido depois.
        captureRequested_ = false;
        const HRESULT hr = render::saveTextureAsPng(device_.device(), ctx, s.backBuffer().Get(), options_.screenshot);
        if (SUCCEEDED(hr)) {
            log::info(L"Screenshot gravado em {}", options_.screenshot);
            log::printConsole(L"Screenshot: " + options_.screenshot + L"\n");
        } else {
            log::error(L"Screenshot falhou: {}", win::hrToString(hr));
        }
        PostQuitMessage(SUCCEEDED(hr) ? 0 : 1);
    }
    return s.present();
}

void App::renderAllNow() {
    if (canvases_.empty()) return;
    const double dt = 1.0 / std::clamp(settings_.fps, 1, settings_.maxFps);
    const double t = now();
    for (auto& c : canvases_) computeCanvas(*c, t, dt);
    for (auto& s : surfaces_) {
        const HRESULT hr = drawSurface(*s);
        if (render::isDeviceLost(hr)) {
            handleDeviceLost(hr);
            return;
        }
    }
}

void App::renderFrame() {
    const LONGLONG start = qpcNow();
    const double t = qpcToSeconds(start);
    double dt = lastFrameTime_ > 0.0 ? t - lastFrameTime_ : 1.0 / settings_.fps;
    dt = std::clamp(dt, 0.0005, 0.1); // depois de uma pausa não dar um salto enorme na simulação
    lastFrameTime_ = t;

    mouse_.update();
    // Velocidade do rato suavizada (px/s), igual para todos os monitores.
    const float k = 1.0f - std::exp(-static_cast<float>(dt) * 12.0f);
    const float vx = static_cast<float>(mouse_.cursor.x - lastCursor_.x) / static_cast<float>(dt);
    const float vy = static_cast<float>(mouse_.cursor.y - lastCursor_.y) / static_cast<float>(dt);
    mouseVelX_ += (vx - mouseVelX_) * k;
    mouseVelY_ += (vy - mouseVelY_) * k;
    lastCursor_ = mouse_.cursor;

    if (options_.windowed) {
        // Em janela, o "monitor" é a área cliente, onde quer que a janela esteja.
        for (auto& s : surfaces_) {
            if (!s->hwnd()) continue;
            POINT o{0, 0};
            ClientToScreen(s->hwnd(), &o);
            const Rect r{o.x, o.y, o.x + s->width(), o.y + s->height()};
            if (!(r == s->monitor.rect)) {
                s->monitor.rect = r;
                rebuildCanvases();
            }
        }
    }

    // 1) Simular cada canvas que tenha pelo menos um monitor visível (uma vez por frame,
    //    mesmo que cubra vários monitores). 2) Cada monitor ativo desenha a sua parte.
    for (auto& c : canvases_) {
        const bool used = std::any_of(surfaces_.begin(), surfaces_.end(), [&](const auto& s) {
            return s->canvas == c.get() && !any(s->pause) && s->hasSwapChain();
        });
        if (used) computeCanvas(*c, t, dt);
    }
    for (auto& s : surfaces_) {
        if (any(s->pause) || !s->hasSwapChain()) continue;
        const HRESULT hr = drawSurface(*s);
        if (render::isDeviceLost(hr)) {
            handleDeviceLost(hr);
            return;
        }
    }
    ++frameIndex_;
    if (benchmarking_) {
        frameTimesMs_.push_back((qpcNow() - start) * 1000.0 / qpcFrequency_);
    }
}

// ---------------------------------------------------------------------------
// Desktop: superfícies por monitor
// ---------------------------------------------------------------------------

std::unique_ptr<render::MonitorSurface> App::createSurface(const desktop::MonitorInfo& mon, unsigned index) {
    auto s = std::make_unique<render::MonitorSurface>();
    s->monitor = mon;
    s->index = index;
    if (!s->createWindow(instance_, false, controller_) || !workerw_.adopt(s->hwnd())) {
        return nullptr;
    }
    workerw_.place(s->hwnd(), mon.rect);
    if (FAILED(s->createSwapChain(device_, mon.rect.width(), mon.rect.height()))) {
        return nullptr;
    }
    prepareSurface(*s);
    const auto& g = s->grid.layout();
    log::info(L"Monitor {}{}: {}x{} em ({}, {}), {} DPI ({}%), swapchain {}, grelha {}x{} de {}x{} px", mon.device,
              mon.primary ? L" (principal)" : L"", mon.rect.width(), mon.rect.height(), mon.rect.left, mon.rect.top,
              mon.dpi, mon.dpi * 100 / 96, s->usesFlipModel() ? L"flip" : L"blt", g.cols, g.rows, g.cellW, g.cellH);
    return s;
}

bool App::attachDesktop() {
    if (!workerw_.attach()) {
        return false;
    }
    const auto monitors = desktop::enumerateMonitors();
    unsigned index = 0;
    for (const auto& m : monitors) {
        if (auto s = createSurface(m, index++)) {
            surfaces_.push_back(std::move(s));
        }
    }
    if (surfaces_.empty()) {
        log::error(L"Nenhuma superfície criada");
        return false;
    }
    // Primeiro frame ANTES de mostrar as janelas: é isto que evita o flash preto.
    rebuildCanvases();
    renderAllNow();
    for (auto& s : surfaces_) s->show();
    updatePause();
    return true;
}

bool App::createWindowedSurface() {
    auto s = std::make_unique<render::MonitorSurface>();
    if (!s->createWindow(instance_, true, controller_)) {
        return false;
    }
    RECT rc{};
    GetClientRect(s->hwnd(), &rc);
    POINT o{0, 0};
    ClientToScreen(s->hwnd(), &o);
    s->monitor.dpi = GetDpiForWindow(s->hwnd());
    s->monitor.device = L"(janela)";
    s->monitor.primary = true;
    s->monitor.rect = {o.x, o.y, o.x + rc.right, o.y + rc.bottom};
    mouse_.windowedTarget = s->hwnd();
    if (FAILED(s->createSwapChain(device_, rc.right, rc.bottom))) {
        return false;
    }
    prepareSurface(*s);
    surfaces_.push_back(std::move(s));
    rebuildCanvases();
    renderAllNow();
    if (options_.screenshot.empty()) {
        surfaces_.back()->show();
    } else {
        // --screenshot: a janela nunca aparece; deixamos a animação correr 3 s e gravamos.
        SetTimer(controller_, TIMER_SCREENSHOT, 3000, nullptr);
    }
    updatePause();
    return true;
}

void App::destroySurfaces() {
    surfaces_.clear(); // o destrutor de cada superfície liberta swapchain e janela
}

void App::scheduleLayout() {
    // As mensagens chegam em rajadas (WM_DISPLAYCHANGE + vários WM_SETTINGCHANGE...).
    // Esperamos que acalmem e fazemos uma passagem; depois outra mais tarde, porque o
    // Explorer redimensiona o WorkerW ao seu ritmo e pode fazê-lo depois de nós.
    SetTimer(controller_, TIMER_LAYOUT, 250, nullptr);
    SetTimer(controller_, TIMER_LAYOUT_LATE, 1500, nullptr);
}

void App::syncLayout() {
    if (options_.windowed || exiting_) {
        return;
    }
    if (!workerw_.isValid()) {
        scheduleReattach(200);
        return;
    }
    const auto monitors = desktop::enumerateMonitors();

    // 1) Remover superfícies de monitores que desapareceram.
    std::erase_if(surfaces_, [&](const auto& s) {
        const bool gone = std::none_of(monitors.begin(), monitors.end(),
                                       [&](const auto& m) { return m.device == s->monitor.device; });
        if (gone) log::info(L"Monitor {} desligado", s->monitor.device);
        return gone;
    });

    // 2) Atualizar as que ficaram e criar as novas.
    std::vector<render::MonitorSurface*> fresh;
    unsigned index = 0;
    for (const auto& m : monitors) {
        auto it = std::find_if(surfaces_.begin(), surfaces_.end(),
                               [&](const auto& s) { return s->monitor.device == m.device; });
        if (it == surfaces_.end()) {
            log::info(L"Monitor {} ligado", m.device);
            if (auto s = createSurface(m, index)) {
                fresh.push_back(s.get());
                surfaces_.push_back(std::move(s));
            }
        } else {
            auto& s = **it;
            if (!s.monitor.sameLayout(m)) {
                log::info(L"Monitor {}: {}x{}@({},{}) {} DPI -> {}x{}@({},{}) {} DPI", m.device,
                          s.monitor.rect.width(), s.monitor.rect.height(), s.monitor.rect.left,
                          s.monitor.rect.top, s.monitor.dpi, m.rect.width(), m.rect.height(), m.rect.left,
                          m.rect.top, m.dpi);
            }
            s.monitor = m;
            s.index = index;
            // Primeiro os buffers, depois atlas/grelha para o DPI novo, depois a janela.
            const HRESULT hr = s.resize(device_, m.rect.width(), m.rect.height());
            if (render::isDeviceLost(hr)) {
                handleDeviceLost(hr);
                return;
            }
            prepareSurface(s);
            // A origem do WorkerW muda se aparecer um monitor à esquerda/acima, por isso
            // reposicionamos sempre (é barato).
            workerw_.place(s.hwnd(), m.rect);
        }
        ++index;
    }
    // O canvas estendido depende de TODOS os monitores: refazer e desenhar um frame novo.
    rebuildCanvases();
    renderAllNow();
    for (auto* s : fresh) s->show();
    updatePause();
}

void App::scheduleReattach(UINT delayMs) {
    if (!exiting_) {
        SetTimer(controller_, TIMER_REATTACH, delayMs, nullptr);
    }
}

// ---------------------------------------------------------------------------
// Device lost (o driver da GPU reiniciou)
// ---------------------------------------------------------------------------

void App::handleDeviceLost(HRESULT hr) {
    log::warn(L"Dispositivo D3D perdido ({}; motivo {}); a recriar", win::hrToString(hr),
              win::hrToString(device_.removedReason()));
    stopFrameTimer();
    for (auto& s : surfaces_) s->releaseSwapChain();
    canvases_.clear();
    program_.releaseDeviceObjects();
    pipeline_.reset();
    device_.reset();
    if (!recreateDeviceResources()) {
        // O driver pode demorar uns segundos a voltar.
        SetTimer(controller_, TIMER_DEVICE_RETRY, 1000, nullptr);
        return;
    }
    updatePause();
}

bool App::recreateDeviceResources() {
    if (FAILED(device_.create()) || FAILED(pipeline_.create(device_.device()))) return false;
    if (hasWallpaper_ && FAILED(program_.recreate(device_.device()))) {
        log::warn(L"Não consegui recriar o compute shader; a recompilar");
        loadWallpaperSync(wallpaperPath_);
    }
    for (auto& s : surfaces_) {
        if (!s->hwnd()) continue;
        RECT rc{};
        GetClientRect(s->hwnd(), &rc);
        if (FAILED(s->createSwapChain(device_, rc.right, rc.bottom))) return false;
        prepareSurface(*s);
    }
    rebuildCanvases();
    renderAllNow();
    log::info(L"Dispositivo D3D recriado");
    return true;
}

// ---------------------------------------------------------------------------
// Pausas
// ---------------------------------------------------------------------------

void App::updatePause() {
    if (exiting_) {
        return;
    }
    std::vector<desktop::MonitorInfo> monitors;
    for (const auto& s : surfaces_) monitors.push_back(s->monitor);

    pause_.rules = settings_.pause;
    std::vector<MonitorCoverage> coverage(monitors.size());
    if (!options_.windowed) {
        coverage = pause_.scan(monitors);
    }

    bool windowPause = false;
    bool resumedAny = false;
    for (size_t i = 0; i < surfaces_.size(); ++i) {
        auto& s = *surfaces_[i];
        PauseReason r = evaluatePause(pause_.global, coverage[i], pause_.rules);
        if (options_.windowed && s.hwnd() && IsIconic(s.hwnd())) {
            r |= PauseReason::User; // janela minimizada: nada para mostrar
        }
        if (benchmarking_) r = PauseReason::None; // o bench mede sempre
        if (r != s.pause) {
            log::info(L"Monitor {}: {} -> {}", s.monitor.device, describe(s.pause), describe(r));
            resumedAny = resumedAny || (any(s.pause) && !any(r));
            s.pause = r;
        }
        windowPause = windowPause || isWindowPause(r);
    }
    if (resumedAny) renderAllNow(); // frame fresco logo, em vez de esperar pelo timer

    const bool active = anyActive();
    // Rato: só registamos o Raw Input quando há algo a desenhar. Em pausa o sistema deixa
    // de nos acordar a cada movimento do rato.
    mouse_.setEnabled(active);
    // Algumas mudanças (ex.: Win+↓ numa janela maximizada) não geram nenhum dos eventos
    // que ouvimos, por isso reavaliamos 1x por segundo enquanto houver algo a desenhar
    // ou uma pausa causada por uma janela. Bloqueio/suspensão = sem timers nenhuns.
    if (!options_.windowed && (active || windowPause)) {
        SetTimer(controller_, TIMER_PAUSE_CHECK, 1000, nullptr);
    } else {
        KillTimer(controller_, TIMER_PAUSE_CHECK);
    }
    if (active && !frameTimerArmed_) {
        lastFrameTime_ = 0.0;
        armFrameTimer(true);
    } else if (!active) {
        stopFrameTimer();
    }
}

void CALLBACK App::onWinEvent(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
    if (!g_app || g_app->exiting_ || idObject != OBJID_WINDOW || !hwnd) {
        return;
    }
    // Debounce: uma janela a maximizar gera vários eventos seguidos.
    SetTimer(g_app->controller_, TIMER_PAUSE_DEBOUNCE, 100, nullptr);
}

// ---------------------------------------------------------------------------
// Bench
// ---------------------------------------------------------------------------

void App::finishBench() {
    KillTimer(controller_, TIMER_BENCH);
    const double wall = (qpcNow() - benchStartQpc_) / static_cast<double>(qpcFrequency_);
    const double cpuSeconds = (processCpu100ns() - benchStartCpu100ns_) / 1e7;
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const double cpuPercent = 100.0 * cpuSeconds / wall / std::max<DWORD>(1, si.dwNumberOfProcessors);

    std::vector<double> t = frameTimesMs_;
    std::sort(t.begin(), t.end());
    const double avg = t.empty() ? 0.0 : std::accumulate(t.begin(), t.end(), 0.0) / t.size();
    const double p99 = t.empty() ? 0.0 : t[std::min(t.size() - 1, static_cast<size_t>(t.size() * 0.99))];

    PROCESS_MEMORY_COUNTERS_EX mem{};
    mem.cb = sizeof(mem);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem), sizeof(mem));

    std::wstring cells;
    for (const auto& s : surfaces_) {
        const auto& g = s->grid.layout();
        cells += std::format(L"\n  {}: {}x{} px, grelha {}x{} de {}x{} px", s->monitor.device, s->width(),
                             s->height(), g.cols, g.rows, g.cellW, g.cellH);
    }
    for (const auto& c : canvases_) {
        cells += std::format(L"\n  canvas: {}x{} ({} células)", c->layout().cols, c->layout().rows,
                             c->layout().cellCount());
    }
    const std::wstring report = std::format(
        L"\n=== AsciiPaper --bench ({}) ===\n"
        L"Wallpaper: {}{}\n"
        L"Frames: {} em {:.1f} s ({:.1f} fps, alvo {})\n"
        L"Frame time (CPU: compute + draw + Present): médio {:.3f} ms, p99 {:.3f} ms\n"
        L"CPU do processo: {:.2f}% (de {} threads lógicas)\n"
        L"RAM: working set {:.1f} MB, private bytes {:.1f} MB\n",
        ASCIIPAPER_VERSION_WSTR, widen(wallpaper_.label), cells, t.size(), wall, t.size() / wall,
        std::clamp(settings_.fps, 1, settings_.maxFps), avg, p99, cpuPercent, si.dwNumberOfProcessors,
        mem.WorkingSetSize / (1024.0 * 1024.0), mem.PrivateUsage / (1024.0 * 1024.0));
    log::info(L"{}", report);
    log::printConsole(report);
    PostQuitMessage(0);
}

// ---------------------------------------------------------------------------
// Mensagens
// ---------------------------------------------------------------------------

LRESULT CALLBACK App::controllerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self && self->controller_ == hwnd) {
        return self->onMessage(msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::onMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (taskbarCreated_ != 0 && msg == taskbarCreated_) {
        log::info(L"Explorer (re)iniciado: a voltar a ligar");
        tray_.readd();
        reattachAttempts_ = 0;
        scheduleReattach(300);
        return 0;
    }

    switch (msg) {
    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_CONTEXTMENU:
        case NIN_SELECT:
        case NIN_KEYSELECT:
            showTrayFlyout();
            break;
        default:
            break;
        }
        return 0;

    case WM_APP_LOAD_DONE:
        if (pendingLoad_.valid()) {
            applyLoadJob(pendingLoad_.get());
        }
        return 0;

    case render::WM_APP_SURFACE_LOST:
        if (!exiting_) {
            log::warn(L"Uma janela de monitor foi destruída por fora (Explorer?)");
            scheduleReattach(500);
        }
        return 0;

    case render::WM_APP_SURFACE_RESIZE: {
        const HWND h = reinterpret_cast<HWND>(lp);
        for (auto& s : surfaces_) {
            if (s->hwnd() != h) continue;
            RECT rc{};
            GetClientRect(h, &rc);
            s->monitor.dpi = GetDpiForWindow(h);
            const HRESULT hr = s->resize(device_, rc.right, rc.bottom);
            if (render::isDeviceLost(hr)) {
                handleDeviceLost(hr);
            } else if (rc.right > 0 && rc.bottom > 0) {
                POINT o{0, 0};
                ClientToScreen(h, &o);
                s->monitor.rect = {o.x, o.y, o.x + rc.right, o.y + rc.bottom};
                prepareSurface(*s);
                rebuildCanvases();
                renderAllNow(); // redesenhar já, mesmo dentro do loop modal de resize
            }
        }
        updatePause();
        return 0;
    }

    case render::WM_APP_SURFACE_CLOSE:
        PostQuitMessage(0);
        return 0;

    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        scheduleLayout();
        return 0;
    case WM_SETTINGCHANGE:
        // Escala, barra de tarefas, área de trabalho... qualquer uma pode mexer no layout.
        scheduleLayout();
        break;
    case WM_DEVICECHANGE:
        if (wp == DBT_DEVNODES_CHANGED) scheduleLayout();
        return TRUE;

    case WM_WTSSESSION_CHANGE:
        switch (wp) {
        case WTS_SESSION_LOCK:
        case WTS_CONSOLE_DISCONNECT:
        case WTS_REMOTE_DISCONNECT:
            pause_.global.sessionLocked = true;
            break;
        case WTS_SESSION_UNLOCK:
        case WTS_CONSOLE_CONNECT:
        case WTS_REMOTE_CONNECT:
            pause_.global.sessionLocked = false;
            break;
        default:
            break;
        }
        updatePause();
        return 0;

    case WM_POWERBROADCAST:
        switch (wp) {
        case PBT_APMSUSPEND:
            pause_.global.suspended = true;
            updatePause();
            break;
        case PBT_APMRESUMEAUTOMATIC:
        case PBT_APMRESUMESUSPEND:
            if (pause_.global.suspended) {
                pause_.global.suspended = false;
                scheduleLayout(); // os monitores podem ter mudado durante a suspensão
                updatePause();
            }
            break;
        case PBT_POWERSETTINGCHANGE: {
            const auto* ps = reinterpret_cast<const POWERBROADCAST_SETTING*>(lp);
            if (ps && IsEqualGUID(ps->PowerSetting, GUID_POWER_SAVING_STATUS) && ps->DataLength >= sizeof(DWORD)) {
                pause_.global.batterySaver = *reinterpret_cast<const DWORD*>(ps->Data) != 0;
                log::info(L"Poupança de bateria: {}", pause_.global.batterySaver ? L"ligada" : L"desligada");
                updatePause();
            }
            break;
        }
        default:
            break;
        }
        return TRUE;

    case WM_TIMER:
        onTimer(wp);
        return 0;

    case WM_ENDSESSION:
        if (wp) {
            shutdown(); // o Windows vai encerrar; repor o wallpaper já
        }
        return 0;

    case WM_CLOSE:
        // "taskkill /im asciipaper.exe" (sem /f), instaladores, etc.: sair com limpeza.
        log::info(L"WM_CLOSE recebido: a sair");
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(controller_, msg, wp, lp);
}

void App::onTimer(UINT_PTR id) {
    switch (id) {
    case TIMER_LAYOUT:
    case TIMER_LAYOUT_LATE:
        KillTimer(controller_, id);
        syncLayout();
        break;
    case TIMER_PAUSE_CHECK:
        updatePause();
        break;
    case TIMER_PAUSE_DEBOUNCE:
        KillTimer(controller_, id);
        updatePause();
        break;
    case TIMER_REATTACH:
        KillTimer(controller_, id);
        destroySurfaces();
        canvases_.clear();
        if (attachDesktop()) {
            log::info(L"Ligado ao desktop");
            reattachAttempts_ = 0;
        } else if (++reattachAttempts_ < 30) {
            scheduleReattach(1000);
        } else {
            log::error(L"Desisti de ligar ao desktop após {} tentativas", reattachAttempts_);
        }
        break;
    case TIMER_DEVICE_RETRY:
        KillTimer(controller_, id);
        handleDeviceLost(DXGI_ERROR_DEVICE_RESET);
        break;
    case TIMER_HOT_RELOAD:
        KillTimer(controller_, id);
        log::info(L"Alterações em {}: a recarregar", wallpaper_.watchDir.wstring());
        loadWallpaperAsync(wallpaperPath_);
        break;
    case TIMER_BENCH:
        finishBench();
        break;
    case TIMER_SCREENSHOT:
        KillTimer(controller_, id);
        captureRequested_ = true;
        break;
    default:
        break;
    }
}

} // namespace ap::app
