#pragma once
// Rato global, sem foco: Raw Input com RIDEV_INPUTSINK numa janela message-only.
//
// Porquê Raw Input e não um hook WH_MOUSE_LL? Um hook de baixo nível põe o nosso processo
// no caminho de CADA evento do rato do sistema inteiro: se demorarmos, o cursor de toda a
// gente fica lento, e o Windows remove hooks que demoram demais. O Raw Input é só uma
// cópia dos eventos entregue na nossa fila: não atrasa ninguém.
//
// A posição absoluta vem de GetCursorPos (o Raw Input dá movimentos relativos).

#include <windows.h>

namespace ap::input {

enum class InteractionMode {
    DesktopOnly, // só reage quando o rato está sobre o ambiente de trabalho (por defeito)
    Always,      // reage mesmo por cima de outras janelas
};

enum MouseButton : unsigned {
    MouseLeft = 1,
    MouseRight = 2,
    MouseMiddle = 4,
};

class MouseInput {
public:
    ~MouseInput();

    bool create(HINSTANCE instance);
    // Registar/desregistar o Raw Input. Em pausa desligamos: o sistema deixa de nos
    // acordar a cada movimento do rato (0% de CPU a sério).
    void setEnabled(bool enabled);

    // Chamar no início de cada frame.
    void update();

    InteractionMode mode = InteractionMode::DesktopOnly;
    HWND windowedTarget = nullptr; // em --windowed, a "área de desktop" é esta janela

    POINT cursor{};          // ecrã virtual, px físicos
    bool overDesktop = false; // o cursor está sobre o desktop (ou mode == Always)
    unsigned buttons = 0;     // botões premidos cujo clique começou no desktop
    float scroll = 0.0f;      // entalhes desde o último update(), + = para cima

    // Último clique que começou no desktop.
    POINT clickPos{};
    unsigned clickButton = 0;
    LONGLONG clickQpc = 0; // 0 = nunca

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    void onRawInput(HRAWINPUT handle);
    bool acceptsAt(POINT pt) const;

    HWND hwnd_ = nullptr;
    bool enabled_ = false;
    float pendingScroll_ = 0.0f;
};

} // namespace ap::input
