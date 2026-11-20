#include "wallpaper/Wallpaper.h"

#include "util/Log.h"
#include "util/Win32.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

namespace ap::wallpaper {

namespace {

bool readFile(const fs::path& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    // Tirar o BOM UTF-8 que alguns editores põem no início.
    if (out.size() >= 3 && out.compare(0, 3, "\xEF\xBB\xBF") == 0) out.erase(0, 3);
    return true;
}

fs::path exeDir() {
    wchar_t buf[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return fs::path(buf).parent_path();
}

// Para um tema em <raiz>/ripple/themes/x.toml com base = "ripple", procurar a pasta base
// subindo a partir do tema e depois nas raízes.
fs::path findBase(const fs::path& themeFile, const std::string& base) {
    const fs::path baseName = toWide(base);
    std::error_code ec;
    for (fs::path dir = themeFile.parent_path(); !dir.empty(); dir = dir.parent_path()) {
        if (dir.filename() == baseName && fs::exists(dir / L"wallpaper.toml", ec)) return dir;
        if (fs::exists(dir / baseName / L"wallpaper.toml", ec)) return dir / baseName;
        if (dir == dir.root_path()) break;
    }
    for (const fs::path& root : wallpaperRoots()) {
        if (fs::exists(root / baseName / L"wallpaper.toml", ec)) return root / baseName;
    }
    return {};
}

} // namespace

std::wstring toWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string toUtf8Path(const fs::path& p) { return win::toUtf8(p.wstring()); }

std::vector<fs::path> wallpaperRoots() {
    std::vector<fs::path> roots{exeDir() / L"wallpapers"};
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw))) {
        roots.push_back(fs::path(raw) / L"AsciiPaper" / L"wallpapers");
        CoTaskMemFree(raw);
    }
    return roots;
}

fs::path resolveWallpaper(const std::wstring& nameOrPath) {
    std::error_code ec;
    const fs::path p(nameOrPath);
    if (fs::exists(p, ec)) return fs::absolute(p, ec);
    for (const fs::path& root : wallpaperRoots()) {
        if (fs::exists(root / p / L"wallpaper.toml", ec)) return root / p;
    }
    return {};
}

LoadResult loadWallpaper(const fs::path& path) {
    LoadResult r;
    LoadedWallpaper& w = r.wallpaper;
    std::error_code ec;

    Theme theme;
    bool isTheme = false;
    if (fs::is_regular_file(path, ec)) {
        std::string text;
        if (!readFile(path, text)) {
            r.error = "não consegui ler " + toUtf8Path(path);
            return r;
        }
        ThemeResult t = parseTheme(text);
        if (!t.error.empty()) {
            r.error = toUtf8Path(path.filename()) + ": " + t.error;
            return r;
        }
        theme = std::move(t.theme);
        isTheme = true;
        w.themeFile = path;
        w.dir = findBase(path, theme.base);
        if (w.dir.empty()) {
            r.error = std::format("{}: o wallpaper base \"{}\" não foi encontrado", toUtf8Path(path.filename()),
                                  theme.base);
            return r;
        }
    } else {
        w.dir = path;
    }

    std::string manifestText;
    if (!readFile(w.dir / L"wallpaper.toml", manifestText)) {
        r.error = "falta o wallpaper.toml em " + toUtf8Path(w.dir);
        return r;
    }
    ManifestResult m = parseManifest(manifestText);
    if (!m.error.empty()) {
        r.error = "wallpaper.toml: " + m.error;
        return r;
    }
    w.manifest = std::move(m.manifest);
    w.warnings = std::move(m.warnings);
    if (w.manifest.name.empty()) w.manifest.name = toUtf8Path(w.dir.filename());
    w.label = toUtf8Path(w.dir.filename());

    if (isTheme) {
        for (auto& warning : applyTheme(w.manifest, theme)) w.warnings.push_back(std::move(warning));
        w.label += " / " + toUtf8Path(path.stem());
    }

    if (w.manifest.mode == WallpaperMode::Script) {
        r.error = "wallpapers do tipo script (Lua) chegam na Fase 4";
        return r;
    }
    if (!readFile(w.dir / toWide(w.manifest.entry), w.source)) {
        r.error = std::format("não consegui ler o ficheiro de entrada \"{}\"", w.manifest.entry);
        return r;
    }
    w.watchDir = w.dir;
    return r;
}

std::vector<CatalogEntry> listWallpapers() {
    std::vector<CatalogEntry> out;
    std::error_code ec;
    for (const fs::path& root : wallpaperRoots()) {
        if (!fs::is_directory(root, ec)) continue;
        std::vector<fs::path> dirs;
        for (const auto& e : fs::directory_iterator(root, ec)) {
            if (e.is_directory(ec) && fs::exists(e.path() / L"wallpaper.toml", ec)) dirs.push_back(e.path());
        }
        std::sort(dirs.begin(), dirs.end());
        for (const fs::path& dir : dirs) {
            const std::string name = toUtf8Path(dir.filename());
            out.push_back({name, dir});
            std::vector<fs::path> themes;
            for (const auto& t : fs::directory_iterator(dir / L"themes", ec)) {
                if (t.path().extension() == L".toml") themes.push_back(t.path());
            }
            std::sort(themes.begin(), themes.end());
            for (const fs::path& t : themes) out.push_back({name + " / " + toUtf8Path(t.stem()), t});
        }
    }
    return out;
}

} // namespace ap::wallpaper
