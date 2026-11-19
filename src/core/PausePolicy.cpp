#include "core/PausePolicy.h"

namespace ap {

PauseReason evaluatePause(const GlobalPauseState& global, const MonitorCoverage& coverage,
                          const PauseRules& rules) {
    PauseReason r = PauseReason::None;
    if (global.user) r |= PauseReason::User;
    if (global.sessionLocked) r |= PauseReason::SessionLocked;
    if (global.suspended) r |= PauseReason::Suspended;
    if (global.batterySaver && rules.pauseOnBatterySaver) r |= PauseReason::BatterySaver;
    if (coverage.fullscreen) r |= PauseReason::Fullscreen;
    if (coverage.maximized && rules.pauseOnMaximized) r |= PauseReason::Maximized;
    return r;
}

std::wstring describe(PauseReason r) {
    if (!any(r)) {
        return L"running";
    }
    struct Name {
        PauseReason bit;
        const wchar_t* text;
    };
    static constexpr Name names[] = {
        {PauseReason::User, L"user"},
        {PauseReason::SessionLocked, L"locked"},
        {PauseReason::Suspended, L"suspended"},
        {PauseReason::BatterySaver, L"battery-saver"},
        {PauseReason::Fullscreen, L"fullscreen"},
        {PauseReason::Maximized, L"maximized"},
    };
    std::wstring out;
    for (const Name& n : names) {
        if (any(r & n.bit)) {
            if (!out.empty()) out += L'+';
            out += n.text;
        }
    }
    return out;
}

} // namespace ap
