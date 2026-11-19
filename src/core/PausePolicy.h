#pragma once
// Decide se um monitor deve estar em pausa. Lógica pura (testável): quem recolhe os
// factos do sistema é o desktop/PauseManager; aqui só se combinam.

#include <string>

namespace ap {

// Bits, porque um monitor pode estar em pausa por várias razões ao mesmo tempo.
enum class PauseReason : unsigned {
    None = 0,
    User = 1u << 0,          // "Pausar" na tray
    SessionLocked = 1u << 1, // Win+L, ecrã de login, sessão remota desligada
    Suspended = 1u << 2,     // suspensão/hibernação
    BatterySaver = 1u << 3,  // poupança de bateria ligada
    Fullscreen = 1u << 4,    // jogo/vídeo em ecrã inteiro neste monitor
    Maximized = 1u << 5,     // janela maximizada a tapar este monitor
};

constexpr PauseReason operator|(PauseReason a, PauseReason b) {
    return static_cast<PauseReason>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
constexpr PauseReason operator&(PauseReason a, PauseReason b) {
    return static_cast<PauseReason>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
}
constexpr PauseReason& operator|=(PauseReason& a, PauseReason b) { return a = a | b; }
constexpr bool any(PauseReason r) { return r != PauseReason::None; }

// Estado que vale para todos os monitores.
struct GlobalPauseState {
    bool user = false;
    bool sessionLocked = false;
    bool suspended = false;
    bool batterySaver = false;
};

// O que está a tapar um monitor específico.
struct MonitorCoverage {
    bool fullscreen = false;
    bool maximized = false;
};

// Regras configuráveis (vão para o config.toml na Fase 5).
struct PauseRules {
    bool pauseOnMaximized = true;
    bool pauseOnBatterySaver = true;
};

PauseReason evaluatePause(const GlobalPauseState& global, const MonitorCoverage& coverage,
                          const PauseRules& rules);

// Pausas causadas por janelas de outras apps: estas exigem voltar a verificar de vez em
// quando, porque nem todas as mudanças de janela geram um evento que possamos ouvir.
constexpr bool isWindowPause(PauseReason r) {
    return any(r & (PauseReason::Fullscreen | PauseReason::Maximized));
}

// Texto legível para os logs, ex.: "locked+fullscreen".
std::wstring describe(PauseReason r);

} // namespace ap
