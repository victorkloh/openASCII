#include "render/GlyphAtlas.h"

#include "core/ProceduralGlyphs.h"
#include "util/Log.h"
#include "util/Win32.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <vector>

namespace ap::render {

using Microsoft::WRL::ComPtr;

namespace {

bool looksLikeFontFile(const std::wstring& name) {
    const auto ext = std::filesystem::path(name).extension().wstring();
    return _wcsicmp(ext.c_str(), L".ttf") == 0 || _wcsicmp(ext.c_str(), L".otf") == 0 ||
           _wcsicmp(ext.c_str(), L".ttc") == 0;
}

HRESULT faceFromFile(IDWriteFactory2* dwrite, const std::filesystem::path& path, ComPtr<IDWriteFontFace>& face) {
    ComPtr<IDWriteFontFile> file;
    HRESULT hr = dwrite->CreateFontFileReference(path.c_str(), nullptr, &file);
    if (FAILED(hr)) return hr;
    BOOL supported = FALSE;
    DWRITE_FONT_FILE_TYPE fileType{};
    DWRITE_FONT_FACE_TYPE faceType{};
    UINT32 faces = 0;
    hr = file->Analyze(&supported, &fileType, &faceType, &faces);
    if (FAILED(hr)) return hr;
    if (!supported) return DWRITE_E_FILEFORMAT;
    IDWriteFontFile* files[] = {file.Get()};
    return dwrite->CreateFontFace(faceType, 1, files, 0, DWRITE_FONT_SIMULATIONS_NONE, &face);
}

HRESULT faceFromSystem(IDWriteFactory2* dwrite, const std::wstring& family, int weight,
                       ComPtr<IDWriteFontFace>& face) {
    ComPtr<IDWriteFontCollection> fonts;
    HRESULT hr = dwrite->GetSystemFontCollection(&fonts);
    if (FAILED(hr)) return hr;
    UINT32 index = 0;
    BOOL exists = FALSE;
    fonts->FindFamilyName(family.c_str(), &index, &exists);
    if (!exists) return DWRITE_E_NOFONT;
    ComPtr<IDWriteFontFamily> fam;
    ComPtr<IDWriteFont> font;
    if (FAILED(hr = fonts->GetFontFamily(index, &fam))) return hr;
    if (FAILED(hr = fam->GetFirstMatchingFont(static_cast<DWRITE_FONT_WEIGHT>(weight), DWRITE_FONT_STRETCH_NORMAL,
                                              DWRITE_FONT_STYLE_NORMAL, &font))) {
        return hr;
    }
    return font->CreateFontFace(&face);
}

// Rasteriza um glifo da fonte para a célula (x0,y0) do atlas, com anti-aliasing em escala
// de cinza. A linha de base fica sempre no mesmo píxel inteiro em todas as células.
void rasterizeGlyph(IDWriteFactory2* dwrite, IDWriteFontFace* face, UINT16 glyph, float emSize, int baseline,
                    int cellW, int cellH, std::vector<uint8_t>& atlas, int atlasW, int x0, int y0) {
    float advance = 0.0f;
    DWRITE_GLYPH_OFFSET offset{};
    DWRITE_GLYPH_RUN run{};
    run.fontFace = face;
    run.fontEmSize = emSize;
    run.glyphCount = 1;
    run.glyphIndices = &glyph;
    run.glyphAdvances = &advance;
    run.glyphOffsets = &offset;

    ComPtr<IDWriteGlyphRunAnalysis> analysis;
    if (FAILED(dwrite->CreateGlyphRunAnalysis(&run, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                              DWRITE_MEASURING_MODE_NATURAL, DWRITE_GRID_FIT_MODE_ENABLED,
                                              DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, 0.0f,
                                              static_cast<float>(baseline), &analysis))) {
        return;
    }

    // Com anti-aliasing em cinza o DirectWrite dá 1 byte por píxel (textura "1x1").
    // Se por algum motivo não der, pedimos ClearType (3 bytes por píxel) e fazemos a média.
    RECT b{};
    bool cleartype = false;
    analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &b);
    if (b.right <= b.left || b.bottom <= b.top) {
        analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &b);
        cleartype = true;
    }
    const int bw = b.right - b.left, bh = b.bottom - b.top;
    if (bw <= 0 || bh <= 0) return; // espaço ou glifo vazio

    const int bpp = cleartype ? 3 : 1;
    std::vector<uint8_t> pixels(static_cast<size_t>(bw) * bh * bpp);
    if (FAILED(analysis->CreateAlphaTexture(cleartype ? DWRITE_TEXTURE_CLEARTYPE_3x1 : DWRITE_TEXTURE_ALIASED_1x1, &b,
                                            pixels.data(), static_cast<UINT32>(pixels.size())))) {
        return;
    }
    for (int y = 0; y < bh; ++y) {
        const int ty = b.top + y;
        if (ty < 0 || ty >= cellH) continue; // recortar à célula
        for (int x = 0; x < bw; ++x) {
            const int tx = b.left + x;
            if (tx < 0 || tx >= cellW) continue;
            const uint8_t* src = &pixels[(static_cast<size_t>(y) * bw + x) * bpp];
            const uint8_t v = cleartype ? static_cast<uint8_t>((src[0] + src[1] + src[2]) / 3) : src[0];
            atlas[static_cast<size_t>(y0 + ty) * atlasW + (x0 + tx)] = v;
        }
    }
}

} // namespace

std::wstring AtlasDesc::key() const {
    std::wstring k = std::format(L"{}|{}|{}|", font, weight, cellHeightPx);
    for (char32_t c : charset) k += std::format(L"{:x},", static_cast<unsigned>(c));
    k += L'|';
    for (char32_t c : glyphs) k += std::format(L"{:x},", static_cast<unsigned>(c));
    if (charsetOnly) k += L"|only";
    return k;
}

void GlyphAtlas::reset() {
    srv_.Reset();
    inkSrv_.Reset();
    slotCount_ = 0;
    slots_.clear();
    textSlots_.clear();
    key_.clear();
}

uint16_t GlyphAtlas::slot(char32_t cp) const {
    const auto it = slots_.find(cp);
    return it != slots_.end() ? it->second : fallback_;
}

uint16_t GlyphAtlas::textSlot(char32_t cp) const {
    if (textSlots_.empty()) return slot(cp);
    const auto it = textSlots_.find(cp);
    if (it != textSlots_.end()) return it->second;
    const auto q = textSlots_.find(U'?');
    return q != textSlots_.end() ? q->second : fallback_;
}

HRESULT GlyphAtlas::build(ID3D11Device* device, IDWriteFactory2* dwrite, const AtlasDesc& desc) {
    reset();

    // 1) Fonte: ficheiro na pasta do wallpaper, ou fonte do sistema, com alternativas.
    ComPtr<IDWriteFontFace> face;
    HRESULT hr = E_FAIL;
    if (looksLikeFontFile(desc.font)) {
        hr = faceFromFile(dwrite, desc.fontDir / desc.font, face);
        if (FAILED(hr)) log::warn(L"Fonte '{}' não carregou ({})", desc.font, win::hrToString(hr));
    } else {
        hr = faceFromSystem(dwrite, desc.font, desc.weight, face);
        if (FAILED(hr)) log::warn(L"Fonte '{}' não está instalada", desc.font);
    }
    for (const wchar_t* alt : {L"Cascadia Mono", L"Consolas", L"Courier New"}) {
        if (SUCCEEDED(hr)) break;
        hr = faceFromSystem(dwrite, alt, desc.weight, face);
        if (SUCCEEDED(hr)) log::info(L"A usar a fonte alternativa '{}'", alt);
    }
    if (FAILED(hr)) return hr;

    // 2) Métricas, arredondadas a píxeis inteiros (sem meio píxel = sem blur nem costuras).
    //    A altura da célula é ascent+descent da fonte; o lineGap é ignorado para os
    //    caracteres preencherem a célula.
    DWRITE_FONT_METRICS fm{};
    face->GetMetrics(&fm);
    const int cellH = std::max(4, desc.cellHeightPx);
    const float emSize = static_cast<float>(cellH) * fm.designUnitsPerEm / static_cast<float>(fm.ascent + fm.descent);
    const int baseline = static_cast<int>(std::lround(fm.ascent * emSize / fm.designUnitsPerEm));

    const UINT32 zero = U'0';
    UINT16 zeroGlyph = 0;
    face->GetGlyphIndicesW(&zero, 1, &zeroGlyph);
    DWRITE_GLYPH_METRICS gm{};
    face->GetDesignGlyphMetrics(&zeroGlyph, 1, &gm, FALSE);
    const int cellW = std::max(2, static_cast<int>(std::lround(gm.advanceWidth * emSize / fm.designUnitsPerEm)));

    // 3) Lista de caracteres: charset primeiro (slots 0..N-1), depois o ASCII imprimível
    //    (para mensagens de erro no ecrã) e por fim '?' como substituto.
    std::u32string chars = desc.charset.empty() ? std::u32string(U" ") : desc.charset;
    charsetSize_ = static_cast<int>(chars.size());
    chars += desc.glyphs; // sprites logo a seguir (posições fixas, mesmo que repitam caracteres)
    glyphCount_ = static_cast<int>(desc.glyphs.size());
    for (char32_t c = 0x20; c < 0x7F; ++c) {
        if (chars.find(c) == std::u32string::npos) chars += c;
    }
    for (char32_t c = 0xA1; c <= 0xFF; ++c) { // Latin-1: acentos das mensagens em português
        if (chars.find(c) == std::u32string::npos) chars += c;
    }
    // "Só o charset": os slots acima vão ser redesenhados com caracteres do charset; as
    // mensagens da app precisam do texto verdadeiro, por isso vai uma cópia intacta no fim.
    const bool charsetOnly = desc.charsetOnly && desc.charset.size() > 0;
    const int pristineStart = static_cast<int>(chars.size());
    if (charsetOnly) {
        for (char32_t c = 0x20; c < 0x7F; ++c) chars += c;
        for (char32_t c = 0xA1; c <= 0xFF; ++c) chars += c;
    }
    if (chars.size() > 65535) chars.resize(65535);

    // 4) Rasterizar para um buffer em CPU e enviar para a GPU de uma vez.
    const int count = static_cast<int>(chars.size());
    const int columns = std::max(1, std::min(count, 4096 / cellW));
    const int rows = (count + columns - 1) / columns;
    const int atlasW = columns * cellW, atlasH = rows * cellH;
    std::vector<uint8_t> atlas(static_cast<size_t>(atlasW) * atlasH, 0);

    std::vector<UINT32> codepoints(chars.begin(), chars.end());
    std::vector<UINT16> glyphs(codepoints.size());
    face->GetGlyphIndicesW(codepoints.data(), static_cast<UINT32>(codepoints.size()), glyphs.data());

    int missing = 0;
    for (int i = 0; i < count; ++i) {
        const char32_t cp = chars[static_cast<size_t>(i)];
        const int x0 = (i % columns) * cellW, y0 = (i / columns) * cellH;
        if (i >= pristineStart) {
            textSlots_[cp] = static_cast<uint16_t>(i); // cópia intacta (só com charsetOnly)
        } else if (!slots_.contains(cp)) {
            slots_[cp] = static_cast<uint16_t>(i);
        }
        if (isProceduralGlyph(cp)) {
            std::span<uint8_t> cell(atlas.data() + static_cast<size_t>(y0) * atlasW + x0,
                                    static_cast<size_t>(cellH - 1) * atlasW + cellW);
            rasterizeProceduralGlyph(cp, cellW, cellH, cell, atlasW);
        } else if (glyphs[static_cast<size_t>(i)] != 0) {
            rasterizeGlyph(dwrite, face.Get(), glyphs[static_cast<size_t>(i)], emSize, baseline, cellW, cellH, atlas,
                           atlasW, x0, y0);
        } else if (cp != U' ') {
            ++missing;
        }
    }
    fallback_ = slot(U'?');

    // Tinta de cada glifo: a média da cobertura da célula (o glow usa-a como intensidade, e o
    // modo "só o charset" escolhe por ela o substituto de cada sprite).
    auto cellOrigin = [&](int i) { return std::pair{(i % columns) * cellW, (i / columns) * cellH}; };
    std::vector<float> ink(static_cast<size_t>(count), 0.0f);
    for (int i = 0; i < count; ++i) {
        const auto [x0, y0] = cellOrigin(i);
        uint32_t sum = 0;
        for (int y = 0; y < cellH; ++y)
            for (int x = 0; x < cellW; ++x) sum += atlas[static_cast<size_t>(y0 + y) * atlasW + x0 + x];
        ink[static_cast<size_t>(i)] = static_cast<float>(sum) / (255.0f * static_cast<float>(cellW * cellH));
    }

    if (charsetOnly) {
        // Cada sprite/caractere de texto passa a ter o desenho do caractere do charset mais
        // parecido: a mesma tinta relativa (o mais "cheio" vai para o mais cheio do charset) E a
        // tinta no mesmo sítio da célula (grelha 3x3), para um '/' ou um '<' ficarem com um
        // caractere de forma parecida quando o charset o permite (braille, caixas). O que é vazio
        // continua vazio (espaços entre palavras, partes transparentes), e um sprite que já é um
        // caractere do charset fica como está.
        constexpr float kBlank = 0.004f;
        auto features = [&](int i) {
            const auto [x0, y0] = cellOrigin(i);
            std::array<float, 9> f{};
            std::array<int, 9> n{};
            for (int y = 0; y < cellH; ++y) {
                for (int x = 0; x < cellW; ++x) {
                    const int b = (y * 3 / cellH) * 3 + (x * 3 / cellW);
                    f[static_cast<size_t>(b)] += atlas[static_cast<size_t>(y0 + y) * atlasW + x0 + x] / 255.0f;
                    ++n[static_cast<size_t>(b)];
                }
            }
            // Distribuição da tinta pelas 9 zonas (soma 1): a FORMA, independente da quantidade.
            float total = 0.0f;
            for (size_t b = 0; b < 9; ++b) {
                f[b] = n[b] ? f[b] / static_cast<float>(n[b]) : 0.0f;
                total += f[b];
            }
            if (total > 0.0f)
                for (float& v : f) v /= total;
            return f;
        };
        std::vector<int> candidates;
        float maxCharset = 0.0f, maxOthers = 0.0f;
        for (int i = 0; i < charsetSize_; ++i) {
            if (ink[static_cast<size_t>(i)] > kBlank) {
                candidates.push_back(i);
                maxCharset = std::max(maxCharset, ink[static_cast<size_t>(i)]);
            }
        }
        for (int i = charsetSize_; i < pristineStart; ++i) maxOthers = std::max(maxOthers, ink[static_cast<size_t>(i)]);
        int replaced = 0;
        if (!candidates.empty() && maxOthers > kBlank) {
            std::vector<std::array<float, 9>> candidateFeatures;
            for (int c : candidates) candidateFeatures.push_back(features(c));
            for (int i = charsetSize_; i < pristineStart; ++i) {
                const char32_t cp = chars[static_cast<size_t>(i)];
                if (ink[static_cast<size_t>(i)] <= kBlank || desc.charset.find(cp) != std::u32string::npos) continue;
                const auto f = features(i);
                // Densidade relativa: o mais cheio dos sprites corresponde ao mais cheio do charset.
                const float target = ink[static_cast<size_t>(i)] / maxOthers;
                int best = candidates.front();
                float bestDist = 1e9f;
                for (size_t j = 0; j < candidates.size(); ++j) {
                    // Forma (distribuição da tinta, 0..2) + densidade relativa (0..1). Nos conjuntos
                    // de "rampa" (blocos, pontos) as formas quase não diferem e decide a densidade;
                    // nos de linhas (caixas, braille) decide a forma: '(' vira '│', '~' vira '─'.
                    float shape = 0.0f;
                    for (size_t b = 0; b < 9; ++b) {
                        const float d = f[b] - candidateFeatures[j][b];
                        shape += d * d;
                    }
                    const float density = ink[static_cast<size_t>(candidates[j])] / maxCharset - target;
                    const float dist = shape + density * density;
                    if (dist < bestDist) {
                        bestDist = dist;
                        best = candidates[j];
                    }
                }
                const auto [dx, dy] = cellOrigin(i);
                const auto [sx, sy] = cellOrigin(best);
                for (int y = 0; y < cellH; ++y) {
                    std::copy_n(&atlas[static_cast<size_t>(sy + y) * atlasW + sx], cellW,
                                &atlas[static_cast<size_t>(dy + y) * atlasW + dx]);
                }
                ink[static_cast<size_t>(i)] = ink[static_cast<size_t>(best)];
                ++replaced;
            }
        }
        log::info(L"Atlas: só o charset — {} sprites/caracteres trocados por caracteres do charset", replaced);
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(atlasW);
    td.Height = static_cast<UINT>(atlasH);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{atlas.data(), static_cast<UINT>(atlasW), 0};
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(hr = device->CreateTexture2D(&td, &init, &tex)) ||
        FAILED(hr = device->CreateShaderResourceView(tex.Get(), nullptr, &srv_))) {
        log::error(L"Atlas: textura {}x{} falhou: {}", atlasW, atlasH, win::hrToString(hr));
        return hr;
    }

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = static_cast<UINT>(ink.size() * sizeof(float));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(float);
    D3D11_SUBRESOURCE_DATA inkInit{ink.data(), 0, 0};
    ComPtr<ID3D11Buffer> inkBuffer;
    if (FAILED(hr = device->CreateBuffer(&bd, &inkInit, &inkBuffer)) ||
        FAILED(hr = device->CreateShaderResourceView(inkBuffer.Get(), nullptr, &inkSrv_))) {
        log::error(L"Atlas: buffer de tinta falhou: {}", win::hrToString(hr));
        return hr;
    }

    slotCount_ = count;
    cellW_ = cellW;
    cellH_ = cellH;
    columns_ = columns;
    key_ = desc.key();
    log::info(L"Atlas: {} glifos de {}x{} px ({} no charset{}), textura {}x{}", count, cellW, cellH, charsetSize_,
              missing ? std::format(L", {} sem glifo na fonte", missing) : L"", atlasW, atlasH);
    return S_OK;
}

} // namespace ap::render
