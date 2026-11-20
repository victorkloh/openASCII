#pragma once
// Backend "shader": o main.hlsl do wallpaper corre como compute shader, uma thread por
// célula, e escreve diretamente no cell buffer.

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

#include "core/Manifest.h"
#include "render/GridRenderer.h"
#include "render/ShaderCompiler.h"

namespace ap::wallpaper {

using Microsoft::WRL::ComPtr;

// Espelho exato do cbuffer Frame em prelude.hlsl (regras de empacotamento de 16 bytes).
struct alignas(16) FrameConstants {
    float time, dt;
    uint32_t gridW, gridH;
    float mouseX, mouseY, mouseVX, mouseVY;
    float clickX, clickY, clickTime;
    uint32_t buttons;
    uint32_t inside, clickButton;
    float scroll;
    uint32_t charsetSize;
    float background[4];
    float palette[8][4];
    uint32_t paletteCount, frameIndex;
    float cellAspect;
    uint32_t glyphCount;
    float params[16];
    float cellPxW, cellPxH, resolutionW, resolutionH;
    float worldPerCellX, worldPerCellY;
    uint32_t monitorCount;
    uint32_t primaryIndex;  // qual das zonas é o monitor principal
    int32_t monitors[8][4]; // zonas visíveis do canvas, em células
    float localHour, localMinute, localSecond;      // hora local (segundos com fração)
    float mousePresence;                            // 0..1: o rato está (ou esteve há pouco) no fundo
    uint32_t year, month, day, weekday;              // data local (weekday 0 = domingo)
};
static_assert(sizeof(FrameConstants) == 480, "tem de bater com o cbuffer Frame do prelude.hlsl");

// Thread-safe: não toca no dispositivo. Pode correr num thread de trabalho.
render::CompiledShader compileWallpaperShader(const Manifest& manifest, const std::string& userSource,
                                              const std::string& entryName);

// Os textos do prelude/epílogo vêm dos recursos do .exe; carregá-los uma vez no thread de UI.
void loadShaderLibrary();

class ShaderProgram {
public:
    HRESULT create(ID3D11Device* device, ID3DBlob* blob);
    HRESULT recreate(ID3D11Device* device) { return blob_ ? create(device, blob_.Get()) : E_FAIL; }
    void releaseDeviceObjects(); // mantém o blob, para recriar depois de um device lost
    bool valid() const { return cs_ != nullptr; }

    void dispatch(ID3D11DeviceContext* ctx, render::CellCanvas& canvas, const FrameConstants& frame);

private:
    ComPtr<ID3DBlob> blob_;
    ComPtr<ID3D11ComputeShader> cs_;
    ComPtr<ID3D11Buffer> constants_;
};

} // namespace ap::wallpaper
