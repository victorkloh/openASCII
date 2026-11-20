#include "input/MouseInput.h"

#include "input/DesktopHitTest.h"
#include "util/Log.h"

namespace ap::input {

namespace {
constexpr wchar_t kClassName[] = L"AsciiPaperInput";
}

MouseInput::~MouseInput() {
    setEnabled(false);
    if (hwnd_) DestroyWindow(hwnd_);
}

bool MouseInput::create(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    // HWND_MESSAGE: janela invisível que só existe para receber mensagens (aqui o WM_INPUT).
    hwnd_ = CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
    return hwnd_ != nullptr;
}

void MouseInput::setEnabled(bool enabled) {
    if (enabled == enabled_ || !hwnd_) return;
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
    rid.usUsage = 0x02;     // HID_USAGE_GENERIC_MOUSE
    rid.dwFlags = enabled ? RIDEV_INPUTSINK : RIDEV_REMOVE; // INPUTSINK: recebe mesmo sem foco
    rid.hwndTarget = enabled ? hwnd_ : nullptr;
    if (RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        enabled_ = enabled;
    } else {
        log::warn(L"RegisterRawInputDevices falhou (erro {})", GetLastError());
    }
    if (!enabled) buttons = 0;
}

bool MouseInput::acceptsAt(POINT pt) const {
    if (windowedTarget) {
        return WindowFromPoint(pt) == windowedTarget;
    }
    return mode == InteractionMode::Always || isDesktopAt(pt);
}

void MouseInput::update() {
    GetCursorPos(&cursor);
    overDesktop = acceptsAt(cursor);
    scroll = pendingScroll_;
    pendingScroll_ = 0.0f;
    // Se o botão foi largado enquanto o Raw Input estava desligado, não ficar "preso".
    if ((buttons & MouseLeft) && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) buttons &= ~MouseLeft;
    if ((buttons & MouseRight) && !(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) buttons &= ~MouseRight;
    if ((buttons & MouseMiddle) && !(GetAsyncKeyState(VK_MBUTTON) & 0x8000)) buttons &= ~MouseMiddle;
}

void MouseInput::onRawInput(HRAWINPUT handle) {
    RAWINPUT raw{};
    UINT size = sizeof(raw);
    if (GetRawInputData(handle, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) return;
    if (raw.header.dwType != RIM_TYPEMOUSE) return;

    const USHORT flags = raw.data.mouse.usButtonFlags;
    if (flags == 0) return; // só movimento: a posição lemos com GetCursorPos no frame

    POINT pt{};
    GetCursorPos(&pt);
    struct Map {
        USHORT down, up;
        unsigned bit;
    };
    static constexpr Map map[] = {
        {RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, MouseLeft},
        {RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, MouseRight},
        {RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, MouseMiddle},
    };
    for (const Map& m : map) {
        if (flags & m.down) {
            // Só conta se o clique começou no desktop: clicar num browser não faz ondas.
            if (acceptsAt(pt)) {
                buttons |= m.bit;
                clickPos = pt;
                clickButton = m.bit;
                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                clickQpc = now.QuadPart;
            }
        }
        if (flags & m.up) buttons &= ~m.bit;
    }
    if ((flags & RI_MOUSE_WHEEL) && acceptsAt(pt)) {
        pendingScroll_ += static_cast<float>(static_cast<SHORT>(raw.data.mouse.usButtonData)) / WHEEL_DELTA;
    }
}

LRESULT CALLBACK MouseInput::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<MouseInput*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_INPUT && self) {
        self->onRawInput(reinterpret_cast<HRAWINPUT>(lp));
        // Com RIDEV_INPUTSINK em segundo plano (wp == RIM_INPUTSINK) não há nada para limpar;
        // em primeiro plano o DefWindowProc tem de ser chamado.
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace ap::input
