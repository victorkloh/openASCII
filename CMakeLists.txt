cmake_minimum_required(VERSION 3.25)

project(AsciiPaper
    VERSION 0.5.0
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
find_package(tomlplusplus CONFIG REQUIRED)

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
    src/core/Canvas.cpp
    src/core/Color.cpp
    src/core/CommandLine.cpp
    src/core/Geometry.cpp
    src/core/Grid.cpp
    src/core/Manifest.cpp
    src/core/PausePolicy.cpp
    src/core/ProceduralGlyphs.cpp
    src/core/ShaderSource.cpp
    src/core/Utf8.cpp)
target_include_directories(asciipaper_core PUBLIC src)
target_link_libraries(asciipaper_core PUBLIC asciipaper_options PRIVATE tomlplusplus::tomlplusplus)

# ---------------------------------------------------------------------------
# asciipaper.exe
# ---------------------------------------------------------------------------
add_executable(asciipaper WIN32
    src/main.cpp
    src/app/App.cpp
    src/app/Config.cpp
    src/app/Tray.cpp
    src/desktop/MonitorManager.cpp
    src/desktop/OtherWallpaperApps.cpp
    src/desktop/PauseManager.cpp
    src/desktop/Wallpaper.cpp
    src/desktop/WorkerW.cpp
    src/input/DesktopHitTest.cpp
    src/input/MouseInput.cpp
    src/inputs/AudioCapture.cpp
    src/inputs/InputHub.cpp
    src/inputs/LiveData.cpp
    src/inputs/SourceMedia.cpp
    src/render/Device.cpp
    src/render/GlyphAtlas.cpp
    src/render/GpuTimer.cpp
    src/render/GridRenderer.cpp
    src/render/MonitorSurface.cpp
    src/render/Screenshot.cpp
    src/render/ShaderCompiler.cpp
    src/ui/ConfirmDialog.cpp
    src/ui/Gfx.cpp
    src/ui/Launcher.cpp
    src/ui/TrayFlyout.cpp
    src/ui/Ui.cpp
    src/ui/UiWindow.cpp
    src/util/Log.cpp
    src/util/Win32.cpp
    src/wallpaper/DirectoryWatcher.cpp
    src/wallpaper/ShaderBackend.cpp
    src/wallpaper/Wallpaper.cpp
    res/app.rc)
target_include_directories(asciipaper PRIVATE
    src res shaders ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(asciipaper PRIVATE
    asciipaper_core WIL::WIL tomlplusplus::tomlplusplus
    d3d11 dxgi d3dcompiler d2d1 dwrite windowscodecs dwmapi shcore shell32 user32 wtsapi32 advapi32 ole32 oleaut32
    mfplat mfreadwrite mfuuid mf winhttp propsys)
# O manifesto (PerMonitorV2, UTF-8, Win10) vem do app.rc; o linker não gera outro.
target_link_options(asciipaper PRIVATE /MANIFEST:NO)
set_target_properties(asciipaper PROPERTIES
    VS_DEBUGGER_COMMAND_ARGUMENTS "--windowed --verbose")
# Os shaders internos vão embebidos pelo .rc: recompilar o .rc quando mudam.
set_source_files_properties(res/app.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/grid.hlsl;${CMAKE_CURRENT_SOURCE_DIR}/shaders/prelude.hlsl;${CMAKE_CURRENT_SOURCE_DIR}/shaders/epilogue.hlsl;${CMAKE_CURRENT_SOURCE_DIR}/res/app.manifest")
# Os presets ficam numa pasta wallpapers/ ao lado do .exe. Alvo próprio (corre em todos
# os builds) para as alterações aos presets chegarem mesmo sem recompilar o .exe.
add_custom_target(asciipaper_wallpapers ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/wallpapers $<TARGET_FILE_DIR:asciipaper>/wallpapers
    COMMENT "A copiar os wallpapers para junto do executável")
add_dependencies(asciipaper_wallpapers asciipaper)

# ---------------------------------------------------------------------------
# Testes
# ---------------------------------------------------------------------------
if(ASCIIPAPER_BUILD_TESTS)
    enable_testing()
    find_package(doctest CONFIG REQUIRED)
    add_executable(asciipaper_tests
        tests/main.cpp
        tests/test_canvas.cpp
        tests/test_color_utf8_grid.cpp
        tests/test_command_line.cpp
        tests/test_geometry.cpp
        tests/test_manifest.cpp
        tests/test_pause_policy.cpp
        tests/test_procedural_glyphs.cpp)
    target_link_libraries(asciipaper_tests PRIVATE asciipaper_core doctest::doctest)
    add_test(NAME unit COMMAND asciipaper_tests)
endif()
