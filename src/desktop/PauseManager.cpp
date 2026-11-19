#include "desktop/PauseManager.h"

#include "util/Win32.h"

#include <dwmapi.h>
#include <shellapi.h>

namespace ap::desktop {

namespace {

// Janelas do próprio shell que nunca contam como "a tapar o wallpaper".
bool isShellWindow(HWND h) {
    const std::wstring cls = win::className(h);
    return cls == L"Progman" || cls == L"WorkerW" || cls == L"Shell_TrayWnd" ||
           cls == L"Shell_SecondaryTrayWnd";
}

bool isOwnWindow(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return pid == GetCurrentProcessId();
}

// Janelas "cloaked" existem mas não se veem (outro ambiente de trabalho virtual,
// apps UWP suspensas). O DWM sabe; IsWindowVisible não.
bool isCloaked(HWND h) {
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
}

int monitorIndex(const std::vector<MonitorInfo>& monitors, HMONITOR mon) {
    for (size_t i = 0; i < monitors.size(); ++i) {
        if (monitors[i].handle == mon) return static_cast<int>(i);
    }
    return -1;
}

struct MaximizedScan {
    const std::vector<MonitorInfo>* monitors;
    std::vector<MonitorCoverage>* out;
};

BOOL CALLBACK markMaximized(HWND h, LPARAM lp) {
    auto* s = reinterpret_cast<MaximizedScan*>(lp);
    if (!IsWindowVisible(h) || IsIconic(h) || !IsZoomed(h)) return TRUE;
    if (isCloaked(h) || isShellWindow(h) || isOwnWindow(h)) return TRUE;
    const int idx = monitorIndex(*s->monitors, MonitorFromWindow(h, MONITOR_DEFAULTTONULL));
    if (idx >= 0) (*s->out)[static_cast<size_t>(idx)].maximized = true;
    return TRUE;
}

} // namespace

std::vector<MonitorCoverage> PauseManager::scan(const std::vector<MonitorInfo>& monitors) const {
    std::vector<MonitorCoverage> out(monitors.size());

    // 1) Ecrã inteiro: só a janela em primeiro plano pode estar em ecrã inteiro.
    HWND fg = GetForegroundWindow();
    if (fg && !isShellWindow(fg) && !isOwnWindow(fg) && !isCloaked(fg)) {
        const int idx = monitorIndex(monitors, MonitorFromWindow(fg, MONITOR_DEFAULTTONULL));
        if (idx >= 0) {
            // O Windows diz-nos se há um jogo D3D exclusivo / apresentação / "não incomodar"...
            QUERY_USER_NOTIFICATION_STATE quns{};
            const bool busy = SUCCEEDED(SHQueryUserNotificationState(&quns)) &&
                              (quns == QUNS_BUSY || quns == QUNS_RUNNING_D3D_FULL_SCREEN ||
                               quns == QUNS_PRESENTATION_MODE);
            // ...e a geometria apanha o resto (vídeo no browser, jogos borderless).
            // Uma janela maximizada também "cobre" o monitor (as bordas saem uns píxeis
            // para fora), por isso essas ficam para a regra de maximizadas.
            RECT wr{};
            GetWindowRect(fg, &wr);
            const bool geometric =
                !IsZoomed(fg) && covers(win::toRect(wr), monitors[static_cast<size_t>(idx)].rect);
            out[static_cast<size_t>(idx)].fullscreen = busy || geometric;
        }
    }

    // 2) Maximizadas: qualquer janela visível, não só a de primeiro plano.
    if (rules.pauseOnMaximized) {
        MaximizedScan s{&monitors, &out};
        EnumWindows(markMaximized, reinterpret_cast<LPARAM>(&s));
    }
    return out;
}

} // namespace ap::desktop
