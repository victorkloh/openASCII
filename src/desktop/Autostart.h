#pragma once
// Arrancar com o Windows, de duas maneiras (as duas só para o utilizador atual, sem admin):
//
// - Normal: a entrada "OpenASCII" em HKCU\...\CurrentVersion\Run. É a mais simples, mas o
//   Windows atrasa estas apps uns segundos depois de iniciares sessão (para o arranque
//   parecer mais rápido), por isso o wallpaper aparece um pouco depois do ambiente de trabalho.
// - Rápido: uma tarefa agendada "ao iniciar sessão" para a tua conta, sem esse atraso, com
//   prioridade normal e sem limite de tempo (o limite por defeito das tarefas é 3 dias).
//
// Nenhuma das duas corre antes de iniciares sessão: o Windows não deixa apps com janelas no
// ecrã de início de sessão nem no ecrã de bloqueio. Com o PC bloqueado (Win+L) a app continua
// aberta, em pausa, e ao desbloquear o wallpaper já lá está.

#include <string>

namespace ap::desktop {

enum class AutostartMode { Off, Normal, Fast };

AutostartMode queryAutostart();
// Liga/desliga (e troca de modo, apagando o outro). Devolve false com `error` preenchido.
bool setAutostart(AutostartMode mode, std::wstring& error);

} // namespace ap::desktop
