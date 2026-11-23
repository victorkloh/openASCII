#pragma once
// Encontra a janela do Explorer que fica ATRÁS dos ícones do ambiente de trabalho e
// "adota" as nossas janelas como filhas dela. Ver docs/ARCHITECTURE.md, secção WorkerW.

#include <windows.h>

#include <string>
#include <vector>

#include "core/Geometry.h"

namespace ap::desktop {

enum class Layout {
    None,
    // Windows 10 / 11 até 23H2: depois da mensagem 0x052C o Explorer cria um WorkerW
    // de topo, logo a seguir à janela que contém os ícones. Desenhamos dentro dele.
    Classic,
    // Windows 11 24H2+: o WorkerW e o SHELLDLL_DefView (ícones) são FILHOS do Progman.
    // Ficamos filhos do Progman, entre os ícones (por cima) e o WorkerW (por baixo).
    Raised,
    // Último recurso: filho do Progman, abaixo dos ícones se os encontrarmos.
    ProgmanFallback,
};

const wchar_t* toString(Layout layout);

class WorkerW {
public:
    // Procura a janela anfitriã. Devolve false se o Explorer não estiver pronto
    // (ex.: a reiniciar); quem chama deve tentar outra vez mais tarde.
    bool attach();
    // Esquece a janela anfitriã (não destrói as nossas janelas).
    void detach();
    bool reattach() {
        detach();
        return attach();
    }

    bool isValid() const { return host_ != nullptr && IsWindow(host_); }
    Layout layout() const { return layout_; }
    HWND host() const { return host_; }

    // Torna `child` (criada como WS_POPUP escondida) numa filha da anfitriã.
    bool adopt(HWND child) const;
    // Posiciona `child` em `screenRect` (coordenadas de ecrã) com a ordem Z correta.
    // `frameChanged` só é preciso na primeira vez, logo a seguir a adopt() mudar o estilo.
    void place(HWND child, const Rect& screenRect, bool frameChanged = false) const;
    // true se `child` já ocupa `screenRect` (evita mexer na janela sem necessidade).
    static bool isPlaced(HWND child, const Rect& screenRect);
    // Origem da área cliente da anfitriã, em coordenadas de ecrã.
    Point hostOrigin() const;

    // Layouts em que somos filhos do Progman (24H2+ e fallback): as nossas janelas têm de
    // ficar logo abaixo dos ícones e por cima de tudo o resto, sobretudo do WorkerW que pinta
    // o fundo estático do Windows. O Explorer reordena os filhos quando troca o fundo (ex.:
    // Windows Spotlight), e o fundo do Windows aparecia por cima do nosso até à próxima
    // passagem de layout. Corrige a ordem se for preciso e devolve o que estava por cima
    // (para o log); vazio = estava tudo bem.
    std::wstring enforceOrder(const std::vector<HWND>& ours) const;
    // Processo do Explorer dono da anfitriã (para filtrar eventos de janela).
    DWORD explorerProcessId() const;

private:
    HWND progman_ = nullptr;
    HWND host_ = nullptr;        // pai das nossas janelas
    HWND insertAfter_ = nullptr; // irmão por baixo do qual ficamos (ícones), ou nullptr
    HWND workerChild_ = nullptr; // só no layout Raised: o WorkerW que desenha o fundo
    Layout layout_ = Layout::None;
};

} // namespace ap::desktop
