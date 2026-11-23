#include "input/MouseInput.h"

#include "input/DesktopHitTest.h"

namespace ap::input {

bool MouseInput::acceptsAt(POINT pt) const {
    if (windowedTarget) {
        return WindowFromPoint(pt) == windowedTarget;
    }
    return mode == InteractionMode::Always || isDesktopAt(pt);
}

void MouseInput::update() {
    GetCursorPos(&cursor);
    overDesktop = acceptsAt(cursor);
    scroll = 0.0f;

    // GetAsyncKeyState lê os botões FÍSICOS; com os botões trocados (canhotos) o esquerdo
    // lógico é o direito físico.
    const bool swapped = GetSystemMetrics(SM_SWAPBUTTON) != 0;
    const int keys[3] = {swapped ? VK_RBUTTON : VK_LBUTTON, swapped ? VK_LBUTTON : VK_RBUTTON, VK_MBUTTON};
    const unsigned bits[3] = {MouseLeft, MouseRight, MouseMiddle};
    for (int b = 0; b < 3; ++b) {
        const bool down = (GetAsyncKeyState(keys[b]) & 0x8000) != 0;
        if (down && !wasDown_[b] && overDesktop) {
            // Um clique conta só se começou no desktop: clicar num browser não faz ondas.
            buttons |= bits[b];
            clickPos = cursor;
            clickButton = bits[b];
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            clickQpc = now.QuadPart;
            ++clickSerial;
        }
        if (!down) buttons &= ~bits[b];
        wasDown_[b] = down;
    }
}

} // namespace ap::input
