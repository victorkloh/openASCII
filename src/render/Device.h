#pragma once
// O dispositivo Direct3D 11, partilhado por todas as swapchains (uma por monitor).

#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace ap::render {

using Microsoft::WRL::ComPtr;

// DEVICE_REMOVED/RESET: o driver da GPU reiniciou (atualização, crash, TDR) ou a placa
// foi removida. Tudo o que foi criado no dispositivo fica inválido e tem de ser recriado.
inline bool isDeviceLost(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET;
}

class Device {
public:
    HRESULT create(); // GPU real; se falhar, WARP (renderizador por software)
    void reset();

    bool valid() const { return device_ != nullptr; }
    ID3D11Device* device() const { return device_.Get(); }
    ID3D11DeviceContext* context() const { return context_.Get(); }
    ID3D11DeviceContext1* context1() const { return context1_.Get(); } // pode ser nullptr
    IDXGIFactory2* factory() const { return factory_.Get(); }
    HRESULT removedReason() const { return device_ ? device_->GetDeviceRemovedReason() : S_OK; }

private:
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11DeviceContext1> context1_;
    ComPtr<IDXGIFactory2> factory_;
};

} // namespace ap::render
