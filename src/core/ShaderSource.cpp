#include "core/ShaderSource.h"

#include <format>

namespace ap {

namespace {

bool isIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

int lineOf(std::string_view s, size_t pos) {
    int line = 1;
    for (size_t i = 0; i < pos && i < s.size(); ++i)
        if (s[i] == '\n') ++line;
    return line;
}

} // namespace

ShaderSourceResult replaceParamCalls(std::string_view src, const std::vector<ParamSpec>& params) {
    ShaderSourceResult r;
    constexpr std::string_view kWord = "param";
    size_t i = 0;
    while (i < src.size()) {
        // Saltar comentários para não tocar em exemplos dentro deles.
        if (src.compare(i, 2, "//") == 0) {
            const size_t end = src.find('\n', i);
            const size_t stop = end == std::string_view::npos ? src.size() : end;
            r.source.append(src.substr(i, stop - i));
            i = stop;
            continue;
        }
        if (src.compare(i, 2, "/*") == 0) {
            const size_t end = src.find("*/", i + 2);
            const size_t stop = end == std::string_view::npos ? src.size() : end + 2;
            r.source.append(src.substr(i, stop - i));
            i = stop;
            continue;
        }
        const bool boundary = i == 0 || !isIdentChar(src[i - 1]);
        if (boundary && src.compare(i, kWord.size(), kWord) == 0) {
            size_t j = i + kWord.size();
            auto skipSpace = [&] {
                while (j < src.size() && (src[j] == ' ' || src[j] == '\t')) ++j;
            };
            skipSpace();
            if (j < src.size() && src[j] == '(') {
                ++j;
                skipSpace();
                if (j < src.size() && src[j] == '"') {
                    const size_t nameStart = ++j;
                    while (j < src.size() && src[j] != '"' && src[j] != '\n') ++j;
                    if (j < src.size() && src[j] == '"') {
                        const std::string_view name = src.substr(nameStart, j - nameStart);
                        ++j;
                        skipSpace();
                        if (j < src.size() && src[j] == ')') {
                            ++j;
                            int index = -1;
                            for (size_t k = 0; k < params.size(); ++k)
                                if (params[k].name == name) index = static_cast<int>(k);
                            if (index < 0) {
                                r.error = std::format("linha {}: param(\"{}\") não está declarado em [params] "
                                                      "do wallpaper.toml",
                                                      lineOf(src, i), name);
                                return r;
                            }
                            r.source += std::format("_param({})", index);
                            i = j;
                            continue;
                        }
                    }
                }
            }
        }
        r.source += src[i];
        ++i;
    }
    return r;
}

std::string assembleShader(std::string_view prelude, std::string_view userSource, std::string_view epilogue,
                           std::string_view userFileName) {
    std::string out;
    out.reserve(prelude.size() + userSource.size() + epilogue.size() + 128);
    out += "#line 1 \"prelude.hlsl\"\n";
    out += prelude;
    out += std::format("\n#line 1 \"{}\"\n", userFileName);
    out += userSource;
    out += "\n#line 1 \"epilogue.hlsl\"\n";
    out += epilogue;
    out += '\n';
    return out;
}

} // namespace ap
