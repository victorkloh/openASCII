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
        p = std::filesystem::path(raw) / L"OpenASCII" / L"config.toml";
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
        c.fps = std::clamp(static_cast<int>(t["fps"].value_or(int64_t{c.fps})), 0, 240);
        // Configs antigas (sem config_version) gravavam fps = 0 por defeito: passam aos 60 da v0.5.
        if (!t.contains("config_version") && c.fps == 0) c.fps = 60;
        c.otherAppsOnStart = t["other_apps_on_start"].value_or(c.otherAppsOnStart);
        c.otherAppsOnExit = t["other_apps_on_exit"].value_or(c.otherAppsOnExit);
        c.weatherCity = t["weather_city"].value_or(c.weatherCity);
        c.webcamAllowed = t["webcam_allowed"].value_or(c.webcamAllowed);
        c.playlistMinutes = std::clamp(static_cast<int>(t["playlist_minutes"].value_or(int64_t{0})), 0, 24 * 60);
        c.playlistShuffle = t["playlist_shuffle"].value_or(c.playlistShuffle);
        c.firstRunDone = t["first_run_done"].value_or(c.firstRunDone);
        if (const toml::array* list = t["playlist"].as_array()) {
            for (const auto& item : *list)
                if (auto s = item.value<std::string>(); s && !s->empty()) c.playlist.push_back(*s);
        }
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
        {"config_version", 2},
        {"wallpaper", wallpaper},
        {"interaction", interaction},
        {"max_fps", maxFps},
        {"pause_on_maximized", pauseOnMaximized},
        {"pause_on_battery_saver", pauseOnBatterySaver},
        {"span_monitors", spanMonitors},
        {"fps", fps},
        {"other_apps_on_start", otherAppsOnStart},
        {"other_apps_on_exit", otherAppsOnExit},
        {"weather_city", weatherCity},
        {"webcam_allowed", webcamAllowed},
        {"playlist_minutes", playlistMinutes},
        {"playlist_shuffle", playlistShuffle},
        {"first_run_done", firstRunDone},
    };
    toml::array list;
    for (const auto& w : playlist) list.push_back(w);
    t.insert("playlist", std::move(list));
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "# Configuração do OpenASCII (reescrita pela app ao escolher um wallpaper na tray)\n"
        << "# interaction: \"desktop_only\" (só reage sobre o desktop) ou \"always\"\n"
        << "# fps: 60 por defeito; 0 = automático (cada wallpaper decide); outro valor = FPS fixo para todos\n"
        << "# other_apps_on_start: \"ask\" | \"pause\" | \"ignore\"  (Wallpaper Engine, Lively)\n"
        << "# other_apps_on_exit:  \"ask\" | \"resume\" | \"keep\"\n"
        << "# weather_city: cidade para os wallpapers com tempo real (vazio = sem internet)\n"
        << "# playlist: wallpapers a alternar; playlist_minutes: de quanto em quanto tempo (0 = desligada)\n\n"
        << t << "\n";
    return static_cast<bool>(out);
}

} // namespace ap::app
