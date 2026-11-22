#pragma once
// Parsing dos argumentos da linha de comandos (sem Win32, testável).

#include <span>
#include <string>
#include <string_view>

namespace ap {

struct Options {
    bool windowed = false;  // --windowed: janela normal em vez do desktop
    bool verbose = false;   // --verbose: logs de debug + consola
    bool showHelp = false;  // --help / -h
    std::wstring wallpaper; // --wallpaper <pasta>   (Fase 3)
    int benchSeconds = 0;   // --bench <segundos>    (Fase 2)
    std::wstring screenshot; // --screenshot <ficheiro.png>: renderiza fora do ecrã, grava e sai
    double screenshotDelay = 3.0; // --screenshot-delay <s>: quanto tempo a animação corre antes
    int uiHeight = 720;           // --ui-height <dip>: altura da captura do launcher (ver a página inteira)
    bool openLauncher = false;    // --launcher: abre o launcher ao arrancar
    std::wstring uiScreenshot;    // --ui-screenshot <png> [página]: desenha o launcher num PNG e sai
    int uiPage = 0;               //   0 = wallpaper, 1 = novo, 2 = definições
    std::wstring flyoutScreenshot; // --flyout-screenshot <png>: desenha o menu da bandeja num PNG e sai
};

struct ParseResult {
    Options options;
    std::wstring error; // vazio = ok
};

// `args` sem o nome do programa.
ParseResult parseCommandLine(std::span<const std::wstring_view> args);

const wchar_t* usageText();

} // namespace ap
