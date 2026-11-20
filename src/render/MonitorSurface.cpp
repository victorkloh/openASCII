#include "render/MonitorSurface.h"

#include "util/Log.h"
#include "util/Win32.h"

#include <cmath>

namespace ap::render {

namespace {
constexpr wchar_t kClassName[] = L"AsciiPaperSurface";
} // namespace

bool MonitorSurface::registerClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    wc.hbrBackground = nullptr; // nada de GDI a pintar o fundo (evita flicker)
    wc.lpszClassName = kClassName;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool MonitorSurface::createWindow(HINSTANCE instance, bool windowed, HWND notify) {
    windowed_ = windowed;
    notify_ = notify;
    const DWORD style = windowed ? WS_OVERLAPPEDWINDOW : WS_POPUP;
    // NOACTIVATE + TOOLWINDOW: nunca rouba o foco nem aparece na barra de tarefas.
    const DWORD exStyle = windowed ? 0 : (WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
    hwnd_ = CreateWindowExW(exStyle, kClassName, L"AsciiPaper", style, CW_USEDEFAULT, CW_USEDEFAULT,
                            windowed ? 1280 : 0, windowed ? 720 : 0, nullptr, nullptr, instance, this);
    if (!hwnd_) {
        log::error(L"CreateWindowEx falhou (erro {})", GetLastError());
        return false;
    }
    return true;
}

void MonitorSurface::show() {
    if (hwnd_) {
        ShowWindow(hwnd_, windowed_ ? SW_SHOWNORMAL : SW_SHOWNA);
    }
}

void MonitorSurface::destroy() {
    releaseSwapChain();
    if (hwnd_) {
        destroying_ = true;
        if (IsWindow(hwnd_)) {
            DestroyWindow(hwnd_);
        }
        hwnd_ = nullptr;
    }
}

HRESULT MonitorSurface::createSwapChain(Device& device, int width, int height) {
    releaseSwapChain();
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;

    // Flip model (FLIP_DISCARD): o DWM usa diretamente os nossos buffers em vez de os
    // copiar ("blt"). Menos cópias = menos GPU e latência. SCALING_NONE: se o tamanho
    // da janela e dos buffers divergirem por um instante, o frame NÃO é esticado.
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(width_);
    desc.Height = static_cast<UINT>(height_);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    HRESULT hr = device.factory()->CreateSwapChainForHwnd(device.device(), hwnd_, &desc, nullptr, nullptr, &swap_);
    flip_ = SUCCEEDED(hr);
    if (FAILED(hr)) {
        // Fallback previsto na spec: modelo blt clássico.
        log::warn(L"Swapchain flip falhou ({}); a usar o modelo blt", win::hrToString(hr));
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        desc.BufferCount = 1;
        desc.Scaling = DXGI_SCALING_STRETCH;
        hr = device.factory()->CreateSwapChainForHwnd(device.device(), hwnd_, &desc, nullptr, nullptr, &swap_);
    }
    if (FAILED(hr)) {
        log::error(L"CreateSwapChainForHwnd falhou: {}", win::hrToString(hr));
        return hr;
    }
    // Não queremos que o DXGI mude a janela para ecrã inteiro com Alt+Enter.
    device.factory()->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    return createTarget(device);
}

void MonitorSurface::releaseSwapChain() {
    grid.reset();
    atlas.reset();
    rtv_.Reset();
    swap_.Reset();
}

HRESULT MonitorSurface::createTarget(Device& device) {
    ComPtr<ID3D11Texture2D> back;
    HRESULT hr = swap_->GetBuffer(0, IID_PPV_ARGS(&back));
    if (SUCCEEDED(hr)) {
        hr = device.device()->CreateRenderTargetView(back.Get(), nullptr, &rtv_);
    }
    if (FAILED(hr)) {
        log::error(L"Render target falhou: {}", win::hrToString(hr));
    }
    return hr;
}

HRESULT MonitorSurface::resize(Device& device, int width, int height) {
    if (!swap_ || width <= 0 || height <= 0) {
        return S_OK;
    }
    if (width == width_ && height == height_) {
        return S_OK;
    }
    // Todas as referências aos buffers têm de ser largadas antes de ResizeBuffers.
    rtv_.Reset();
    device.context()->ClearState();
    HRESULT hr = swap_->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height),
                                      DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        log::error(L"ResizeBuffers {}x{} falhou: {}", width, height, win::hrToString(hr));
        return hr;
    }
    width_ = width;
    height_ = height;
    return createTarget(device);
}

HRESULT MonitorSurface::present() {
    if (!swap_) {
        return S_OK;
    }
    // SyncInterval 0: o ritmo vem do nosso timer. Com várias swapchains, Present(1)
    // bloquearia à espera do vsync uma vez por monitor.
    HRESULT hr = swap_->Present(0, 0);
    if (hr == DXGI_STATUS_OCCLUDED) {
        hr = S_OK;
    }
    return hr;
}

LRESULT CALLBACK MonitorSurface::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<MonitorSurface*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        break;
    }
    case WM_ERASEBKGND:
        return 1; // "já apagámos": o GDI não pinta nada por cima da swapchain
    case WM_PAINT:
        ValidateRect(hwnd, nullptr); // o conteúdo vem do D3D, não do GDI
        return 0;
    case WM_NCHITTEST:
        if (self && !self->windowed_) return HTTRANSPARENT; // o rato é para os ícones
        break;
    case WM_MOUSEACTIVATE:
        if (self && !self->windowed_) return MA_NOACTIVATE;
        break;
    case WM_SIZE:
        if (self && self->windowed_ && self->notify_) {
            SendMessageW(self->notify_, WM_APP_SURFACE_RESIZE, 0, reinterpret_cast<LPARAM>(hwnd));
        }
        return 0;
    case WM_CLOSE:
        if (self && self->windowed_ && self->notify_) {
            PostMessageW(self->notify_, WM_APP_SURFACE_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_DESTROY:
        if (self) {
            if (!self->destroying_ && self->notify_) {
                // Não fomos nós: o Explorer morreu e levou o WorkerW (e as filhas) com ele.
                PostMessageW(self->notify_, WM_APP_SURFACE_LOST, 0, 0);
            }
            self->hwnd_ = nullptr;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace ap::render
