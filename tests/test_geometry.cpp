#include <doctest/doctest.h>

#include "core/Geometry.h"

#include <vector>

using namespace ap;

TEST_CASE("toParentSpace converte para a origem do ecrã virtual") {
    // Monitor principal em (0,0) e um secundário à esquerda: o ecrã virtual começa em -1920.
    const Point virtualOrigin{-1920, 0};
    const Rect primary{0, 0, 2560, 1440};
    const Rect left{-1920, 180, 0, 1260};

    CHECK(toParentSpace(primary, virtualOrigin) == Rect{1920, 0, 4480, 1440});
    CHECK(toParentSpace(left, virtualOrigin) == Rect{0, 180, 1920, 1260});
}

TEST_CASE("toParentSpace com monitor acima (coordenadas Y negativas)") {
    const Point origin{0, -1080};
    CHECK(toParentSpace(Rect{0, -1080, 1920, 0}, origin) == Rect{0, 0, 1920, 1080});
    CHECK(toParentSpace(Rect{0, 0, 1920, 1080}, origin) == Rect{0, 1080, 1920, 2160});
}

TEST_CASE("boundingBox calcula o ecrã virtual") {
    const std::vector<Rect> monitors{{0, 0, 1920, 1080}, {-1280, -200, 0, 824}, {1920, 0, 3360, 2560}};
    CHECK(boundingBox(monitors) == Rect{-1280, -200, 3360, 2560});
    CHECK(boundingBox({}) == Rect{});
}

TEST_CASE("covers") {
    const Rect monitor{0, 0, 1920, 1080};
    CHECK(covers(monitor, monitor));
    // Janela maximizada: o Windows estende-a uns píxeis para fora do monitor.
    CHECK(covers(Rect{-8, -8, 1928, 1088}, monitor));
    CHECK_FALSE(covers(Rect{0, 0, 1920, 1040}, monitor));
}

TEST_CASE("intersect") {
    CHECK(intersect(Rect{0, 0, 10, 10}, Rect{5, 5, 20, 20}) == Rect{5, 5, 10, 10});
    CHECK(intersect(Rect{0, 0, 10, 10}, Rect{10, 0, 20, 10}).empty());
}

TEST_CASE("scaleByDpi arredonda a píxeis inteiros") {
    CHECK(scaleByDpi(10, 96) == 10);   // 100%
    CHECK(scaleByDpi(10, 120) == 13);  // 125% -> 12.5 arredonda para 13
    CHECK(scaleByDpi(6, 144) == 9);    // 150%
    CHECK(scaleByDpi(10, 168) == 18);  // 175% -> 17.5 -> 18
    CHECK(scaleByDpi(6, 192) == 12);   // 200%
    CHECK(scaleByDpi(-10, 120) == -13);
    CHECK(scaleByDpi(0, 192) == 0);
}
