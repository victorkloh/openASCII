#pragma once
// Ícone na área de notificação (tray) com o menu da app.

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace ap::app {

enum TrayCommand : UINT {
    ID_TRAY_PAUSE = 40001,
    ID_TRAY_OPEN_LOG,
    ID_TRAY_EXIT,
    ID_TRAY_RELOAD,
    ID_TRAY_OPEN_WALLPAPERS,
    ID_TRAY_WALLPAPER_FIRST = 41000, // + índice na lista de wallpapers
    ID_TRAY_WALLPAPER_LAST = 41999,
};

struct TrayMenuState {
    bool paused = false;
    std::vector<std::wstring> wallpapers; // etiquetas, pela ordem dos IDs
    int current = -1;                     // índice selecionado (visto)
};

class Tray {
public:
    ~Tray() { remove(); }

    bool add(HWND owner, UINT callbackMessage, HICON icon);
    // Depois de o Explorer reiniciar a barra de tarefas é nova e esqueceu-se do ícone.
    bool readd();
    void remove();
    void showMenu(const TrayMenuState& state);

private:
    NOTIFYICONDATAW nid_{};
    bool added_ = false;
};

} // namespace ap::app
