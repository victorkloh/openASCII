#pragma once
// Rato global, sem foco e SEM mensagens: lido uma vez por frame (posição + botões).
//
// Porquê não Raw Input? Medido na v0.3: com RIDEV_INPUTSINK o Windows entrega uma mensagem
// WM_INPUT por cada movimento do rato no sistema inteiro (centenas a milhares por segundo),
// mesmo com o cursor sobre outras janelas. E como as nossas janelas são filhas de uma janela
// do Explorer, a nossa fila de input está ligada à dele: essas mensagens misturam-se com o
// input do ambiente de trabalho. Resultado: o fundo perdia FPS ao mexer o rato.
//
// Ler o estado uma vez por frame dá a mesma informação para um wallpaper (posição, botões,
// cliques) com zero mensagens, e só quando há algo a desenhar. E nunca um hook WH_MOUSE_LL,
// que poria o nosso processo no caminho de cada evento do rato de toda a gente.

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
    // Chamar no início de cada frame.
    void update();

    InteractionMode mode = InteractionMode::DesktopOnly;
    HWND windowedTarget = nullptr; // em --windowed, a "área de desktop" é esta janela

    POINT cursor{};           // ecrã virtual, px físicos
    bool overDesktop = false; // o cursor está sobre o desktop (ou mode == Always)
    unsigned buttons = 0;     // botões premidos cujo clique começou no desktop
    float scroll = 0.0f;      // reservado (sempre 0: a roda não se consegue ler sem mensagens)

    // Último clique que começou no desktop.
    POINT clickPos{};
    unsigned clickButton = 0;
    LONGLONG clickQpc = 0; // 0 = nunca

private:
    bool acceptsAt(POINT pt) const;
    bool wasDown_[3] = {};
};

} // namespace ap::input
