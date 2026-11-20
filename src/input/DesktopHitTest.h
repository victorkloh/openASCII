#pragma once
// O ponto do ecrã está sobre o ambiente de trabalho (ícones ou fundo), e não sobre uma janela?

#include <windows.h>

namespace ap::input {

bool isDesktopAt(POINT screenPoint);

} // namespace ap::input
