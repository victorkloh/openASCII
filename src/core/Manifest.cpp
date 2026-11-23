#include "core/Manifest.h"

#include "core/Utf8.h"

#include <algorithm>
#include <format>
#include <sstream>

#include <toml++/toml.hpp>

namespace ap {

namespace {

// Erros de validação levam-nos a sair cedo com uma mensagem clara.
struct Invalid {
    std::string message;
};

std::string where(const toml::node& n) {
    const auto& src = n.source();
    return src.begin ? std::format(" (linha {})", src.begin.line) : std::string{};
}

std::optional<std::string> getString(const toml::table& t, std::string_view key) {
    const toml::node* n = t.get(key);
    if (!n) return std::nullopt;
    if (auto v = n->value<std::string>()) return *v;
    throw Invalid{std::format("'{}' tem de ser texto{}", key, where(*n))};
}

std::optional<int> getInt(const toml::table& t, std::string_view key, int lo, int hi) {
    const toml::node* n = t.get(key);
    if (!n) return std::nullopt;
    auto v = n->value<int64_t>();
    if (!v || !n->is_integer()) throw Invalid{std::format("'{}' tem de ser um número inteiro{}", key, where(*n))};
    if (*v < lo || *v > hi) throw Invalid{std::format("'{}' tem de estar entre {} e {}{}", key, lo, hi, where(*n))};
    return static_cast<int>(*v);
}

std::optional<float> getNumber(const toml::table& t, std::string_view key) {
    const toml::node* n = t.get(key);
    if (!n) return std::nullopt;
    if (!n->is_number()) throw Invalid{std::format("'{}' tem de ser um número{}", key, where(*n))};
    return static_cast<float>(n->value<double>().value_or(0.0));
}

Rgba8 toColor(const toml::node& n, std::string_view what) {
    auto s = n.value<std::string>();
    if (!s) throw Invalid{std::format("'{}' tem de ser uma cor em texto, ex.: \"#38bdf8\"{}", what, where(n))};
    auto c = parseColor(*s);
    if (!c) throw Invalid{std::format("cor inválida em '{}': \"{}\"{}", what, *s, where(n))};
    return *c;
}

std::optional<Rgba8> getColor(const toml::table& t, std::string_view key) {
    const toml::node* n = t.get(key);
    if (!n) return std::nullopt;
    return toColor(*n, key);
}

std::optional<std::vector<Rgba8>> getPalette(const toml::table& t, std::string_view key) {
    const toml::node* n = t.get(key);
    if (!n) return std::nullopt;
    const toml::array* arr = n->as_array();
    if (!arr) throw Invalid{std::format("'{}' tem de ser uma lista de cores{}", key, where(*n))};
    if (arr->empty() || arr->size() > static_cast<size_t>(kMaxPaletteColors)) {
        throw Invalid{std::format("'{}' tem de ter entre 1 e {} cores{}", key, kMaxPaletteColors, where(*n))};
    }
    std::vector<Rgba8> out;
    for (const toml::node& item : *arr) out.push_back(toColor(item, key));
    return out;
}

std::optional<std::u32string> getCharset(const toml::table& t, std::string_view key) {
    auto s = getString(t, key);
    if (!s) return std::nullopt;
    bool ok = true;
    std::u32string cs = decodeUtf8(*s, &ok);
    if (!ok) throw Invalid{std::format("'{}' não é UTF-8 válido", key)};
    if (cs.empty()) throw Invalid{std::format("'{}' não pode estar vazio", key)};
    if (cs.size() > 4096) throw Invalid{std::format("'{}' tem demasiados caracteres (máx. 4096)", key)};
    return cs;
}

std::optional<bool> getBool(const toml::table& t, std::string_view key) {
    const toml::node* n = t.get(key);
    if (!n) return std::nullopt;
    if (!n->is_boolean()) throw Invalid{std::format("'{}' tem de ser true ou false{}", key, where(*n))};
    return n->value<bool>();
}

std::optional<float> getScale(const toml::table& t) {
    auto s = getNumber(t, "scale");
    if (s && (*s < 1.0f || *s > 400.0f)) throw Invalid{"'scale' tem de estar entre 1 e 400"};
    return s;
}

// Um valor de [post]: número entre 0 e 1.
std::optional<float> getPostValue(const toml::table& t, std::string_view key) {
    const toml::node* n = t.get(key);
    if (!n) return std::nullopt;
    const auto v = n->value<double>();
    if (!v || *v < 0.0 || *v > 1.0) throw Invalid{std::format("post.{} tem de ser um número entre 0 e 1{}", key, where(*n))};
    return static_cast<float>(*v);
}

void parseParams(const toml::table& params, Manifest& m) {
    for (auto&& [key, node] : params) {
        const std::string name(key.str());
        const toml::table* spec = node.as_table();
        ParamSpec p;
        p.name = name;
        if (spec) {
            p.defaultValue = getNumber(*spec, "default").value_or(0.0f);
            p.min = getNumber(*spec, "min").value_or(0.0f);
            p.max = getNumber(*spec, "max").value_or(1.0f);
        } else if (node.is_number()) {
            // Forma curta: speed = 1.0 (sem limites declarados)
            p.defaultValue = static_cast<float>(node.value<double>().value_or(0.0));
            p.min = -1e9f;
            p.max = 1e9f;
        } else {
            throw Invalid{std::format("parâmetro '{}' tem de ser {{ default, min, max }}{}", name, where(node))};
        }
        if (p.min > p.max) {
            throw Invalid{std::format("parâmetro '{}': min ({}) é maior que max ({})", name, p.min, p.max)};
        }
        p.value = clampParam(p, p.defaultValue);
        m.params.push_back(p);
    }
    if (m.params.size() > static_cast<size_t>(kMaxParams)) {
        throw Invalid{std::format("demasiados parâmetros ({}); o máximo é {}", m.params.size(), kMaxParams)};
    }
}

template <class Fn>
std::string catchErrors(Fn&& fn) {
    try {
        fn();
    } catch (const toml::parse_error& e) {
        return std::format("erro de sintaxe TOML na linha {}, coluna {}: {}", e.source().begin.line,
                           e.source().begin.column, e.description());
    } catch (const Invalid& e) {
        return e.message;
    }
    return {};
}

} // namespace

float& postFxValue(PostFx& fx, size_t i) {
    float* v[] = {&fx.glow, &fx.scanlines, &fx.curvature, &fx.vignette, &fx.aberration, &fx.grain};
    return *v[std::min(i, std::size(v) - 1)];
}

float postFxValue(const PostFx& fx, size_t i) { return postFxValue(const_cast<PostFx&>(fx), i); }

bool postFxActive(const PostFx& fx) {
    for (size_t i = 0; i < kPostFxNames.size(); ++i)
        if (postFxValue(fx, i) > 0.0f) return true;
    return false;
}

float clampParam(const ParamSpec& p, float v) {
    if (!(v == v)) return p.defaultValue; // NaN
    return std::clamp(v, p.min, p.max);
}

const ParamSpec* Manifest::findParam(std::string_view n) const {
    for (const auto& p : params)
        if (p.name == n) return &p;
    return nullptr;
}

ParamSpec* Manifest::findParam(std::string_view n) {
    for (auto& p : params)
        if (p.name == n) return &p;
    return nullptr;
}

ManifestResult parseManifest(std::string_view text) {
    ManifestResult result;
    Manifest& m = result.manifest;
    result.error = catchErrors([&] {
        const toml::table root = toml::parse(text);

        if (const toml::table* meta = root["meta"].as_table()) {
            m.name = getString(*meta, "name").value_or("");
            m.author = getString(*meta, "author").value_or("");
            m.version = getString(*meta, "version").value_or("");
            m.description = getString(*meta, "description").value_or("");
            m.category = getString(*meta, "category").value_or("");
        }

        const toml::table* render = root["render"].as_table();
        if (!render) throw Invalid{"falta a secção [render]"};
        const std::string mode = getString(*render, "mode").value_or("shader");
        if (mode == "shader") {
            m.mode = WallpaperMode::Shader;
            m.entry = "main.hlsl";
        } else if (mode == "script") {
            m.mode = WallpaperMode::Script;
            m.entry = "main.lua";
        } else {
            throw Invalid{std::format("render.mode tem de ser \"shader\" ou \"script\", não \"{}\"", mode)};
        }
        m.entry = getString(*render, "entry").value_or(m.entry);
        if (m.entry.empty() || m.entry.find("..") != std::string::npos) {
            throw Invalid{"render.entry tem de ser um ficheiro dentro da pasta do wallpaper"};
        }
        m.fps = getInt(*render, "fps", 1, 240).value_or(m.fps);
        m.cell = getInt(*render, "cell", 4, 128).value_or(m.cell);
        m.scale = getScale(*render).value_or(m.scale);
        m.font = getString(*render, "font").value_or(m.font);
        m.fontWeight = getInt(*render, "font_weight", 100, 950).value_or(m.fontWeight);
        m.charset = getCharset(*render, "charset").value_or(m.charset);
        m.charsetOnly = getBool(*render, "charset_only").value_or(m.charsetOnly);
        if (render->contains("glyphs")) {
            m.glyphs = getCharset(*render, "glyphs").value_or(U"");
            if (m.glyphs.size() > 256) throw Invalid{"render.glyphs tem no máximo 256 caracteres"};
        }
        m.substeps = getInt(*render, "substeps", 1, 64).value_or(m.substeps);

        if (const toml::table* in = root["inputs"].as_table()) {
            auto flag = [&](std::string_view key, bool def) {
                const toml::node* n = in->get(key);
                if (!n) return def;
                if (!n->is_boolean()) throw Invalid{std::format("inputs.{} tem de ser true ou false{}", key, where(*n))};
                return n->value<bool>().value_or(def);
            };
            m.useWindows = flag("windows", false);
            m.useIcons = flag("icons", false);
            m.useAudio = flag("audio", false);
            m.useSystem = flag("system", false);
            m.useWeather = flag("weather", false);
            m.source = getString(*in, "source").value_or("");
            m.text = getString(*in, "text").value_or("");
            if (const toml::node* n = in->get("text_interval")) {
                const auto v = n->value<double>();
                if (!v || *v < 1.0 || *v > 3600.0) {
                    throw Invalid{std::format("inputs.text_interval tem de ser um número entre 1 e 3600{}", where(*n))};
                }
                m.textInterval = static_cast<float>(*v);
            }
        }
        if (const toml::table* post = root["post"].as_table()) {
            for (size_t i = 0; i < kPostFxNames.size(); ++i) {
                if (auto v = getPostValue(*post, kPostFxNames[i])) postFxValue(m.post, i) = *v;
            }
        }

        if (const toml::table* colors = root["colors"].as_table()) {
            m.background = getColor(*colors, "background").value_or(m.background);
            if (auto p = getPalette(*colors, "palette")) m.palette = std::move(*p);
        }
        if (m.palette.empty()) {
            m.palette = {m.background, Rgba8{224, 242, 254, 255}};
        }

        if (const toml::table* params = root["params"].as_table()) {
            parseParams(*params, m);
        }
    });
    return result;
}

ThemeResult parseTheme(std::string_view text, bool requireBase) {
    ThemeResult result;
    Theme& t = result.theme;
    result.error = catchErrors([&] {
        const toml::table root = toml::parse(text);
        t.base = getString(root, "base").value_or("");
        if (requireBase && t.base.empty()) throw Invalid{"um tema precisa de 'base = \"<wallpaper>\"'"};
        t.font = getString(root, "font");
        t.fontWeight = getInt(root, "font_weight", 100, 950);
        t.cell = getInt(root, "cell", 4, 128);
        t.scale = getScale(root);
        t.fps = getInt(root, "fps", 1, 240);
        t.charset = getCharset(root, "charset");
        t.charsetOnly = getBool(root, "charset_only");
        t.background = getColor(root, "background");
        t.palette = getPalette(root, "palette");
        t.source = getString(root, "source");
        t.text = getString(root, "text");
        if (const toml::table* post = root["post"].as_table()) {
            for (size_t i = 0; i < kPostFxNames.size(); ++i) t.post[i] = getPostValue(*post, kPostFxNames[i]);
        }
        if (const toml::table* in = root["inputs"].as_table()) {
            auto flag = [&](std::string_view key) -> std::optional<bool> {
                const toml::node* n = in->get(key);
                if (!n) return std::nullopt;
                if (!n->is_boolean()) throw Invalid{std::format("inputs.{} tem de ser true ou false{}", key, where(*n))};
                return n->value<bool>();
            };
            t.useWindows = flag("windows");
            t.useIcons = flag("icons");
            t.useAudio = flag("audio");
            t.useSystem = flag("system");
            t.useWeather = flag("weather");
            if (auto s = getString(*in, "source")) t.source = s;
            if (auto s = getString(*in, "text")) t.text = s;
        }
        if (const toml::table* params = root["params"].as_table()) {
            for (auto&& [key, node] : *params) {
                if (!node.is_number()) {
                    throw Invalid{std::format("params.{} tem de ser um número{}", key.str(), where(node))};
                }
                t.params.emplace_back(std::string(key.str()), static_cast<float>(node.value<double>().value_or(0)));
            }
        }
    });
    return result;
}

std::vector<std::string> applyTheme(Manifest& m, const Theme& t) {
    std::vector<std::string> warnings;
    if (t.font) m.font = *t.font;
    if (t.fontWeight) m.fontWeight = *t.fontWeight;
    if (t.cell) m.cell = *t.cell;
    if (t.scale) m.scale = *t.scale;
    if (t.fps) m.fps = *t.fps;
    if (t.charset) m.charset = *t.charset;
    if (t.charsetOnly) m.charsetOnly = *t.charsetOnly;
    if (t.background) m.background = *t.background;
    if (t.palette) m.palette = *t.palette;
    if (t.source) m.source = *t.source;
    if (t.text) m.text = *t.text;
    for (size_t i = 0; i < kPostFxNames.size(); ++i)
        if (t.post[i]) postFxValue(m.post, i) = *t.post[i];
    if (t.useWindows) m.useWindows = *t.useWindows;
    if (t.useIcons) m.useIcons = *t.useIcons;
    if (t.useAudio) m.useAudio = *t.useAudio;
    if (t.useSystem) m.useSystem = *t.useSystem;
    if (t.useWeather) m.useWeather = *t.useWeather;
    for (const auto& [name, value] : t.params) {
        if (ParamSpec* p = m.findParam(name)) {
            p->value = clampParam(*p, value);
            if (p->value != value) {
                warnings.push_back(std::format("params.{} = {} fora de [{}, {}]; usado {}", name, value, p->min,
                                               p->max, p->value));
            }
        } else {
            warnings.push_back(std::format("o wallpaper '{}' não tem o parâmetro '{}'", t.base, name));
        }
    }
    return warnings;
}

namespace {
std::string colorHex(Rgba8 c) {
    return c.a == 255 ? std::format("#{:02x}{:02x}{:02x}", c.r, c.g, c.b)
                      : std::format("#{:02x}{:02x}{:02x}{:02x}", c.r, c.g, c.b, c.a);
}
} // namespace

std::string themeToToml(const Theme& t, std::string_view headerComment) {
    toml::table root;
    if (!t.base.empty()) root.insert("base", t.base);
    if (t.font) root.insert("font", *t.font);
    if (t.fontWeight) root.insert("font_weight", *t.fontWeight);
    if (t.cell) root.insert("cell", *t.cell);
    if (t.scale) root.insert("scale", static_cast<double>(*t.scale));
    if (t.fps) root.insert("fps", *t.fps);
    if (t.charset) root.insert("charset", encodeUtf8(*t.charset));
    if (t.charsetOnly) root.insert("charset_only", *t.charsetOnly);
    if (t.background) root.insert("background", colorHex(*t.background));
    if (t.source) root.insert("source", *t.source);
    if (t.text) root.insert("text", *t.text);
    if (t.palette) {
        toml::array arr;
        for (const Rgba8& c : *t.palette) arr.push_back(colorHex(c));
        root.insert("palette", std::move(arr));
    }
    toml::table post;
    for (size_t i = 0; i < kPostFxNames.size(); ++i)
        if (t.post[i]) post.insert(kPostFxNames[i], static_cast<double>(*t.post[i]));
    if (!post.empty()) root.insert("post", std::move(post));
    toml::table inputs;
    const std::pair<const char*, const std::optional<bool>*> flags[] = {
        {"windows", &t.useWindows}, {"icons", &t.useIcons},     {"audio", &t.useAudio},
        {"system", &t.useSystem},   {"weather", &t.useWeather},
    };
    for (const auto& [key, v] : flags)
        if (*v) inputs.insert(key, **v);
    if (!inputs.empty()) root.insert("inputs", std::move(inputs));
    if (!t.params.empty()) {
        toml::table params;
        for (const auto& [name, value] : t.params) params.insert(name, static_cast<double>(value));
        root.insert("params", std::move(params));
    }
    std::ostringstream out;
    if (!headerComment.empty()) out << "# " << headerComment << "\n";
    out << root << "\n";
    return out.str();
}

bool themeIsEmpty(const Theme& t) {
    return !t.font && !t.fontWeight && !t.cell && !t.scale && !t.fps && !t.charset && !t.charsetOnly && !t.background &&
           !t.palette && !t.source && !t.text && t.params.empty() &&
           std::none_of(t.post.begin(), t.post.end(), [](const auto& v) { return v.has_value(); }) &&
           !t.useWindows && !t.useIcons && !t.useAudio && !t.useSystem && !t.useWeather;
}

bool looksLikeTheme(std::string_view text) {
    try {
        const toml::table root = toml::parse(text);
        return root.contains("base") && !root.contains("render");
    } catch (const toml::parse_error&) {
        return false;
    }
}

} // namespace ap
