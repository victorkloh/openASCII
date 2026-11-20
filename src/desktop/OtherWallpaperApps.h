#pragma once
// Outras apps de wallpaper animado (Wallpaper Engine, Lively) também desenham por trás dos
// ícones. Se correrem ao mesmo tempo que o AsciiPaper, frames delas aparecem por baixo do
// nosso. Pausamo-las com os comandos DELAS (linha de comandos oficial), por isso as
// definições e os wallpapers escolhidos nessas apps não mudam; ao sair, retomamo-las.

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace ap::desktop {

enum class OtherAppKind { WallpaperEngine, Lively };

struct OtherWallpaperApp {
    OtherAppKind kind;
    std::wstring name;         // "Wallpaper Engine", "Lively Wallpaper"
    std::filesystem::path exe; // executável a quem enviamos os comandos
    DWORD pid = 0;
};

std::vector<OtherWallpaperApp> detectOtherWallpaperApps();

// Wallpaper Engine: "-control stop" (pára e liberta memória) / "-control play".
// Lively: "app --play false" / "app --play true".
bool suspendOtherApp(const OtherWallpaperApp& app);
bool resumeOtherApp(const OtherWallpaperApp& app);

// "Wallpaper Engine" ou "Wallpaper Engine e Lively Wallpaper", para as mensagens.
std::wstring describeApps(const std::vector<OtherWallpaperApp>& apps);

} // namespace ap::desktop
