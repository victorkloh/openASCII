#include <doctest/doctest.h>

#include "core/Manifest.h"
#include "core/ShaderSource.h"

using namespace ap;

namespace {
constexpr const char* kFluid = R"toml(
[meta]
name = "Fluid"
author = "VictorKloh"
version = "1.0.0"
description = "Fumo ASCII que segue o rato"

[render]
mode = "shader"
entry = "main.hlsl"
fps = 30
cell = 10
font = "Cascadia Mono"
font_weight = 400
charset = " .:-=+*#%@"

[colors]
background = "#05070d"
palette = ["#05070d", "#1e3a8a", "#38bdf8", "#e0f2fe"]

[params]
speed     = { default = 1.0, min = 0.0, max = 4.0 }
viscosity = { default = 0.2, min = 0.0, max = 1.0 }
)toml";
} // namespace

TEST_CASE("manifest completo da spec") {
    const auto r = parseManifest(kFluid);
    REQUIRE(r.error.empty());
    const Manifest& m = r.manifest;
    CHECK(m.name == "Fluid");
    CHECK(m.mode == WallpaperMode::Shader);
    CHECK(m.entry == "main.hlsl");
    CHECK(m.fps == 30);
    CHECK(m.cell == 10);
    CHECK(m.charset == U" .:-=+*#%@");
    CHECK(m.background == Rgba8{5, 7, 13, 255});
    REQUIRE(m.palette.size() == 4);
    REQUIRE(m.params.size() == 2);
    CHECK(m.params[0].name == "speed"); // a ordem do ficheiro define o índice na GPU
    CHECK(m.params[0].value == doctest::Approx(1.0));
    CHECK(m.params[1].max == doctest::Approx(1.0));
}

TEST_CASE("manifest mínimo usa valores por defeito") {
    const auto r = parseManifest("[render]\nmode = \"script\"\n");
    REQUIRE(r.error.empty());
    CHECK(r.manifest.mode == WallpaperMode::Script);
    CHECK(r.manifest.entry == "main.lua");
    CHECK(r.manifest.palette.size() == 2);
}

TEST_CASE("erros do manifest") {
    CHECK(parseManifest("[meta]\nname = \"x\"\n").error.find("[render]") != std::string::npos);
    CHECK(parseManifest("[render]\nmode = \"video\"\n").error.find("shader") != std::string::npos);
    CHECK(parseManifest("[render]\nfps = 0\n").error.find("fps") != std::string::npos);
    CHECK(parseManifest("[render]\nfps = \"30\"\n").error.find("inteiro") != std::string::npos);
    CHECK(parseManifest("[render]\ncharset = \"\"\n").error.find("vazio") != std::string::npos);
    CHECK(parseManifest("[render]\nentry = \"../x.hlsl\"\n").error.find("entry") != std::string::npos);
    CHECK(parseManifest("[render]\n[colors]\nbackground = \"azul\"\n").error.find("cor") != std::string::npos);
    CHECK(parseManifest("[render]\n[params]\nx = { default = 1, min = 2, max = 0 }\n").error.find("min") !=
          std::string::npos);
    // Erro de sintaxe com número da linha
    const auto syntax = parseManifest("[render]\nfps = = 3\n");
    CHECK(syntax.error.find("linha 2") != std::string::npos);
}

TEST_CASE("limites de params") {
    const auto r = parseManifest("[render]\n[params]\nspeed = { default = 9.0, min = 0.0, max = 4.0 }\n");
    REQUIRE(r.error.empty());
    const ParamSpec& p = r.manifest.params[0];
    CHECK(p.value == doctest::Approx(4.0)); // default fora dos limites fica dentro
    CHECK(clampParam(p, -1.0f) == doctest::Approx(0.0));
    CHECK(clampParam(p, 2.5f) == doctest::Approx(2.5));
}

TEST_CASE("temas herdam e só alteram valores") {
    auto base = parseManifest(kFluid);
    REQUIRE(base.error.empty());
    const auto t = parseTheme(R"toml(
base = "fluid"
font = "JetBrains Mono"
cell = 7
charset = " ·•●"
palette = ["#0d0d0d", "#ff2e63", "#fafafa"]
[params]
speed = 2.0
inexistente = 1.0
viscosity = 5.0
)toml");
    REQUIRE(t.error.empty());
    CHECK(t.theme.base == "fluid");
    CHECK(looksLikeTheme("base = \"fluid\"\ncell = 7\n"));
    CHECK_FALSE(looksLikeTheme(kFluid));

    Manifest m = base.manifest;
    const auto warnings = applyTheme(m, t.theme);
    CHECK(m.font == "JetBrains Mono");
    CHECK(m.cell == 7);
    CHECK(m.charset == U" ·•●");
    CHECK(m.palette.size() == 3);
    CHECK(m.background == base.manifest.background); // não alterado pelo tema
    CHECK(m.findParam("speed")->value == doctest::Approx(2.0));
    CHECK(m.findParam("viscosity")->value == doctest::Approx(1.0)); // limitado ao max
    CHECK(warnings.size() == 2); // parâmetro inexistente + valor fora dos limites
}

TEST_CASE("erros dos temas") {
    CHECK_FALSE(parseTheme("cell = 7\n").error.empty());
    CHECK_FALSE(parseTheme("base = \"x\"\ncell = 1\n").error.empty());
    CHECK_FALSE(parseTheme("base = \"x\"\n[params]\nspeed = \"rápido\"\n").error.empty());
}

TEST_CASE("param(\"nome\") vira _param(índice)") {
    std::vector<ParamSpec> params(2);
    params[0].name = "speed";
    params[1].name = "radius";
    const auto r = replaceParamCalls("float a = param(\"radius\") * param( \"speed\" );\n"
                                     "// param(\"nada\") num comentário fica igual\n"
                                     "float myparam(float x);",
                                     params);
    REQUIRE(r.error.empty());
    CHECK(r.source.find("_param(1) * _param(0)") != std::string::npos);
    CHECK(r.source.find("// param(\"nada\")") != std::string::npos);
    CHECK(r.source.find("myparam(float x)") != std::string::npos);

    const auto bad = replaceParamCalls("\n\nfloat a = param(\"vento\");", params);
    CHECK(bad.error.find("linha 3") != std::string::npos);
    CHECK(bad.error.find("vento") != std::string::npos);
}

TEST_CASE("assembleShader põe #line para os erros apontarem para main.hlsl") {
    const auto s = assembleShader("P", "U", "E", "main.hlsl");
    CHECK(s.find("#line 1 \"main.hlsl\"\nU") != std::string::npos);
    CHECK(s.find("#line 1 \"epilogue.hlsl\"\nE") != std::string::npos);
}
