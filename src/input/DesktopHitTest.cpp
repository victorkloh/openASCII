#include "input/DesktopHitTest.h"

#include "util/Win32.h"

namespace ap::input {

bool isDesktopAt(POINT pt) {
    HWND h = WindowFromPoint(pt);
    if (!h) return false;
    // As classes do shell que formam o desktop, mais as nossas próprias janelas.
    const std::wstring cls = win::className(h);
    return cls == L"SysListView32" || cls == L"SHELLDLL_DefView" || cls == L"WorkerW" || cls == L"Progman" ||
           cls == L"OpenASCIISurface";
}

} // namespace ap::input
