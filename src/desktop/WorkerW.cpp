#include "desktop/WorkerW.h"

#include "util/Log.h"
#include "util/Win32.h"

namespace ap::desktop {

namespace {

// Mensagem não documentada que pede ao Progman para criar o WorkerW por trás dos ícones.
// É a mesma usada pelo Lively, Wallpaper Engine e afins. wParam/lParam = 0xD/0x1.
constexpr UINT kSpawnWorkerW = 0x052C;

struct ClassicSearch {
    HWND defViewParent = nullptr;
    HWND defView = nullptr;
};

BOOL CALLBACK findDefViewParent(HWND top, LPARAM lp) {
    auto* s = reinterpret_cast<ClassicSearch*>(lp);
    if (HWND dv = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr)) {
        s->defViewParent = top;
        s->defView = dv;
        return FALSE; // parar a enumeração
    }
    return TRUE;
}

} // namespace

const wchar_t* toString(Layout layout) {
    switch (layout) {
    case Layout::None: return L"none";
    case Layout::Classic: return L"classic (WorkerW de topo)";
    case Layout::Raised: return L"24H2+ (WorkerW filho do Progman)";
    case Layout::ProgmanFallback: return L"fallback (filho do Progman)";
    }
    return L"?";
}

bool WorkerW::attach() {
    detach();

    progman_ = FindWindowW(L"Progman", nullptr);
    if (!progman_) {
        log::warn(L"WorkerW: Progman não encontrado (o Explorer está a correr?)");
        return false;
    }

    // Timeout curto: se o Explorer estiver bloqueado não queremos bloquear também.
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(progman_, kSpawnWorkerW, 0xD, 0x1, SMTO_NORMAL, 1000, &result)) {
        log::warn(L"WorkerW: Progman não respondeu a 0x052C (erro {})", GetLastError());
    }

    // --- Layout 24H2+: tudo dentro do Progman. ---
    HWND defViewInProgman = FindWindowExW(progman_, nullptr, L"SHELLDLL_DefView", nullptr);
    HWND workerInProgman = FindWindowExW(progman_, nullptr, L"WorkerW", nullptr);
    if (defViewInProgman && workerInProgman) {
        host_ = progman_;
        insertAfter_ = defViewInProgman;
        workerChild_ = workerInProgman;
        layout_ = Layout::Raised;
    } else {
        // --- Layout clássico: a janela de topo com os ícones, e o WorkerW a seguir. ---
        ClassicSearch s;
        EnumWindows(findDefViewParent, reinterpret_cast<LPARAM>(&s));
        if (s.defViewParent) {
            if (HWND w = FindWindowExW(nullptr, s.defViewParent, L"WorkerW", nullptr)) {
                host_ = w;
                layout_ = Layout::Classic;
            }
        }
        if (!host_) {
            // --- Fallback: filhos do Progman, por baixo dos ícones se estiverem lá. ---
            host_ = progman_;
            insertAfter_ = defViewInProgman; // pode ser nullptr
            layout_ = Layout::ProgmanFallback;
        }
    }

    RECT rc{};
    GetClientRect(host_, &rc);
    const Point o = hostOrigin();
    log::info(L"WorkerW: estratégia = {}; anfitriã {} ({}), {}x{} com origem em ({}, {}); "
              L"ecrã virtual em ({}, {})",
              toString(layout_), win::hwndStr(host_), win::className(host_), rc.right, rc.bottom, o.x, o.y,
              GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN));
    return true;
}

void WorkerW::detach() {
    progman_ = host_ = insertAfter_ = workerChild_ = nullptr;
    layout_ = Layout::None;
}

bool WorkerW::adopt(HWND child) const {
    if (!isValid()) {
        return false;
    }
    // A documentação de SetParent pede: trocar WS_POPUP por WS_CHILD ANTES de mudar o pai.
    LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
    style = (style & ~static_cast<LONG_PTR>(WS_POPUP)) | WS_CHILD;
    SetWindowLongPtrW(child, GWL_STYLE, style);

    // Atenção: isto liga a nossa fila de input à do Explorer (pai noutro processo).
    // Se o nosso thread de UI bloquear, o desktop também bloqueia. Por isso o thread
    // de UI nunca faz trabalho longo.
    SetParent(child, host_);
    if (GetParent(child) != host_) {
        log::error(L"WorkerW: SetParent falhou (erro {})", GetLastError());
        return false;
    }
    return true;
}

void WorkerW::place(HWND child, const Rect& screenRect) const {
    const Rect r = toParentSpace(screenRect, hostOrigin());
    HWND after = insertAfter_ ? insertAfter_ : HWND_TOP;
    SetWindowPos(child, after, r.left, r.top, r.width(), r.height(),
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    if (workerChild_) {
        // 24H2+: o WorkerW (que pinta o wallpaper estático) tem de ficar por baixo de nós.
        SetWindowPos(workerChild_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

Point WorkerW::hostOrigin() const {
    POINT p{0, 0};
    if (host_) {
        ClientToScreen(host_, &p);
    }
    return {p.x, p.y};
}

} // namespace ap::desktop
