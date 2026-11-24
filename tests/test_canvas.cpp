#include <doctest/doctest.h>

#include "core/Canvas.h"

using namespace ap;

namespace {
CanvasMonitor monitor(Rect r, int cw, int ch) { return {r, computeGrid(r.width(), r.height(), cw, ch)}; }
} // namespace

TEST_CASE("canvas estendido: dois monitores lado a lado formam uma só grelha") {
    const CanvasMonitor ms[] = {monitor({0, 0, 1920, 1080}, 6, 12), monitor({1920, 0, 3840, 1080}, 6, 12)};
    const auto c = computeSpanCanvas(ms, 0);
    CHECK(c.cellW == 6);
    CHECK(c.cols == 640); // 3840 / 6
    CHECK(c.rows == 90);
    REQUIRE(c.monitorCells.size() == 2);
    CHECK(c.monitorCells[0] == Rect{0, 0, 320, 90});
    CHECK(c.monitorCells[1] == Rect{320, 0, 640, 90});
    // A última coluna do monitor 1 e a primeira do monitor 2 são vizinhas no canvas.
    CHECK(canvasCellFor(c, ms[0], 319, 10) == Point{319, 10});
    CHECK(canvasCellFor(c, ms[1], 0, 10) == Point{320, 10});
}

TEST_CASE("canvas estendido: monitor vertical à esquerda, em coordenadas negativas") {
    // O caso real desta máquina: principal 1920x1080 em (0,0), vertical 1080x1920 em (-1080,-615).
    const CanvasMonitor ms[] = {monitor({0, 0, 1920, 1080}, 6, 12), monitor({-1080, -615, 0, 1305}, 6, 12)};
    const auto c = computeSpanCanvas(ms, 0);
    CHECK(c.reference == 0);
    CHECK(computeSpanCanvas(ms, 1).reference == 1);
    CHECK(c.originPx.x <= -1080);
    CHECK(c.originPx.y <= -615);
    CHECK(c.originPx.x + c.cols * c.cellW >= 1920);
    CHECK(c.originPx.y + c.rows * c.cellH >= 1305);
    // O monitor principal fica alinhado: a célula (0,0) dele é uma célula inteira do canvas.
    const Point p0 = canvasCellFor(c, ms[0], 0, 0);
    CHECK(c.originPx.x + p0.x * c.cellW == 0);
    CHECK(c.originPx.y + p0.y * c.cellH == 0);
    // Células vizinhas de um monitor são vizinhas no canvas (mapeamento 1:1 com o mesmo DPI).
    const Point a = canvasCellFor(c, ms[1], 50, 70), b = canvasCellFor(c, ms[1], 51, 71);
    CHECK(b.x - a.x == 1);
    CHECK(b.y - a.y == 1);
    // A borda direita do vertical encosta à borda esquerda do principal.
    CHECK(c.monitorCells[1].right == c.monitorCells[0].left);
}

TEST_CASE("canvas estendido: monitor com DPI maior tem células maiores, mesma escala física") {
    // Principal a 100% (células 6x12), secundário a 150% (9x18) à direita.
    const CanvasMonitor ms[] = {monitor({0, 0, 1920, 1080}, 6, 12), monitor({1920, 0, 4800, 1620}, 9, 18)};
    const auto c = computeSpanCanvas(ms, 0);
    // No secundário, cada célula avança 1.5 células do canvas.
    const Point a = canvasCellFor(c, ms[1], 0, 0), b = canvasCellFor(c, ms[1], 2, 0);
    CHECK(b.x - a.x == 3);
    CHECK(c.monitorCells[1].left == 320);
}

TEST_CASE("área de trabalho: as células tapadas pela barra de tarefas ficam de fora") {
    // Barra de tarefas de 48 px em baixo no principal; células de 12 px: 1080/12 = 90 linhas,
    // a área de trabalho acaba em 1032 px = 86 linhas inteiras.
    CanvasMonitor primary = monitor({0, 0, 1920, 1080}, 6, 12);
    primary.workPx = {0, 0, 1920, 1032};
    CanvasMonitor vertical = monitor({-1080, -615, 0, 1305}, 6, 12); // sem barra: área = monitor
    const CanvasMonitor ms[] = {primary, vertical};
    const auto c = computeSpanCanvas(ms, 0);
    REQUIRE(c.workCells.size() == 2);
    CHECK(c.workCells[0].top == c.monitorCells[0].top);
    CHECK(c.workCells[0].bottom == c.monitorCells[0].bottom - 4); // 48 px = 4 linhas
    CHECK(c.workCells[0].left == c.monitorCells[0].left);
    CHECK(c.workCells[1] == c.monitorCells[1]);

    // Barra de 40 px: a linha meio tapada (1040..1052) também fica de fora.
    primary.workPx = {0, 0, 1920, 1040};
    const auto single = computeSingleCanvas(primary);
    CHECK(single.workCells[0] == Rect{0, 0, single.cols, 86});

    // Barra em cima.
    primary.workPx = {0, 48, 1920, 1080};
    CHECK(computeSingleCanvas(primary).workCells[0].top == 4);
}

TEST_CASE("canvas de um só monitor = a grelha dele") {
    const auto m = monitor({-1920, 0, 0, 1080}, 7, 15);
    const auto c = computeSingleCanvas(m);
    CHECK(c.cols == m.grid.cols);
    CHECK(c.rows == m.grid.rows);
    CHECK(canvasCellFor(c, m, 10, 5) == Point{10, 5});
    CHECK(c.monitorCells[0] == Rect{0, 0, c.cols, c.rows});
}
