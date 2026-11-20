#pragma once
// Atlas de glifos: todos os caracteres de que precisamos, rasterizados UMA vez no tamanho
// exato em píxeis para o DPI atual, numa textura R8 (só cobertura, 1 byte por píxel).
// Nunca escalamos o atlas: se o DPI muda, reconstruímos. É isso que dá texto nítido.

#include <d3d11.h>
#include <dwrite_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace ap::render {

struct AtlasDesc {
    std::wstring font;              // nome de sistema ou caminho para .ttf/.otf
    std::filesystem::path fontDir;  // pasta do wallpaper (para caminhos relativos)
    int weight = 400;
    int cellHeightPx = 20;          // já multiplicado pela escala do DPI
    std::u32string charset;         // ocupa os slots 0..N-1, pela mesma ordem

    std::wstring key() const;       // identifica o atlas (para saber quando reconstruir)
};

class GlyphAtlas {
public:
    HRESULT build(ID3D11Device* device, IDWriteFactory2* dwrite, const AtlasDesc& desc);
    void reset();

    bool valid() const { return srv_ != nullptr; }
    ID3D11ShaderResourceView* srv() const { return srv_.Get(); }
    int cellW() const { return cellW_; }
    int cellH() const { return cellH_; }
    int columns() const { return columns_; }
    int charsetSize() const { return charsetSize_; }
    const std::wstring& key() const { return key_; }

    // Slot do atlas para um caractere (ASCII imprimível está sempre presente; o resto cai em '?').
    uint16_t slot(char32_t cp) const;

private:
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv_;
    std::unordered_map<char32_t, uint16_t> slots_;
    int cellW_ = 1, cellH_ = 1, columns_ = 1, charsetSize_ = 1;
    uint16_t fallback_ = 0;
    std::wstring key_;
};

} // namespace ap::render
