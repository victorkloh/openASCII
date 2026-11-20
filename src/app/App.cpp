#include "app/App.h"

#include "desktop/MonitorManager.h"
#include "desktop/Wallpaper.h"
#include "render/Screenshot.h"
#include "resource.h"
#include "util/Log.h"
#include "util/Win32.h"
#include "version.h"

#include <dbt.h>
#include <psapi.h>
#include <shellapi.h>
#include <wtsapi32.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace ap::app {

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kControllerClass[] = L"AsciiPaperController";
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

    // O wallpaper é carregado e compilado ANTES de criar as janelas: assim o primeiro
    // frame já é o wallpaper a sério (sem flash preto nem cor provisória).
    wallpaper::loadShaderLibrary();
    loadWallpaperSync(initialWallpaperPath());

    if (options_.benchSeconds > 0) {
        benchmarking_ = true;
        benchStartQpc_ = qpcNow();
        benchStartCpu100ns_ = processCpu100ns();
        frameTimesMs_.reserve(static_cast<size_t>(options_.benchSeconds) * 64);
        SetTimer(controller_, TIMER_BENCH, static_cast<UINT>(options_.benchSeconds) * 1000u, nullptr);
        log::info(L"--bench: a medir durante {} s (pausas ignoradas)", options_.benchSeconds);
    }

    if (options_.windowed) {
        return createWindowedSurface();
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
    winEventHooks_.clear();
    batterySaverNotify_.reset();
    if (sessionNotify_) {
        WTSUnRegisterSessionNotification(controller_);
        sessionNotify_ = false;
    }
    tray_.remove();
    destroySurfaces();
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
// Wallpaper: carregar, compilar, hot reload
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
        job.shader = wallpaper::compileWallpaperShader(job.load.wallpaper.manifest, job.load.wallpaper.source,
                                                       job.load.wallpaper.manifest.entry);
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
    wallpaperPath_ = job.path;
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
        }
    } else {
        if (FAILED(program_.create(device_.device(), job.shader.blob.Get()))) {
            errorText_ = "CreateComputeShader falhou (ver log)";
        } else {
            if (!job.shader.errors.empty()) {
                log::debug(L"Avisos do compilador: {}", widen(job.shader.errors));
            }
            wallpaper_ = load.wallpaper;
            hasWallpaper_ = true;
            errorText_.clear();
            settings_.fps = wallpaper_.manifest.fps;
            for (const auto& w : wallpaper_.warnings) log::warn(L"{}: {}", widen(wallpaper_.label), widen(w));
            log::info(L"Wallpaper ativo: {} ({} fps, célula {} px lógicos, fonte \"{}\")", widen(wallpaper_.label),
                      wallpaper_.manifest.fps, wallpaper_.manifest.cell, widen(wallpaper_.manifest.font));
        }
    }

    // Hot reload: vigiar a pasta do wallpaper (mesmo com erro, para apanhar a correção).
    const fs::path watch = !load.wallpaper.watchDir.empty() ? load.wallpaper.watchDir
                           : !load.wallpaper.dir.empty()   ? load.wallpaper.dir
                                                           : wallpaper_.watchDir;
    if (!watch.empty()) watcher_.start(watch);

    prepareAllSurfaces();
    for (auto& s : surfaces_) {
        if (s->hasSwapChain()) renderSurfaceNow(*s);
    }
    if (controller_ && !surfaces_.empty()) updatePause(); // o fps pode ter mudado
}

void App::selectWallpaper(size_t index) {
    if (index >= catalog_.size()) return;
    const fs::path& p = catalog_[index].path;
    // Guardar na config de forma portável: relativo à pasta de wallpapers, se possível.
    std::string stored = wallpaper::toUtf8Path(p);
    for (const fs::path& root : wallpaper::wallpaperRoots()) {
        std::error_code ec;
        const fs::path rel = fs::relative(p, root, ec);
        if (!ec && !rel.empty() && rel.native().rfind(L"..", 0) != 0) {
            stored = wallpaper::toUtf8Path(rel.generic_wstring());
            break;
        }
    }
    config_.wallpaper = stored;
    if (!config_.save()) log::warn(L"Não consegui gravar {}", Config::path().wstring());
    log::info(L"Wallpaper escolhido na tray: {}", widen(catalog_[index].label));
    loadWallpaperAsync(p);
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
    if (!s.atlas.valid() || s.atlas.key() != desc.key()) {
        if (HRESULT hr = s.atlas.build(device_.device(), dwrite_.Get(), desc); FAILED(hr)) return hr;
        s.grid.reset(); // células de outro tamanho
    }
    return s.grid.resize(device_.device(), s.width(), s.height(), s.atlas);
}

void App::prepareAllSurfaces() {
    for (auto& s : surfaces_) prepareSurface(*s);
}

wallpaper::FrameConstants App::makeFrame(render::MonitorSurface& s, double time, double dt) const {
    wallpaper::FrameConstants f{};
    const GridLayout& g = s.grid.layout();
    const Manifest& m = wallpaper_.manifest;
    const Rect& r = s.monitor.rect;
    const float cw = static_cast<float>(g.cellW), ch = static_cast<float>(g.cellH);

    f.time = static_cast<float>(time);
    f.dt = static_cast<float>(dt);
    f.gridW = static_cast<uint32_t>(g.cols);
    f.gridH = static_cast<uint32_t>(g.rows);

    auto toCellX = [&](LONG x) { return (static_cast<float>(x - r.left - g.originX)) / cw; };
    auto toCellY = [&](LONG y) { return (static_cast<float>(y - r.top - g.originY)) / ch; };
    const POINT c = mouse_.cursor;
    const bool onThisMonitor = c.x >= r.left && c.x < r.right && c.y >= r.top && c.y < r.bottom;
    const bool inside = onThisMonitor && mouse_.overDesktop;
    f.mouseX = toCellX(c.x);
    f.mouseY = toCellY(c.y);
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

    f.charsetSize = static_cast<uint32_t>(s.atlas.charsetSize());
    toFloat4(m.background, f.background);
    f.paletteCount = static_cast<uint32_t>(std::min<size_t>(m.palette.size(), 8));
    for (uint32_t i = 0; i < f.paletteCount; ++i) toFloat4(m.palette[i], f.palette[i]);
    f.frameIndex = frameIndex_;
    f.cellAspect = cw / ch;
    for (size_t i = 0; i < m.params.size() && i < 16; ++i) f.params[i] = m.params[i].value;
    f.cellPxW = cw;
    f.cellPxH = ch;
    f.resolutionW = static_cast<float>(s.width());
    f.resolutionH = static_cast<float>(s.height());
    return f;
}

void App::drawError(render::MonitorSurface& s) {
    const Rgba8 fg{255, 120, 120, 255}, bg{24, 4, 6, 235}, dim{200, 200, 210, 255};
    s.grid.writeText(device_.context(), s.atlas, 1, 1, " AsciiPaper: erro no wallpaper (o último estado bom continua) ",
                     dim, bg);
    std::istringstream lines(errorText_);
    std::string line;
    int row = 2;
    while (std::getline(lines, line) && row < 14) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        s.grid.writeText(device_.context(), s.atlas, 1, row++, " " + line + " ", fg, bg);
    }
}

HRESULT App::renderSurface(render::MonitorSurface& s, double time, double dt) {
    if (!s.hasSwapChain()) return S_OK;
    if ((!s.atlas.valid() || !s.grid.valid()) && FAILED(prepareSurface(s))) {
        return S_OK; // sem wallpaper utilizável: nada para desenhar (fica o último frame)
    }
    if (options_.windowed && s.hwnd()) {
        // Em janela, a "área do monitor" é a área cliente, onde quer que a janela esteja.
        POINT o{0, 0};
        ClientToScreen(s.hwnd(), &o);
        s.monitor.rect = {o.x, o.y, o.x + s.width(), o.y + s.height()};
    }
    auto* ctx = device_.context();
    if (program_.valid()) {
        program_.dispatch(ctx, s.grid, makeFrame(s, time, dt));
    } else {
        s.grid.clear(ctx);
    }
    if (!errorText_.empty()) drawError(s);
    s.grid.draw(ctx, pipeline_, s.rtv(), s.atlas, wallpaper_.manifest.background);
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

void App::renderSurfaceNow(render::MonitorSurface& s) {
    const double dt = 1.0 / std::clamp(settings_.fps, 1, settings_.maxFps);
    const HRESULT hr = renderSurface(s, now(), dt);
    if (render::isDeviceLost(hr)) {
        handleDeviceLost(hr);
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

    for (auto& s : surfaces_) {
        if (any(s->pause) || !s->hasSwapChain()) continue;
        const HRESULT hr = renderSurface(*s, t, dt);
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
    // Primeiro frame ANTES de mostrar a janela: é isto que evita o flash preto.
    renderSurfaceNow(*s);
    s->show();
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
    s->monitor.dpi = GetDpiForWindow(s->hwnd());
    s->monitor.device = L"(janela)";
    mouse_.windowedTarget = s->hwnd();
    if (FAILED(s->createSwapChain(device_, rc.right, rc.bottom))) {
        return false;
    }
    prepareSurface(*s);
    renderSurfaceNow(*s);
    if (options_.screenshot.empty()) {
        s->show();
    } else {
        // --screenshot: a janela nunca aparece; deixamos a animação correr 3 s e gravamos.
        SetTimer(controller_, TIMER_SCREENSHOT, 3000, nullptr);
    }
    surfaces_.push_back(std::move(s));
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
    unsigned index = 0;
    for (const auto& m : monitors) {
        auto it = std::find_if(surfaces_.begin(), surfaces_.end(),
                               [&](const auto& s) { return s->monitor.device == m.device; });
        if (it == surfaces_.end()) {
            log::info(L"Monitor {} ligado", m.device);
            if (auto s = createSurface(m, index)) surfaces_.push_back(std::move(s));
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
            // Primeiro os buffers, depois atlas/grelha para o DPI novo, depois a janela e um
            // frame novo: assim nunca se vê um frame antigo esticado ou cortado.
            const HRESULT hr = s.resize(device_, m.rect.width(), m.rect.height());
            if (render::isDeviceLost(hr)) {
                handleDeviceLost(hr);
                return;
            }
            prepareSurface(s);
            // A origem do WorkerW muda se aparecer um monitor à esquerda/acima, por isso
            // reposicionamos sempre (é barato).
            workerw_.place(s.hwnd(), m.rect);
            renderSurfaceNow(s);
        }
        ++index;
    }
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
        if (FAILED(renderSurface(*s, now(), 1.0 / settings_.fps))) return false;
    }
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
    for (size_t i = 0; i < surfaces_.size(); ++i) {
        auto& s = *surfaces_[i];
        PauseReason r = evaluatePause(pause_.global, coverage[i], pause_.rules);
        if (options_.windowed && s.hwnd() && IsIconic(s.hwnd())) {
            r |= PauseReason::User; // janela minimizada: nada para mostrar
        }
        if (benchmarking_) r = PauseReason::None; // o bench mede sempre
        if (r != s.pause) {
            log::info(L"Monitor {}: {} -> {}", s.monitor.device, describe(s.pause), describe(r));
            const bool resumed = any(s.pause) && !any(r);
            s.pause = r;
            if (resumed && s.hasSwapChain()) {
                renderSurfaceNow(s); // frame fresco logo, em vez de esperar pelo timer
            }
        }
        windowPause = windowPause || isWindowPause(r);
    }

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
        cells += std::format(L"\n  {}: {}x{} px, grelha {}x{} ({} células de {}x{} px)", s->monitor.device,
                             s->width(), s->height(), g.cols, g.rows, g.cellCount(), g.cellW, g.cellH);
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

void App::onCommand(UINT id) {
    if (id >= ID_TRAY_WALLPAPER_FIRST && id <= ID_TRAY_WALLPAPER_LAST) {
        selectWallpaper(id - ID_TRAY_WALLPAPER_FIRST);
        return;
    }
    switch (id) {
    case ID_TRAY_PAUSE:
        pause_.global.user = !pause_.global.user;
        updatePause();
        break;
    case ID_TRAY_RELOAD:
        loadWallpaperAsync(wallpaperPath_);
        break;
    case ID_TRAY_OPEN_WALLPAPERS: {
        const fs::path dir = wallpaper_.dir.empty() ? wallpaper::wallpaperRoots().front() : wallpaper_.dir.parent_path();
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
    case ID_TRAY_OPEN_LOG:
        ShellExecuteW(nullptr, L"open", log::folder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case ID_TRAY_EXIT:
        PostQuitMessage(0);
        break;
    default:
        break;
    }
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
        case NIN_KEYSELECT: {
            catalog_ = wallpaper::listWallpapers(); // reler: pode haver wallpapers novos
            TrayMenuState state;
            state.paused = userPaused();
            std::error_code ec;
            for (size_t i = 0; i < catalog_.size(); ++i) {
                state.wallpapers.push_back(widen(catalog_[i].label));
                if (fs::equivalent(catalog_[i].path, wallpaperPath_, ec)) state.current = static_cast<int>(i);
            }
            tray_.showMenu(state);
            break;
        }
        default:
            break;
        }
        return 0;

    case WM_COMMAND:
        onCommand(LOWORD(wp));
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
                prepareSurface(*s);
                renderSurfaceNow(*s); // redesenhar já, mesmo dentro do loop modal de resize
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
