#include "util/Win32.h"

#include <format>

namespace ap::win {

std::string toUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int len = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                        nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), len, nullptr,
                        nullptr);
    return out;
}

std::wstring hrToString(HRESULT hr) {
    wchar_t* msg = nullptr;
    const DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        static_cast<DWORD>(hr), 0, reinterpret_cast<wchar_t*>(&msg), 0, nullptr);
    std::wstring text = n && msg ? std::wstring(msg, n) : L"";
    if (msg) {
        LocalFree(msg);
    }
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ')) {
        text.pop_back();
    }
    return std::format(L"0x{:08X}{}{}", static_cast<unsigned>(hr), text.empty() ? L"" : L" ", text);
}

std::wstring className(HWND hwnd) {
    wchar_t buf[256] = {};
    const int n = GetClassNameW(hwnd, buf, static_cast<int>(std::size(buf)));
    return std::wstring(buf, n > 0 ? static_cast<size_t>(n) : 0);
}

} // namespace ap::win
