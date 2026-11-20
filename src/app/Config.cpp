#include "app/Config.h"

#include "util/Log.h"
#include "util/Win32.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <fstream>

#include <toml++/toml.hpp>

namespace ap::app {

std::filesystem::path Config::path() {
    PWSTR raw = nullptr;
    std::filesystem::path p = L"config.toml";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw))) {
        p = std::filesystem::path(raw) / L"AsciiPaper" / L"config.toml";
        CoTaskMemFree(raw);
    }
    return p;
}

Config Config::load() {
    Config c;
    const auto file = path();
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return c;
    try {
        const toml::table t = toml::parse_file(win::toUtf8(file.wstring()));
        c.wallpaper = t["wallpaper"].value_or(c.wallpaper);
        c.interaction = t["interaction"].value_or(c.interaction);
        c.maxFps = std::clamp(static_cast<int>(t["max_fps"].value_or(int64_t{c.maxFps})), 1, 240);
        c.pauseOnMaximized = t["pause_on_maximized"].value_or(c.pauseOnMaximized);
        c.pauseOnBatterySaver = t["pause_on_battery_saver"].value_or(c.pauseOnBatterySaver);
        c.spanMonitors = t["span_monitors"].value_or(c.spanMonitors);
    } catch (const toml::parse_error& e) {
        log::warn(L"config.toml inválido (linha {}): {}; a usar valores por defeito", e.source().begin.line,
                  std::wstring(e.description().begin(), e.description().end()));
    }
    return c;
}

bool Config::save() const {
    const auto file = path();
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    toml::table t{
        {"wallpaper", wallpaper},
        {"interaction", interaction},
        {"max_fps", maxFps},
        {"pause_on_maximized", pauseOnMaximized},
        {"pause_on_battery_saver", pauseOnBatterySaver},
        {"span_monitors", spanMonitors},
    };
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "# Configuração do AsciiPaper (reescrita pela app ao escolher um wallpaper na tray)\n"
        << "# interaction: \"desktop_only\" (só reage sobre o desktop) ou \"always\"\n\n"
        << t << "\n";
    return static_cast<bool>(out);
}

} // namespace ap::app
