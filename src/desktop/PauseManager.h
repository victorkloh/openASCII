#pragma once
// Recolhe do sistema os factos que decidem as pausas: que monitores estão tapados por
// uma janela em ecrã inteiro ou maximizada. O estado global (bloqueio, suspensão,
// bateria) é atualizado pela App a partir das mensagens do Windows.

#include <vector>

#include "core/PausePolicy.h"
#include "desktop/MonitorManager.h"

namespace ap::desktop {

class PauseManager {
public:
    GlobalPauseState global;
    PauseRules rules;

    // Uma entrada por monitor, pela mesma ordem de `monitors`.
    std::vector<MonitorCoverage> scan(const std::vector<MonitorInfo>& monitors) const;
};

} // namespace ap::desktop
