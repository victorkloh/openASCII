cmake_minimum_required(VERSION 3.25)

project(AsciiPaper
    VERSION 0.1.0
    DESCRIPTION "Motor de wallpapers ASCII reativos para Windows"
    LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "AsciiPaper só compila para Windows.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CRT estático (/MT, /MTd): o .exe final não depende de vcruntime*.dll.
# Tem de bater com o triplet do vcpkg (x64-windows-static).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(ASCIIPAPER_BUILD_TESTS "Compilar os testes unitários" ON)

find_package(wil CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Flags comuns a todos os nossos alvos (não às dependências).
# Os headers de terceiros vêm do vcpkg como SYSTEM, e o CMake passa-os com
# /external:I + /external:W0, por isso os warnings deles não nos afetam.
# ---------------------------------------------------------------------------
add_library(asciipaper_options INTERFACE)
target_compile_options(asciipaper_options INTERFACE
    /W4 /WX /permissive- /utf-8 /EHsc /Zc:__cplusplus
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>   # clang-cl já é conforme
    $<$<CONFIG:Release>:/O2 /Gy /Gw>)
target_compile_definitions(asciipaper_options INTERFACE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A00000B)
target_link_options(asciipaper_options INTERFACE
    $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>)

configure_file(src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)

# ---------------------------------------------------------------------------
# asciipaper_core: lógica pura, sem Win32. É o que os testes exercitam.
# ---------------------------------------------------------------------------
add_library(asciipaper_core STATIC
    src/core/CommandLine.cpp
    src/core/Geometry.cpp
    src/core/PausePolicy.cpp)
target_include_directories(asciipaper_core PUBLIC src)
target_link_libraries(asciipaper_core PUBLIC asciipaper_options)

# ---------------------------------------------------------------------------
# asciipaper.exe
# ---------------------------------------------------------------------------
add_executable(asciipaper WIN32
    src/main.cpp
    src/app/App.cpp
    src/app/Tray.cpp
    src/desktop/MonitorManager.cpp
    src/desktop/PauseManager.cpp
    src/desktop/Wallpaper.cpp
    src/desktop/WorkerW.cpp
    src/render/Device.cpp
    src/render/MonitorSurface.cpp
    src/util/Log.cpp
    src/util/Win32.cpp
    res/app.rc)
target_include_directories(asciipaper PRIVATE
    src res ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(asciipaper PRIVATE
    asciipaper_core WIL::WIL
    d3d11 dxgi dwmapi shcore shell32 user32 wtsapi32 advapi32 ole32)
# O manifesto (PerMonitorV2, UTF-8, Win10) vem do app.rc; o linker não gera outro.
target_link_options(asciipaper PRIVATE /MANIFEST:NO)
set_target_properties(asciipaper PROPERTIES
    VS_DEBUGGER_COMMAND_ARGUMENTS "--windowed --verbose")

# ---------------------------------------------------------------------------
# Testes
# ---------------------------------------------------------------------------
if(ASCIIPAPER_BUILD_TESTS)
    enable_testing()
    find_package(doctest CONFIG REQUIRED)
    add_executable(asciipaper_tests
        tests/main.cpp
        tests/test_command_line.cpp
        tests/test_geometry.cpp
        tests/test_pause_policy.cpp)
    target_link_libraries(asciipaper_tests PRIVATE asciipaper_core doctest::doctest)
    add_test(NAME unit COMMAND asciipaper_tests)
endif()
