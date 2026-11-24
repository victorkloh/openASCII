#pragma once
// O "canvas": a grelha onde o wallpaper corre. No modo estendido há UM canvas que cobre
// todos os monitores (como o "Estender" do Windows): uma onda que sai de um ecrã entra no
// outro. Cada monitor mostra a sua parte. Lógica pura, testada.

#include <span>
#include <vector>

#include "core/Geometry.h"
#include "core/Grid.h"

namespace ap {

struct CanvasMonitor {
    Rect rectPx;     // retângulo do monitor no ecrã virtual (px físicos)
    GridLayout grid; // a grelha que o monitor desenha (células e margens dele)
    Rect workPx;     // área de trabalho (sem a barra de tarefas); vazia = o monitor todo
};

struct CanvasLayout {
    Point originPx;   // canto superior esquerdo da célula (0,0), em px do ecrã virtual
    int cols = 0;
    int rows = 0;
    int cellW = 1;    // tamanho de referência de uma célula do canvas (px físicos)
    int cellH = 1;
    std::vector<Rect> monitorCells; // por monitor: a zona do canvas que ele mostra (em células)
    // Por monitor: as células que NÃO ficam por baixo da barra de tarefas (só células inteiras).
    // É aqui que um wallpaper põe o que tem de se ver no "fundo do ecrã" (a raquete, o vaso...).
    std::vector<Rect> workCells;
    int reference = 0;              // índice (em monitorCells) do monitor principal

    int cellCount() const { return cols * rows; }
};

// Um canvas que cobre vários monitores. A célula de referência é a do monitor `reference`
// (o principal); o canvas fica alinhado com a grelha dele, por isso nesse monitor (e nos
// que têm o mesmo DPI) cada célula do ecrã corresponde a exatamente uma célula do canvas.
CanvasLayout computeSpanCanvas(std::span<const CanvasMonitor> monitors, size_t reference);

// Canvas só para um monitor (modo "cada monitor o seu") ou para a janela do --windowed.
CanvasLayout computeSingleCanvas(const CanvasMonitor& monitor);

// Que célula do canvas mostra a célula (col,row) de um monitor? Igual ao que o grid.hlsl
// faz na GPU: usa o centro da célula do monitor, por isso funciona com DPIs diferentes.
Point canvasCellFor(const CanvasLayout& canvas, const CanvasMonitor& monitor, int col, int row);

} // namespace ap
