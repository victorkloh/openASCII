#pragma once
// Ícone na área de notificação (tray) com o menu da app.

#include <windows.h>
#include <shellapi.h>

namespace ap::app {

enum TrayCommand : UINT {
    ID_TRAY_PAUSE = 40001,
    ID_TRAY_OPEN_LOG,
    ID_TRAY_EXIT,
};

class Tray {
public:
    ~Tray() { remove(); }

    bool add(HWND owner, UINT callbackMessage, HICON icon);
    // Depois de o Explorer reiniciar a barra de tarefas é nova e esqueceu-se do ícone.
    bool readd();
    void remove();
    void showMenu(bool paused);

private:
    NOTIFYICONDATAW nid_{};
    bool added_ = false;
};

} // namespace ap::app
