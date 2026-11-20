#pragma once
// Preparação do código HLSL de um wallpaper antes de compilar.

#include <string>
#include <string_view>
#include <vector>

#include "core/Manifest.h"

namespace ap {

struct ShaderSourceResult {
    std::string source;
    std::string error;
};

// O HLSL não tem strings, por isso `param("speed")` não compila. Substituímos cada
// chamada por `_param(<índice>)`, onde o índice é a posição do parâmetro no manifest.
// Um nome que o manifest não declara é um erro (com a linha).
ShaderSourceResult replaceParamCalls(std::string_view userSource, const std::vector<ParamSpec>& params);

// Junta prelude + código do utilizador + epílogo, com diretivas #line para que os erros
// do compilador apontem para a linha certa de main.hlsl.
std::string assembleShader(std::string_view prelude, std::string_view userSource, std::string_view epilogue,
                           std::string_view userFileName);

} // namespace ap
