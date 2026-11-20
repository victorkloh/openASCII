#include "util/Log.h"

#include "util/Win32.h"

#include <windows.h>
#include <shlobj.h>

#include <share.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>

namespace ap::log {

namespace {

constexpr std::uintmax_t kMaxLogBytes = 1024 * 1024; // 1 MiB, depois roda para log.1.txt

struct State {
    std::mutex mutex;
    FILE* file = nullptr;
    bool verbose = false;
    HANDLE console = nullptr;
    std::wstring folder;
};

State& state() {
    static State s;
    return s;
}

std::wstring appDataFolder() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &raw))) {
        return L".";
    }
    std::wstring path = std::wstring(raw) + L"\\AsciiPaper";
    CoTaskMemFree(raw);
    return path;
}

const wchar_t* levelName(Level l) {
    switch (l) {
    case Level::Debug: return L"DEBUG";
    case Level::Info: return L"INFO ";
    case Level::Warn: return L"WARN ";
    case Level::Error: return L"ERROR";
    }
    return L"?";
}

void writeConsole(HANDLE h, std::wstring_view text) {
    DWORD mode = 0;
    DWORD written = 0;
    if (GetConsoleMode(h, &mode)) {
        WriteConsoleW(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    } else {
        // Saída redirecionada para ficheiro/pipe: escrever UTF-8.
        const std::string utf8 = win::toUtf8(text);
        WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
}

} // namespace

void init(bool verbose, bool console) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    s.verbose = verbose;
    s.folder = appDataFolder();

    std::error_code ec;
    std::filesystem::create_directories(s.folder, ec);
    const std::filesystem::path logPath = s.folder + L"\\log.txt";
    if (std::filesystem::exists(logPath, ec) && std::filesystem::file_size(logPath, ec) > kMaxLogBytes) {
        std::filesystem::rename(logPath, s.folder + L"\\log.1.txt", ec);
    }
    // "N" = não herdável por processos-filho. Partilhado (_SH_DENYNO): podes abri-lo enquanto
    // corre, e uma segunda instância (ex.: --windowed ao lado do wallpaper) também escreve.
    s.file = _wfsopen(logPath.c_str(), L"abN", _SH_DENYNO);

    if (console && AttachConsole(ATTACH_PARENT_PROCESS)) {
        s.console = GetStdHandle(STD_ERROR_HANDLE);
        writeConsole(s.console, L"\n");
    }
}

void shutdown() {
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (s.file) {
        fclose(s.file);
        s.file = nullptr;
    }
    if (s.console) {
        FreeConsole();
        s.console = nullptr;
    }
}

void write(Level level, std::wstring_view message) {
    State& s = state();
    if (level == Level::Debug && !s.verbose) {
        return;
    }

    SYSTEMTIME t;
    GetLocalTime(&t);
    const std::wstring line =
        std::format(L"[{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}] [{}] {}\n", t.wYear, t.wMonth, t.wDay,
                    t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, levelName(level), message);

    OutputDebugStringW(line.c_str());

    std::lock_guard lock(s.mutex);
    if (s.file) {
        const std::string utf8 = win::toUtf8(line);
        fwrite(utf8.data(), 1, utf8.size(), s.file);
        fflush(s.file); // se crasharmos, a última linha já está no disco
    }
    if (s.console) {
        writeConsole(s.console, line);
    }
}

std::wstring folder() {
    State& s = state();
    std::lock_guard lock(s.mutex);
    return s.folder;
}

void printConsole(std::wstring_view text) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (s.console) {
        writeConsole(s.console, text);
    }
}

} // namespace ap::log
