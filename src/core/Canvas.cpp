#include "core/Canvas.h"

#include <algorithm>
#include <cmath>

namespace ap {

namespace {

// Divisões que arredondam para baixo/cima também com números negativos.
int floorDiv(int a, int b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }
int ceilDiv(int a, int b) { return -floorDiv(-a, b); }

Rect gridAreaPx(const CanvasMonitor& m) {
    const int x0 = m.rectPx.left + m.grid.originX;
    const int y0 = m.rectPx.top + m.grid.originY;
    return {x0, y0, x0 + m.grid.cols * m.grid.cellW, y0 + m.grid.rows * m.grid.cellH};
}

// As células do canvas que ficam INTEIRAS dentro da área de trabalho do monitor (uma célula
// meio tapada pela barra de tarefas não conta), limitadas à zona que o monitor mostra.
Rect workCellsOf(const CanvasLayout& c, const CanvasMonitor& m, const Rect& monitorCells) {
    const Rect work = m.workPx.empty() ? m.rectPx : intersect(m.workPx, m.rectPx);
    if (work.empty()) return monitorCells;
    // Só os lados onde há mesmo uma barra encolhem; nos outros fica a zona do monitor tal como
    // está (que inclui as células só meio dentro dele, num monitor desalinhado da grelha).
    Rect cells = monitorCells;
    if (work.left > m.rectPx.left) cells.left = ceilDiv(work.left - c.originPx.x, c.cellW);
    if (work.top > m.rectPx.top) cells.top = ceilDiv(work.top - c.originPx.y, c.cellH);
    if (work.right < m.rectPx.right) cells.right = floorDiv(work.right - c.originPx.x, c.cellW);
    if (work.bottom < m.rectPx.bottom) cells.bottom = floorDiv(work.bottom - c.originPx.y, c.cellH);
    cells = intersect(cells, monitorCells);
    return cells.empty() ? monitorCells : cells;
}

} // namespace

CanvasLayout computeSpanCanvas(std::span<const CanvasMonitor> monitors, size_t reference) {
    CanvasLayout c;
    if (monitors.empty()) return c;
    reference = std::min(reference, monitors.size() - 1);
    c.reference = static_cast<int>(reference);
    const CanvasMonitor& ref = monitors[reference];
    c.cellW = std::max(1, ref.grid.cellW);
    c.cellH = std::max(1, ref.grid.cellH);

    std::vector<Rect> rects;
    for (const auto& m : monitors) rects.push_back(m.rectPx);
    const Rect bounds = boundingBox(rects);

    // Alinhar o canvas com a grelha do monitor de referência e recuar até cobrir tudo.
    const Rect refGrid = gridAreaPx(ref);
    c.originPx.x = refGrid.left - ceilDiv(refGrid.left - bounds.left, c.cellW) * c.cellW;
    c.originPx.y = refGrid.top - ceilDiv(refGrid.top - bounds.top, c.cellH) * c.cellH;
    c.cols = std::max(1, ceilDiv(bounds.right - c.originPx.x, c.cellW));
    c.rows = std::max(1, ceilDiv(bounds.bottom - c.originPx.y, c.cellH));

    for (const auto& m : monitors) {
        const Rect g = gridAreaPx(m);
        Rect cells{floorDiv(g.left - c.originPx.x, c.cellW), floorDiv(g.top - c.originPx.y, c.cellH),
                   ceilDiv(g.right - c.originPx.x, c.cellW), ceilDiv(g.bottom - c.originPx.y, c.cellH)};
        cells = intersect(cells, Rect{0, 0, c.cols, c.rows});
        c.monitorCells.push_back(cells);
        c.workCells.push_back(workCellsOf(c, m, cells));
    }
    return c;
}

CanvasLayout computeSingleCanvas(const CanvasMonitor& m) {
    CanvasLayout c;
    c.cellW = std::max(1, m.grid.cellW);
    c.cellH = std::max(1, m.grid.cellH);
    c.originPx = {m.rectPx.left + m.grid.originX, m.rectPx.top + m.grid.originY};
    c.cols = std::max(1, m.grid.cols);
    c.rows = std::max(1, m.grid.rows);
    c.monitorCells.push_back({0, 0, c.cols, c.rows});
    c.workCells.push_back(workCellsOf(c, m, c.monitorCells.back()));
    return c;
}

Point canvasCellFor(const CanvasLayout& c, const CanvasMonitor& m, int col, int row) {
    const double cx = m.rectPx.left + m.grid.originX + (col + 0.5) * m.grid.cellW - c.originPx.x;
    const double cy = m.rectPx.top + m.grid.originY + (row + 0.5) * m.grid.cellH - c.originPx.y;
    return {static_cast<int>(std::floor(cx / c.cellW)), static_cast<int>(std::floor(cy / c.cellH))};
}

} // namespace ap
