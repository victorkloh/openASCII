#include "desktop/OtherWallpaperApps.h"

#include "util/Log.h"

#include <tlhelp32.h>

#include <wil/resource.h>

namespace ap::desktop {

namespace {

std::filesystem::path processPath(DWORD pid) {
    wil::unique_handle h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!h) return {};
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD size = static_cast<DWORD>(std::size(buf));
    if (!QueryFullProcessImageNameW(h.get(), 0, buf, &size)) return {};
    return buf;
}

// Corre `exe args` sem janela e espera um pouco (os comandos destas apps são instantâneos).
bool runCommand(const std::filesystem::path& exe, const std::wstring& args) {
    std::wstring cmd = L"\"" + exe.wstring() + L"\" " + args;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        exe.parent_path().c_str(), &si, &pi)) {
        log::warn(L"Não consegui correr {} {} (erro {})", exe.wstring(), args, GetLastError());
        return false;
    }
    WaitForSingleObject(pi.hProcess, 3000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

} // namespace

std::vector<OtherWallpaperApp> detectOtherWallpaperApps() {
    std::vector<OtherWallpaperApp> out;
    wil::unique_handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snap || snap.get() == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{sizeof(pe)};
    bool haveWe = false, haveLively = false;
    for (BOOL ok = Process32FirstW(snap.get(), &pe); ok; ok = Process32NextW(snap.get(), &pe)) {
        const std::wstring name = pe.szExeFile;
        auto is = [&](const wchar_t* n) { return _wcsicmp(name.c_str(), n) == 0; };
        if (!haveWe && (is(L"wallpaper64.exe") || is(L"wallpaper32.exe"))) {
            if (auto path = processPath(pe.th32ProcessID); !path.empty()) {
                out.push_back({OtherAppKind::WallpaperEngine, L"Wallpaper Engine", path, pe.th32ProcessID});
                haveWe = true;
            }
        } else if (!haveLively && is(L"Lively.exe")) {
            if (auto path = processPath(pe.th32ProcessID); !path.empty()) {
                out.push_back({OtherAppKind::Lively, L"Lively Wallpaper", path, pe.th32ProcessID});
                haveLively = true;
            }
        }
    }
    return out;
}

bool suspendOtherApp(const OtherWallpaperApp& app) {
    log::info(L"A pausar {} ({})", app.name, app.exe.wstring());
    switch (app.kind) {
    case OtherAppKind::WallpaperEngine: return runCommand(app.exe, L"-control stop");
    case OtherAppKind::Lively: return runCommand(app.exe, L"app --play false");
    }
    return false;
}

bool resumeOtherApp(const OtherWallpaperApp& app) {
    log::info(L"A retomar {}", app.name);
    switch (app.kind) {
    case OtherAppKind::WallpaperEngine: return runCommand(app.exe, L"-control play");
    case OtherAppKind::Lively: return runCommand(app.exe, L"app --play true");
    }
    return false;
}

std::wstring describeApps(const std::vector<OtherWallpaperApp>& apps) {
    std::wstring s;
    for (size_t i = 0; i < apps.size(); ++i) {
        if (i > 0) s += i + 1 == apps.size() ? L" e " : L", ";
        s += apps[i].name;
    }
    return s;
}

} // namespace ap::desktop
