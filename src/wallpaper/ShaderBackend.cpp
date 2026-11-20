#include "wallpaper/ShaderBackend.h"

#include "core/ShaderSource.h"
#include "resource.h"
#include "util/Log.h"
#include "util/Win32.h"

namespace ap::wallpaper {

namespace {
std::string g_prelude;
std::string g_epilogue;
} // namespace

void loadShaderLibrary() {
    if (g_prelude.empty()) g_prelude = render::loadTextResource(IDR_SHADER_PRELUDE);
    if (g_epilogue.empty()) g_epilogue = render::loadTextResource(IDR_SHADER_EPILOGUE);
}

render::CompiledShader compileWallpaperShader(const Manifest& manifest, const std::string& userSource,
                                              const std::string& entryName) {
    render::CompiledShader out;
    if (g_prelude.empty() || g_epilogue.empty()) {
        out.errors = "prelude.hlsl/epilogue.hlsl não estão embebidos no executável";
        return out;
    }
    const ShaderSourceResult pre = replaceParamCalls(userSource, manifest.params);
    if (!pre.error.empty()) {
        out.errors = entryName + ": " + pre.error;
        return out;
    }
    const std::string source = assembleShader(g_prelude, pre.source, g_epilogue, entryName);
    return render::compileShader(source, entryName.c_str(), "CSMain", "cs_5_0");
}

HRESULT ShaderProgram::create(ID3D11Device* device, ID3DBlob* blob) {
    ComPtr<ID3D11ComputeShader> cs;
    HRESULT hr = device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &cs);
    if (FAILED(hr)) {
        log::error(L"CreateComputeShader falhou: {}", win::hrToString(hr));
        return hr;
    }
    if (!constants_) {
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = sizeof(FrameConstants);
        cb.Usage = D3D11_USAGE_DEFAULT;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(hr = device->CreateBuffer(&cb, nullptr, &constants_))) return hr;
    }
    cs_ = cs;
    blob_ = blob;
    return S_OK;
}

void ShaderProgram::releaseDeviceObjects() {
    cs_.Reset();
    constants_.Reset();
}

void ShaderProgram::dispatch(ID3D11DeviceContext* ctx, render::CellCanvas& grid, const FrameConstants& frame) {
    if (!cs_ || !grid.valid() || grid.layout().cellCount() == 0) return;
    ctx->UpdateSubresource(constants_.Get(), 0, nullptr, &frame, 0, 0);

    ID3D11Buffer* cbs[] = {constants_.Get()};
    ID3D11ShaderResourceView* srvs[] = {grid.statePrevSrv()};
    ID3D11UnorderedAccessView* uavs[] = {grid.cellsUav(), grid.stateNextUav()};
    ctx->CSSetShader(cs_.Get(), nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, cbs);
    ctx->CSSetShaderResources(0, 1, srvs);
    ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);

    const auto& g = grid.layout();
    ctx->Dispatch(static_cast<UINT>((g.cols + 7) / 8), static_cast<UINT>((g.rows + 7) / 8), 1);

    // Desligar tudo: estes buffers vão ser lidos pelo pixel shader / trocados de papel.
    ID3D11ShaderResourceView* noSrv[1] = {};
    ID3D11UnorderedAccessView* noUav[2] = {};
    ctx->CSSetShaderResources(0, 1, noSrv);
    ctx->CSSetUnorderedAccessViews(0, 2, noUav, nullptr);
    grid.swapState();
}

} // namespace ap::wallpaper
