#include <doctest/doctest.h>

#include "core/PausePolicy.h"

using namespace ap;

TEST_CASE("sem razões = a correr") {
    CHECK(evaluatePause({}, {}, {}) == PauseReason::None);
    CHECK(describe(PauseReason::None) == L"running");
}

TEST_CASE("razões globais afetam todos os monitores") {
    GlobalPauseState g;
    g.sessionLocked = true;
    g.suspended = true;
    const auto r = evaluatePause(g, {}, {});
    CHECK(any(r & PauseReason::SessionLocked));
    CHECK(any(r & PauseReason::Suspended));
    CHECK_FALSE(isWindowPause(r));
    CHECK(describe(r) == L"locked+suspended");
}

TEST_CASE("janela maximizada respeita a regra configurável") {
    MonitorCoverage c;
    c.maximized = true;
    PauseRules rules;
    CHECK(evaluatePause({}, c, rules) == PauseReason::Maximized);
    rules.pauseOnMaximized = false;
    CHECK(evaluatePause({}, c, rules) == PauseReason::None);
}

TEST_CASE("ecrã inteiro pausa sempre e é uma pausa de janela") {
    MonitorCoverage c;
    c.fullscreen = true;
    PauseRules rules;
    rules.pauseOnMaximized = false;
    const auto r = evaluatePause({}, c, rules);
    CHECK(r == PauseReason::Fullscreen);
    CHECK(isWindowPause(r));
}

TEST_CASE("poupança de bateria respeita a regra") {
    GlobalPauseState g;
    g.batterySaver = true;
    PauseRules rules;
    CHECK(evaluatePause(g, {}, rules) == PauseReason::BatterySaver);
    rules.pauseOnBatterySaver = false;
    CHECK(evaluatePause(g, {}, rules) == PauseReason::None);
}
