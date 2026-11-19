#include "desktop/MonitorManager.h"

#include "util/Win32.h"

#include <shellscalingapi.h>

namespace ap::desktop {

namespace {

BOOL CALLBACK collect(HMONITOR mon, HDC, LPRECT, LPARAM lp) {
    auto* out = reinterpret_cast<std::vector<MonitorInfo>*>(lp);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) {
        return TRUE;
    }
    MonitorInfo info;
    info.handle = mon;
    info.device = mi.szDevice;
    info.rect = win::toRect(mi.rcMonitor);
    info.work = win::toRect(mi.rcWork);
    info.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

    // Como as nossas janelas são filhas (não recebem WM_DPICHANGED), relemos o DPI
    // diretamente do monitor sempre que algo muda.
    UINT dx = 96, dy = 96;
    if (SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy))) {
        info.dpi = dx;
    }
    out->push_back(std::move(info));
    return TRUE;
}

} // namespace

std::vector<MonitorInfo> enumerateMonitors() {
    std::vector<MonitorInfo> out;
    EnumDisplayMonitors(nullptr, nullptr, collect, reinterpret_cast<LPARAM>(&out));
    return out;
}

} // namespace ap::desktop
