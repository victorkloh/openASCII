#pragma once
// Guardar e repor o wallpaper original do Windows.

#include <string>

namespace ap::desktop {

// Caminho do wallpaper atual (vazio = cor sólida).
std::wstring currentWallpaper();

// Volta a aplicar o wallpaper, o que força o Explorer a repintar o fundo por cima de
// qualquer resto nosso. Seguro de chamar a partir do handler de crash.
void restoreWallpaper(const std::wstring& path);

} // namespace ap::desktop
