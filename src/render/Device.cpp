#include "render/Device.h"

#include "util/Log.h"
#include "util/Win32.h"

#include <iterator>

namespace ap::render {

namespace {

HRESULT createWithFlags(D3D_DRIVER_TYPE type, UINT flags, ComPtr<ID3D11Device>& dev,
                        ComPtr<ID3D11DeviceContext>& ctx, D3D_FEATURE_LEVEL& level) {
    static constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                                   D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    return D3D11CreateDevice(nullptr, type, nullptr, flags, levels, static_cast<UINT>(std::size(levels)),
                             D3D11_SDK_VERSION, &dev, &level, &ctx);
}

} // namespace

HRESULT Device::create() {
    reset();

    // BGRA: o formato nativo do ecrã e o que o Direct2D/DirectWrite (Fase 2) precisam.
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL level{};
    D3D_DRIVER_TYPE type = D3D_DRIVER_TYPE_HARDWARE;
    HRESULT hr = createWithFlags(type, flags, device_, context_, level);
#ifdef _DEBUG
    if (FAILED(hr)) {
        // A camada de debug só existe com as "Graphics Tools" instaladas.
        flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = createWithFlags(type, flags, device_, context_, level);
    }
#endif
    if (FAILED(hr)) {
        log::warn(L"D3D11: sem GPU utilizável ({}), a tentar WARP", win::hrToString(hr));
        type = D3D_DRIVER_TYPE_WARP;
        hr = createWithFlags(type, flags, device_, context_, level);
    }
    if (FAILED(hr)) {
        log::error(L"D3D11CreateDevice falhou: {}", win::hrToString(hr));
        reset();
        return hr;
    }

    // A fábrica DXGI tem de ser a mesma que criou o dispositivo (mesmo adaptador).
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(hr = device_.As(&dxgiDevice)) || FAILED(hr = dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(hr = adapter->GetParent(IID_PPV_ARGS(&factory_)))) {
        log::error(L"DXGI: não consegui obter a fábrica: {}", win::hrToString(hr));
        reset();
        return hr;
    }
    context_.As(&context1_); // opcional (D3D 11.1): dá-nos ClearView

    DXGI_ADAPTER_DESC desc{};
    adapter->GetDesc(&desc);
    log::info(L"D3D11: {} ({}), feature level {:X}.{:X}, VRAM {} MB", desc.Description,
              type == D3D_DRIVER_TYPE_WARP ? L"WARP" : L"hardware", (level >> 12) & 0xF, (level >> 8) & 0xF,
              desc.DedicatedVideoMemory / (1024 * 1024));
    return S_OK;
}

void Device::reset() {
    if (context_) {
        context_->ClearState();
        context_->Flush();
    }
    context1_.Reset();
    context_.Reset();
    factory_.Reset();
    device_.Reset();
}

} // namespace ap::render
