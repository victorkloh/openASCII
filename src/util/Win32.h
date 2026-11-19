#pragma once
// Pequenos helpers de Win32 usados em vários sítios.

#include <windows.h>

#include <string>
#include <string_view>

#include "core/Geometry.h"

namespace ap::win {

std::string toUtf8(std::wstring_view text);

// "0x887A0005 (The GPU device instance has been suspended...)"
std::wstring hrToString(HRESULT hr);

// Nome da classe de uma janela, ex.: L"WorkerW". Vazio se a janela não existir.
std::wstring className(HWND hwnd);

inline Rect toRect(const RECT& r) { return {r.left, r.top, r.right, r.bottom}; }
inline RECT toRECT(const Rect& r) { return {r.left, r.top, r.right, r.bottom}; }

// Formata um HWND para os logs.
inline std::wstring hwndStr(HWND h) {
    wchar_t buf[32];
    swprintf_s(buf, L"0x%p", static_cast<void*>(h));
    return buf;
}

} // namespace ap::win
