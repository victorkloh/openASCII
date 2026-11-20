#include "render/Screenshot.h"

#include <wincodec.h>
#include <wrl/client.h>

namespace ap::render {

using Microsoft::WRL::ComPtr;

HRESULT saveTextureAsPng(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11Texture2D* texture,
                         const std::wstring& path) {
    // 1) Copiar para uma textura "staging", a única que a CPU pode ler.
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &staging);
    if (FAILED(hr)) return hr;
    ctx->CopyResource(staging.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(hr = ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return hr;

    // 2) Codificar em PNG com o WIC (componente do Windows, sem dependências).
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (SUCCEEDED(hr)) hr = wic->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(desc.Width, desc.Height);
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr)) {
        hr = frame->WritePixels(desc.Height, mapped.RowPitch, mapped.RowPitch * desc.Height,
                                static_cast<BYTE*>(mapped.pData));
    }
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    ctx->Unmap(staging.Get(), 0);
    return hr;
}

} // namespace ap::render
