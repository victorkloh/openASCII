#pragma once
// Compilação de HLSL em runtime com D3DCompile. O d3dcompiler_47.dll já vem com o
// Windows 10/11, por isso não acrescenta nada ao que distribuímos.
// As funções aqui são thread-safe (não tocam no dispositivo D3D): podem correr num
// thread de trabalho para não bloquear o thread de UI.

#include <d3dcommon.h>
#include <wrl/client.h>

#include <string>
#include <string_view>

namespace ap::render {

struct CompiledShader {
    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    std::string errors; // mensagens do compilador (erros e avisos), já sem caminhos absolutos
    bool ok() const { return blob != nullptr; }
};

CompiledShader compileShader(std::string_view source, const char* sourceName, const char* entry,
                             const char* target);

// Lê um recurso RCDATA embebido no .exe (os shaders internos vivem lá).
std::string loadTextResource(int id);

} // namespace ap::render
