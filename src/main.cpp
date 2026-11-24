// Ponto de entrada: argumentos, instância única, handler de crash e App::run().

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <filesystem>

#include <string_view>
#include <vector>

#include <wil/resource.h>
#include <wil/result.h>

#include "app/App.h"
#include "core/CommandLine.h"
#include "desktop/Wallpaper.h"
#include "util/Log.h"

namespace {

// Se crasharmos, as nossas janelas desaparecem mas o Explorer pode ficar a mostrar o
// último frame (ou preto). Repor o wallpaper aqui é o melhor que se pode fazer.
LONG WINAPI onCrash(EXCEPTION_POINTERS* info) {
    ap::log::error(L"CRASH: exceção 0x{:08X} em {}",
                   info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0u,
                   info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr);
    if (!ap::app::App::originalWallpaper().empty()) {
        ap::desktop::restoreWallpaper(ap::app::App::originalWallpaper());
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// O OpenASCII chamava-se AsciiPaper. Na primeira vez com o nome novo: as definições, os ajustes
// e os wallpapers do utilizador passam para a pasta nova, e o arranque com o Windows passa para
// a entrada nova (a antiga apontava para o asciipaper.exe). Os nomes antigos só existem aqui.
void migrateFromAsciiPaper() {
    namespace fs = std::filesystem;
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw))) {
        const fs::path base(raw);
        CoTaskMemFree(raw);
        const fs::path oldDir = base / L"AsciiPaper", newDir = base / L"OpenASCII";
        std::error_code ec;
        if (fs::exists(oldDir, ec) && !fs::exists(newDir, ec) && !MoveFileExW(oldDir.c_str(), newDir.c_str(), 0)) {
            // Mover falha se a versão antiga ainda estiver aberta (log.txt em uso): copiar.
            fs::copy(oldDir, newDir, fs::copy_options::recursive, ec);
        }
    }
    constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (RegQueryValueExW(key, L"AsciiPaper", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        RegDeleteValueW(key, L"AsciiPaper");
        if (RegQueryValueExW(key, L"OpenASCII", nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            wchar_t exe[MAX_PATH * 2] = {};
            GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
            const std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
            RegSetValueExW(key, L"OpenASCII", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                           static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
        }
    }
    RegCloseKey(key);
}

} // namespace

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    int argc = 0;
    wil::unique_hlocal_ptr<PWSTR> argv(CommandLineToArgvW(GetCommandLineW(), &argc));
    std::vector<std::wstring_view> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv.get()[i]);
    }
    const ap::ParseResult parsed = ap::parseCommandLine(args);
    const ap::Options& opts = parsed.options;

    const bool wantConsole = opts.verbose || opts.showHelp || opts.benchSeconds > 0 || !parsed.error.empty();
    migrateFromAsciiPaper(); // antes do log: o log.txt vive na pasta que pode ser movida
    ap::log::init(opts.verbose, wantConsole);
    auto logGuard = wil::scope_exit([] { ap::log::shutdown(); });

    if (!parsed.error.empty()) {
        ap::log::error(L"{}", parsed.error);
        ap::log::printConsole(ap::usageText());
        return 2;
    }
    if (opts.showHelp) {
        ap::log::printConsole(ap::usageText());
        return 0;
    }

    // Instância única só no modo wallpaper: o --windowed pode correr ao lado, para testar.
    wil::unique_handle mutex;
    if (!opts.windowed) {
        mutex.reset(CreateMutexW(nullptr, FALSE, L"Local\\OpenASCII.SingleInstance"));
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            // Abrir a app outra vez (atalho do Menu Iniciar, com ela já a correr desde o
            // arranque do Windows) mostra o launcher da instância que já existe, em vez de
            // não fazer nada visível.
            if (HWND running = FindWindowW(L"OpenASCIIController", nullptr)) {
                DWORD pid = 0;
                GetWindowThreadProcessId(running, &pid);
                AllowSetForegroundWindow(pid); // somos nós que temos o foco: passá-lo à outra
                PostMessageW(running, RegisterWindowMessageW(ap::app::kOpenLauncherMessage), 0, 0);
            }
            ap::log::info(L"Já existe uma instância a correr; a pedir-lhe que abra o launcher");
            return 0;
        }
    }

    SetUnhandledExceptionFilter(onCrash);

    // COM (STA) é preciso para o shell (ShellExecute) e para o DirectWrite (Fase 2).
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    auto coGuard = wil::scope_exit([coHr] {
        if (SUCCEEDED(coHr)) CoUninitialize();
    });

    try {
        ap::app::App app(opts);
        return app.run();
    } catch (const std::exception& e) {
        ap::log::error(L"Exceção não tratada: {}", std::wstring(e.what(), e.what() + strlen(e.what())));
        return 1;
    }
}
