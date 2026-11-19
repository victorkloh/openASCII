#include "app/App.h"

#include "desktop/MonitorManager.h"
#include "desktop/Wallpaper.h"
#include "resource.h"
#include "util/Log.h"
#include "util/Win32.h"
#include "version.h"

#include <dbt.h>
#include <shellapi.h>
#include <wtsapi32.h>

#include <algorithm>

namespace ap::app {

namespace {

constexpr wchar_t kControllerClass[] = L"AsciiPaperController";
constexpr UINT WM_APP_TRAY = WM_APP + 10;

enum TimerId : UINT_PTR {
    TIMER_LAYOUT = 1,      // mudança de monitores/DPI (com debounce)
    TIMER_LAYOUT_LATE,     // segunda passagem: o Explorer pode redimensionar o WorkerW depois de nós
    TIMER_PAUSE_CHECK,     // reavaliar pausas de janela (1 Hz, só quando é preciso)
    TIMER_PAUSE_DEBOUNCE,  // depois de um evento de janela
    TIMER_REATTACH,        // voltar a ligar ao Explorer
    TIMER_DEVICE_RETRY,    // recriar o dispositivo D3D depois de um device lost
};

App* g_app = nullptr;
std::wstring g_originalWallpaper;

LONGLONG qpcNow() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return li.QuadPart;
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

double App::now() const {
    return static_cast<double>(qpcNow() - qpcStart_) / static_cast<double>(qpcFrequency_);
}

// ---------------------------------------------------------------------------
// Arranque e paragem
// ---------------------------------------------------------------------------

bool App::init() {
    const auto awareness = GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext());
    log::info(L"AsciiPaper {} a arrancar (modo {}, DPI awareness {})", ASCIIPAPER_VERSION_WSTR,
              options_.windowed ? L"janela" : L"wallpaper", static_cast<int>(awareness));
    if (options_.benchSeconds > 0) {
        log::warn(L"--bench ainda não está implementado (Fase 2); ignorado");
    }
    if (!options_.wallpaper.empty()) {
        log::warn(L"--wallpaper ainda não está implementado (Fase 3); ignorado");
    }

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
    if (FAILED(device_.create())) {
        return false;
    }

    if (options_.windowed) {
        return createWindowedSurface();
    }

    // ---- Modo wallpaper ----
    g_originalWallpaper = desktop::currentWallpaper();
    log::info(L"Wallpaper original: \"{}\"", g_originalWallpaper);

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
    winEventHooks_.clear();
    batterySaverNotify_.reset();
    if (sessionNotify_) {
        WTSUnRegisterSessionNotification(controller_);
        sessionNotify_ = false;
    }
    tray_.remove();
    destroySurfaces();
    device_.reset();
    if (!options_.windowed) {
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
        // Em pausa não esperamos pelo timer: o thread dorme até chegar uma mensagem.
        const bool active = frameTimerArmed_;
        HANDLE handles[] = {frameTimer_.get()};
        const DWORD r = MsgWaitForMultipleObjectsEx(active ? 1 : 0, handles, INFINITE, QS_ALLINPUT,
                                                    MWMO_INPUTAVAILABLE);
        if (active && r == WAIT_OBJECT_0) {
            frameTimerArmed_ = false;
            renderFrame();
            if (anyActive()) {
                armFrameTimer(false);
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

void App::renderFrame() {
    const double t = now();
    for (auto& s : surfaces_) {
        if (any(s->pause) || !s->hasSwapChain()) continue;
        const HRESULT hr = s->render(device_, t);
        if (render::isDeviceLost(hr)) {
            handleDeviceLost(hr);
            return;
        }
    }
}

void App::renderSurfaceNow(render::MonitorSurface& s) {
    const HRESULT hr = s.render(device_, now());
    if (render::isDeviceLost(hr)) {
        handleDeviceLost(hr);
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
    // Primeiro frame ANTES de mostrar a janela: é isto que evita o flash preto.
    renderSurfaceNow(*s);
    s->show();
    log::info(L"Monitor {}{}: {}x{} em ({}, {}), {} DPI ({}%), swapchain {}", mon.device,
              mon.primary ? L" (principal)" : L"", mon.rect.width(), mon.rect.height(), mon.rect.left,
              mon.rect.top, mon.dpi, mon.dpi * 100 / 96, s->usesFlipModel() ? L"flip" : L"blt");
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
    if (FAILED(s->createSwapChain(device_, rc.right, rc.bottom))) {
        return false;
    }
    renderSurfaceNow(*s);
    s->show();
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
            // Primeiro os buffers, depois a janela, depois um frame novo: assim nunca se
            // vê um frame antigo esticado ou cortado no tamanho novo.
            const HRESULT hr = s.resize(device_, m.rect.width(), m.rect.height());
            if (render::isDeviceLost(hr)) {
                handleDeviceLost(hr);
                return;
            }
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
    device_.reset();
    if (FAILED(device_.create()) || !recreateSwapChains()) {
        // O driver pode demorar uns segundos a voltar.
        SetTimer(controller_, TIMER_DEVICE_RETRY, 1000, nullptr);
        return;
    }
    updatePause();
}

bool App::recreateSwapChains() {
    for (auto& s : surfaces_) {
        if (!s->hwnd()) continue;
        RECT rc{};
        GetClientRect(s->hwnd(), &rc);
        if (FAILED(s->createSwapChain(device_, rc.right, rc.bottom))) {
            return false;
        }
        const HRESULT hr = s->render(device_, now());
        if (FAILED(hr)) {
            return false;
        }
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
    // Algumas mudanças (ex.: Win+↓ numa janela maximizada) não geram nenhum dos eventos
    // que ouvimos, por isso reavaliamos 1x por segundo enquanto houver algo a desenhar
    // ou uma pausa causada por uma janela. Bloqueio/suspensão = sem timers nenhuns.
    if (!options_.windowed && (active || windowPause)) {
        SetTimer(controller_, TIMER_PAUSE_CHECK, 1000, nullptr);
    } else {
        KillTimer(controller_, TIMER_PAUSE_CHECK);
    }
    if (active && !frameTimerArmed_) {
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
            tray_.showMenu(userPaused());
            break;
        default:
            break;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_TRAY_PAUSE:
            pause_.global.user = !pause_.global.user;
            updatePause();
            break;
        case ID_TRAY_OPEN_LOG:
            ShellExecuteW(nullptr, L"open", log::folder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case ID_TRAY_EXIT:
            PostQuitMessage(0);
            break;
        default:
            break;
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
    default:
        break;
    }
}

} // namespace ap::app
