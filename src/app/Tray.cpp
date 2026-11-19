#include "app/Tray.h"

#include "util/Log.h"
#include "version.h"

#include <wil/resource.h>

namespace ap::app {

bool Tray::add(HWND owner, UINT callbackMessage, HICON icon) {
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = owner;
    nid_.uID = 1;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid_.uCallbackMessage = callbackMessage;
    nid_.hIcon = icon;
    wcscpy_s(nid_.szTip, L"AsciiPaper " ASCIIPAPER_VERSION_WSTR);
    return readd();
}

bool Tray::readd() {
    if (!nid_.hWnd) {
        return false;
    }
    Shell_NotifyIconW(NIM_DELETE, &nid_); // pode não existir; ignorar
    if (!Shell_NotifyIconW(NIM_ADD, &nid_)) {
        log::warn(L"Tray: Shell_NotifyIcon(NIM_ADD) falhou");
        added_ = false;
        return false;
    }
    // Versão 4: eventos modernos (WM_CONTEXTMENU, NIN_SELECT) e coordenadas corretas por DPI.
    nid_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);
    added_ = true;
    return true;
}

void Tray::remove() {
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        added_ = false;
    }
}

void Tray::showMenu(bool paused) {
    wil::unique_hmenu menu(CreatePopupMenu());
    if (!menu) {
        return;
    }
    AppendMenuW(menu.get(), MF_STRING, ID_TRAY_PAUSE, paused ? L"Retomar" : L"Pausar");
    AppendMenuW(menu.get(), MF_STRING, ID_TRAY_OPEN_LOG, L"Abrir pasta de logs");
    AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu.get(), MF_STRING, ID_TRAY_EXIT, L"Sair");

    POINT pt{};
    GetCursorPos(&pt);
    // Sem isto o menu não fecha ao clicar fora (comportamento documentado do Windows).
    SetForegroundWindow(nid_.hWnd);
    TrackPopupMenuEx(menu.get(), TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, nid_.hWnd, nullptr);
    PostMessageW(nid_.hWnd, WM_NULL, 0, 0);
}

} // namespace ap::app
