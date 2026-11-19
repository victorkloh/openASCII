#pragma once
// Log simples e thread-safe para %APPDATA%\AsciiPaper\log.txt (com rotação),
// OutputDebugString (visível no debugger) e, com --verbose, a consola de onde viemos.

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace ap::log {

enum class Level { Debug, Info, Warn, Error };

// `verbose` ativa o nível Debug. `console` liga-se à consola do processo-pai, se houver.
void init(bool verbose, bool console);
void shutdown();

void write(Level level, std::wstring_view message);
std::wstring folder(); // pasta onde o log vive

template <class... Args>
void debug(std::wformat_string<Args...> fmt, Args&&... args) {
    write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void info(std::wformat_string<Args...> fmt, Args&&... args) {
    write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void warn(std::wformat_string<Args...> fmt, Args&&... args) {
    write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void error(std::wformat_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

// Escreve diretamente na consola (para --help), sem prefixos.
void printConsole(std::wstring_view text);

} // namespace ap::log
