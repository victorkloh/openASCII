#include "render/GlyphAtlas.h"

#include "core/ProceduralGlyphs.h"
#include "util/Log.h"
#include "util/Win32.h"

#include <algorithm>
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
    return k;
}

void GlyphAtlas::reset() {
    srv_.Reset();
    slots_.clear();
    key_.clear();
}

uint16_t GlyphAtlas::slot(char32_t cp) const {
    const auto it = slots_.find(cp);
    return it != slots_.end() ? it->second : fallback_;
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
    for (char32_t c = 0x20; c < 0x7F; ++c) {
        if (chars.find(c) == std::u32string::npos) chars += c;
    }
    for (char32_t c = 0xA1; c <= 0xFF; ++c) { // Latin-1: acentos das mensagens em português
        if (chars.find(c) == std::u32string::npos) chars += c;
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
        if (!slots_.contains(cp)) slots_[cp] = static_cast<uint16_t>(i);
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

    cellW_ = cellW;
    cellH_ = cellH;
    columns_ = columns;
    key_ = desc.key();
    log::info(L"Atlas: {} glifos de {}x{} px ({} no charset{}), textura {}x{}", count, cellW, cellH, charsetSize_,
              missing ? std::format(L", {} sem glifo na fonte", missing) : L"", atlasW, atlasH);
    return S_OK;
}

} // namespace ap::render
