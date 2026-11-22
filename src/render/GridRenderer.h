#pragma once
// CellCanvas: o cell buffer (e o estado de prev()) de um canvas — no modo estendido, um só
// para todos os monitores. GridRenderer: o desenho final (grid.hlsl) de UM monitor, que lê
// a sua parte do canvas. Uma draw call por monitor.

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <span>
#include <string_view>

#include "core/Canvas.h"
#include "core/Color.h"
#include "core/Grid.h"
#include "core/Manifest.h"
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

// Shaders partilhados por todos os monitores (dependem só do dispositivo).
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

class CellCanvas {
public:
    // Recria os buffers se o tamanho mudou (o estado da simulação recomeça).
    HRESULT resize(ID3D11Device* device, const CanvasLayout& layout);
    void reset();
    bool valid() const { return cells_ != nullptr; }
    const CanvasLayout& layout() const { return layout_; }

    ID3D11ShaderResourceView* cellsSrv() const { return cellsSrv_.Get(); }
    ID3D11UnorderedAccessView* cellsUav() const { return cellsUav_.Get(); }
    ID3D11ShaderResourceView* statePrevSrv() const { return stateSrv_[prev_].Get(); }
    ID3D11UnorderedAccessView* stateNextUav() const { return stateUav_[1 - prev_].Get(); }
    void swapState() { prev_ = 1 - prev_; } // double buffering para prev()

    // Escreve uma linha de texto (UTF-8) por cima das células (mensagens de erro).
    void writeText(ID3D11DeviceContext* ctx, const GlyphAtlas& atlas, int col, int row, std::string_view text,
                   Rgba8 fg, Rgba8 bg);
    void clear(ID3D11DeviceContext* ctx);

private:
    CanvasLayout layout_;
    ComPtr<ID3D11Buffer> cells_;
    ComPtr<ID3D11ShaderResourceView> cellsSrv_;
    ComPtr<ID3D11UnorderedAccessView> cellsUav_;
    ComPtr<ID3D11Buffer> state_[2];
    ComPtr<ID3D11ShaderResourceView> stateSrv_[2];
    ComPtr<ID3D11UnorderedAccessView> stateUav_[2];
    int prev_ = 0;
};

class GridRenderer {
public:
    // A grelha que ESTE monitor desenha (as células dele, com o tamanho do atlas dele).
    void setLayout(int widthPx, int heightPx, const GlyphAtlas& atlas);
    const GridLayout& layout() const { return layout_; }
    void reset() { constants_.Reset(); layout_ = {}; }

    // monitorRectPx: onde está este monitor no ecrã virtual (para saber que parte do canvas mostrar).
    HRESULT draw(ID3D11Device* device, ID3D11DeviceContext* ctx, const GridPipeline& pipeline,
                 ID3D11RenderTargetView* rtv, const GlyphAtlas& atlas, const CellCanvas& canvas,
                 const Rect& monitorRectPx, Rgba8 background, const PostFx& post = {}, float time = 0.0f);

private:
    GridLayout layout_;
    int width_ = 0, height_ = 0;
    ComPtr<ID3D11Buffer> constants_;
};

} // namespace ap::render
