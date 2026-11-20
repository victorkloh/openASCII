#pragma once
// O cell buffer de um monitor e o desenho final (grid.hlsl): uma draw call por monitor.

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <span>
#include <string_view>

#include "core/Color.h"
#include "core/Grid.h"
#include "render/GlyphAtlas.h"

namespace ap::render {

using Microsoft::WRL::ComPtr;

// Uma célula, exatamente como a GPU a lê (12 bytes): ver grid.hlsl.
struct CellData {
    uint32_t glyph; // slot do atlas (16 bits baixos); os 16 altos ficam para flags
    uint32_t fg;    // RGBA8
    uint32_t bg;    // RGBA8 (alpha 0 = cor de fundo do wallpaper)
};
static_assert(sizeof(CellData) == 12);

// Shaders e estado partilhados por todos os monitores (dependem só do dispositivo).
class GridPipeline {
public:
    HRESULT create(ID3D11Device* device);
    void reset();
    bool valid() const { return ps_ != nullptr; }

    ID3D11VertexShader* vs() const { return vs_.Get(); }
    ID3D11PixelShader* ps() const { return ps_.Get(); }

private:
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
};

class GridRenderer {
public:
    // Recria os buffers para um ecrã de width×height px com as células do atlas.
    HRESULT resize(ID3D11Device* device, int width, int height, const GlyphAtlas& atlas);
    void reset();
    bool valid() const { return cells_ != nullptr; }

    const GridLayout& layout() const { return layout_; }

    // O compute shader escreve nas células e no estado "next"; lê o estado "prev".
    ID3D11UnorderedAccessView* cellsUav() const { return cellsUav_.Get(); }
    ID3D11ShaderResourceView* statePrevSrv() const { return stateSrv_[prev_].Get(); }
    ID3D11UnorderedAccessView* stateNextUav() const { return stateUav_[1 - prev_].Get(); }
    void swapState() { prev_ = 1 - prev_; } // double buffering para prev()

    // Escreve uma linha de texto ASCII por cima das células (mensagens de erro).
    void writeText(ID3D11DeviceContext* ctx, const GlyphAtlas& atlas, int col, int row, std::string_view text,
                   Rgba8 fg, Rgba8 bg);
    // Preenche tudo com células vazias (quando não há wallpaper a correr).
    void clear(ID3D11DeviceContext* ctx);

    void draw(ID3D11DeviceContext* ctx, const GridPipeline& pipeline, ID3D11RenderTargetView* rtv,
              const GlyphAtlas& atlas, Rgba8 background);

private:
    GridLayout layout_;
    int width_ = 0, height_ = 0;
    ComPtr<ID3D11Buffer> cells_;
    ComPtr<ID3D11ShaderResourceView> cellsSrv_;
    ComPtr<ID3D11UnorderedAccessView> cellsUav_;
    ComPtr<ID3D11Buffer> state_[2];
    ComPtr<ID3D11ShaderResourceView> stateSrv_[2];
    ComPtr<ID3D11UnorderedAccessView> stateUav_[2];
    int prev_ = 0;
    ComPtr<ID3D11Buffer> constants_;
};

} // namespace ap::render
