#pragma once
// Enumeração dos monitores ligados, com o DPI efetivo de cada um.

#include <windows.h>

#include <string>
#include <vector>

#include "core/Geometry.h"

namespace ap::desktop {

struct MonitorInfo {
    HMONITOR handle = nullptr;
    std::wstring device; // ex.: L"\\\\.\\DISPLAY1" — estável enquanto o monitor estiver ligado
    Rect rect;           // em coordenadas de ecrã virtual (podem ser negativas)
    Rect work;           // área sem a barra de tarefas
    unsigned dpi = 96;   // 96 = 100%, 120 = 125%, 144 = 150%, 168 = 175%, 192 = 200%
    bool primary = false;

    bool sameLayout(const MonitorInfo& o) const { return rect == o.rect && dpi == o.dpi; }
};

std::vector<MonitorInfo> enumerateMonitors();

} // namespace ap::desktop
