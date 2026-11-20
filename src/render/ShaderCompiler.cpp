#include "render/ShaderCompiler.h"

#include <windows.h>
#include <d3dcompiler.h>

namespace ap::render {

CompiledShader compileShader(std::string_view source, const char* sourceName, const char* entry,
                             const char* target) {
    CompiledShader out;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG;
#endif
    const HRESULT hr = D3DCompile(source.data(), source.size(), sourceName, nullptr, nullptr, entry, target, flags,
                                  0, &out.blob, &errors);
    if (errors && errors->GetBufferSize() > 0) {
        out.errors.assign(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        while (!out.errors.empty() && (out.errors.back() == '\0' || out.errors.back() == '\n')) {
            out.errors.pop_back();
        }
    }
    if (FAILED(hr)) {
        out.blob.Reset();
        if (out.errors.empty()) out.errors = "D3DCompile falhou sem mensagem";
    }
    return out;
}

std::string loadTextResource(int id) {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (!res) return {};
    HGLOBAL data = LoadResource(nullptr, res);
    const DWORD size = SizeofResource(nullptr, res);
    const void* ptr = data ? LockResource(data) : nullptr;
    return ptr ? std::string(static_cast<const char*>(ptr), size) : std::string{};
}

} // namespace ap::render
