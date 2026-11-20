#pragma once
// Grava uma textura da GPU (o back buffer) num PNG, via WIC. Usado por --screenshot.

#include <d3d11.h>

#include <string>

namespace ap::render {

HRESULT saveTextureAsPng(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11Texture2D* texture,
                         const std::wstring& path);

} // namespace ap::render
