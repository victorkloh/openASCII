#pragma once
// Uma janela + swapchain por monitor. Em modo wallpaper a janela é filha do WorkerW;
// em modo --windowed é uma janela normal de topo.

#include <windows.h>

#include "core/PausePolicy.h"
#include "desktop/MonitorManager.h"
#include "render/Device.h"

namespace ap::render {

// Mensagens que as superfícies enviam à janela controladora da App.
inline constexpr UINT WM_APP_SURFACE_LOST = WM_APP + 1;   // janela destruída por alguém (Explorer)
inline constexpr UINT WM_APP_SURFACE_RESIZE = WM_APP + 2; // --windowed: o utilizador redimensionou
inline constexpr UINT WM_APP_SURFACE_CLOSE = WM_APP + 3;  // --windowed: fechou a janela

class MonitorSurface {
public:
    MonitorSurface() = default;
    ~MonitorSurface() { destroy(); }
    MonitorSurface(const MonitorSurface&) = delete;
    MonitorSurface& operator=(const MonitorSurface&) = delete;

    static bool registerClass(HINSTANCE instance);

    // Cria a janela ESCONDIDA. Só a mostramos depois do primeiro frame estar pronto
    // (sem flash preto). Em modo wallpaper é criada como WS_POPUP e depois adotada.
    bool createWindow(HINSTANCE instance, bool windowed, HWND notify);
    void show();
    void destroy();

    HRESULT createSwapChain(Device& device, int width, int height);
    void releaseSwapChain();
    HRESULT resize(Device& device, int width, int height);
    // Desenha um frame e apresenta-o. Devolve o HRESULT do Present (para detetar device lost).
    HRESULT render(Device& device, double timeSeconds);

    HWND hwnd() const { return hwnd_; }
    bool hasSwapChain() const { return swap_ != nullptr; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool usesFlipModel() const { return flip_; }

    desktop::MonitorInfo monitor;      // monitor que esta superfície cobre
    unsigned index = 0;                // só para variar a cor entre monitores
    PauseReason pause = PauseReason::None;

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    HRESULT createTarget(Device& device);

    HWND hwnd_ = nullptr;
    HWND notify_ = nullptr;
    bool windowed_ = false;
    bool destroying_ = false;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    int width_ = 0;
    int height_ = 0;
    bool flip_ = false;
};

} // namespace ap::render
