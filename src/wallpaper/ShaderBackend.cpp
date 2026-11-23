#include "wallpaper/ShaderBackend.h"

#include "core/ShaderSource.h"
#include "resource.h"
#include "util/Log.h"
#include "util/Win32.h"
#include "version.h"

#include <d3dcompiler.h>
#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>

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

    // Cache no disco: compilar pode demorar segundos (simulações grandes); o mesmo código
    // compilado antes carrega em milissegundos. A chave é o texto completo (prelude + código +
    // epílogo) e a versão da app, por isso qualquer mudança gera uma entrada nova.
    const std::string key = std::string(ASCIIPAPER_VERSION_STR) + "|cs_5_0|" + source;
    uint64_t h = 1469598103934665603ull; // FNV-1a 64
    for (unsigned char ch : key) h = (h ^ ch) * 1099511628211ull;
    std::filesystem::path cacheFile;
    if (PWSTR raw = nullptr; SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw))) {
        cacheFile = std::filesystem::path(raw) / L"AsciiPaper" / L"shadercache" / std::format(L"{:016x}.cso", h);
        CoTaskMemFree(raw);
    }
    std::error_code ec;
    if (!cacheFile.empty() && std::filesystem::exists(cacheFile, ec)) {
        std::string bytes;
        {
            std::ifstream in(cacheFile, std::ios::binary);
            bytes.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }
        // Um DXBC começa por "DXBC" e guarda o tamanho total no byte 24: um ficheiro cortado
        // (app morta a meio da escrita) é apagado e recompilado em vez de falhar para sempre.
        uint32_t declared = 0;
        if (bytes.size() >= 32) memcpy(&declared, bytes.data() + 24, sizeof(declared));
        const bool valid = bytes.size() >= 32 && bytes.compare(0, 4, "DXBC") == 0 && declared == bytes.size();
        render::CompiledShader cached;
        if (valid && SUCCEEDED(D3DCreateBlob(bytes.size(), &cached.blob))) {
            memcpy(cached.blob->GetBufferPointer(), bytes.data(), bytes.size());
            cached.cacheFile = cacheFile.wstring();
            return cached;
        }
        log::warn(L"Cache de shaders: {} estava inválido; a recompilar", cacheFile.filename().wstring());
        std::filesystem::remove(cacheFile, ec);
    }
    render::CompiledShader out2 = render::compileShader(source, entryName.c_str(), "CSMain", "cs_5_0");
    if (out2.ok() && !cacheFile.empty()) {
        std::filesystem::create_directories(cacheFile.parent_path(), ec);
        // Limite: 200 entradas; as mais antigas saem (cada edição de um wallpaper gera uma nova).
        std::vector<std::filesystem::directory_entry> entries;
        for (const auto& e : std::filesystem::directory_iterator(cacheFile.parent_path(), ec)) entries.push_back(e);
        if (entries.size() >= 200) {
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                std::error_code e1, e2;
                return a.last_write_time(e1) < b.last_write_time(e2);
            });
            for (size_t k = 0; k + 150 < entries.size(); ++k) std::filesystem::remove(entries[k].path(), ec);
        }
        // Escrever num temporário e só depois trocar: quem ler (outra instância, ou nós no
        // próximo arranque) vê o ficheiro inteiro ou nenhum, nunca meio ficheiro.
        std::filesystem::path tmp = cacheFile;
        tmp += std::format(L".{}.tmp", GetCurrentProcessId());
        bool written = false;
        {
            std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
            o.write(static_cast<const char*>(out2.blob->GetBufferPointer()),
                    static_cast<std::streamsize>(out2.blob->GetBufferSize()));
            written = o.good();
        }
        if (!written || !MoveFileExW(tmp.c_str(), cacheFile.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            std::filesystem::remove(tmp, ec);
        }
    }
    return out2;
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

void ShaderProgram::dispatch(ID3D11DeviceContext* ctx, render::CellCanvas& grid, FrameConstants frame,
                             const ShaderInputs& in) {
    if (!cs_ || !grid.valid() || grid.layout().cellCount() == 0) return;
    const UINT passes = std::max(1u, frame.passCount);
    const auto& g = grid.layout();
    ctx->CSSetShader(cs_.Get(), nullptr, 0);
    ID3D11Buffer* cbs[] = {constants_.Get()};
    ctx->CSSetConstantBuffers(0, 1, cbs);
    ID3D11SamplerState* samplers[] = {in.sampler};
    ctx->CSSetSamplers(0, 1, samplers);

    for (UINT pass = 0; pass < passes; ++pass) {
        frame.passIndex = pass;
        frame.passCount = passes;
        if (pass > 0) frame.clicked = 0; // o clique conta uma vez, não uma vez por passo
        ctx->UpdateSubresource(constants_.Get(), 0, nullptr, &frame, 0, 0);
        ID3D11ShaderResourceView* srvs[] = {grid.statePrevSrv(), in.obstacles, in.text, in.source};
        ID3D11UnorderedAccessView* uavs[] = {grid.cellsUav(), grid.stateNextUav()};
        ctx->CSSetShaderResources(0, 4, srvs);
        ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
        ctx->Dispatch(static_cast<UINT>((g.cols + 7) / 8), static_cast<UINT>((g.rows + 7) / 8), 1);
        // Desligar: o estado troca de papel no passo seguinte e o pixel shader lê as células.
        ID3D11ShaderResourceView* noSrv[4] = {};
        ID3D11UnorderedAccessView* noUav[2] = {};
        ctx->CSSetShaderResources(0, 4, noSrv);
        ctx->CSSetUnorderedAccessViews(0, 2, noUav, nullptr);
        grid.swapState();
    }
}

} // namespace ap::wallpaper
