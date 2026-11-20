#include "core/CommandLine.h"

#include <charconv>
#include <string>

namespace ap {

namespace {

bool parsePositiveInt(std::wstring_view text, int& out) {
    // std::from_chars não aceita wchar_t; os dígitos são ASCII, por isso convertemos.
    std::string narrow;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9') return false;
        narrow += static_cast<char>(c);
    }
    if (narrow.empty()) return false;
    int value = 0;
    auto [ptr, ec] = std::from_chars(narrow.data(), narrow.data() + narrow.size(), value);
    if (ec != std::errc{} || ptr != narrow.data() + narrow.size() || value <= 0) return false;
    out = value;
    return true;
}

} // namespace

ParseResult parseCommandLine(std::span<const std::wstring_view> args) {
    ParseResult result;
    Options& o = result.options;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::wstring_view a = args[i];
        auto needValue = [&](std::wstring_view name) -> const std::wstring_view* {
            if (i + 1 >= args.size()) {
                result.error = std::wstring(name) + L" precisa de um valor";
                return nullptr;
            }
            return &args[++i];
        };

        if (a == L"--windowed") {
            o.windowed = true;
        } else if (a == L"--verbose" || a == L"-v") {
            o.verbose = true;
        } else if (a == L"--help" || a == L"-h" || a == L"/?") {
            o.showHelp = true;
        } else if (a == L"--wallpaper") {
            const auto* v = needValue(a);
            if (!v) return result;
            o.wallpaper = std::wstring(*v);
        } else if (a == L"--screenshot") {
            const auto* v = needValue(a);
            if (!v) return result;
            o.screenshot = std::wstring(*v);
            o.windowed = true; // renderiza numa janela escondida, nunca no desktop
        } else if (a == L"--bench") {
            const auto* v = needValue(a);
            if (!v) return result;
            if (!parsePositiveInt(*v, o.benchSeconds)) {
                result.error = L"--bench espera um número inteiro de segundos > 0";
                return result;
            }
        } else {
            result.error = L"argumento desconhecido: " + std::wstring(a);
            return result;
        }
    }
    return result;
}

const wchar_t* usageText() {
    return L"Uso: asciipaper [opções]\n"
           L"  --windowed            renderiza numa janela normal (desenvolvimento)\n"
           L"  --wallpaper <pasta>   carrega um wallpaper específico (Fase 3)\n"
           L"  --bench <segundos>    mede frame time, CPU e RAM\n"
           L"  --screenshot <png>    renderiza 3 s fora do ecrã (1280x720), grava um PNG e sai\n"
           L"  --verbose, -v         logs detalhados, também na consola\n"
           L"  --help, -h            mostra esta ajuda\n";
}

} // namespace ap
