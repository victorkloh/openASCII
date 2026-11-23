#include "render/GridRenderer.h"

#include "core/Utf8.h"
#include "render/ShaderCompiler.h"
#include "resource.h"
#include "util/Log.h"
#include "util/Win32.h"

#include <algorithm>
#include <vector>

namespace ap::render {

namespace {

// Espelho do cbuffer GridCB de grid.hlsl.
struct alignas(16) GridConstants {
    int32_t gridW, gridH;        // grelha do monitor
    int32_t cellW, cellH;
    int32_t originX, originY;    // margem do monitor
    int32_t atlasCols;
    uint32_t flags;
    float monitorToCanvasX;      // origem do monitor - origem do canvas (px)
    float monitorToCanvasY;
    float canvasCellW, canvasCellH;
    int32_t canvasW, canvasH;
    int32_t slotCount, pad1;
    float background[4];
    float post0[4]; // glow, scanlines, curvature, vignette
    float post1[4]; // aberration, grain, tempo (s), escala do DPI
};
static_assert(sizeof(GridConstants) == 112);

HRESULT createStructured(ID3D11Device* device, UINT stride, UINT count, ComPtr<ID3D11Buffer>& buffer,
                         ComPtr<ID3D11ShaderResourceView>& srv, ComPtr<ID3D11UnorderedAccessView>& uav) {
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = stride * count;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = stride;
    std::vector<uint8_t> zeros(bd.ByteWidth, 0); // começa tudo vazio (glifo 0, cores transparentes)
    D3D11_SUBRESOURCE_DATA init{zeros.data(), 0, 0};
    HRESULT hr = device->CreateBuffer(&bd, &init, &buffer);
    if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(buffer.Get(), nullptr, &srv);
    if (SUCCEEDED(hr)) hr = device->CreateUnorderedAccessView(buffer.Get(), nullptr, &uav);
    return hr;
}

void toFloat4(Rgba8 c, float out[4]) {
    out[0] = c.r / 255.0f;
    out[1] = c.g / 255.0f;
    out[2] = c.b / 255.0f;
    out[3] = c.a / 255.0f;
}

} // namespace

// ---------------------------------------------------------------------------

HRESULT GridPipeline::create(ID3D11Device* device) {
    reset();
    const std::string source = loadTextResource(IDR_SHADER_GRID);
    if (source.empty()) {
        log::error(L"grid.hlsl não está embebido no executável");
        return E_FAIL;
    }
    const CompiledShader vs = compileShader(source, "grid.hlsl", "VSMain", "vs_5_0");
    const CompiledShader ps = compileShader(source, "grid.hlsl", "PSMain", "ps_5_0");
    if (!vs.ok() || !ps.ok()) {
        log::error(L"grid.hlsl não compilou: {}", std::wstring(vs.errors.begin(), vs.errors.end()) +
                                                      std::wstring(ps.errors.begin(), ps.errors.end()));
        return E_FAIL;
    }
    HRESULT hr = device->CreateVertexShader(vs.blob->GetBufferPointer(), vs.blob->GetBufferSize(), nullptr, &vs_);
    if (SUCCEEDED(hr)) {
        hr = device->CreatePixelShader(ps.blob->GetBufferPointer(), ps.blob->GetBufferSize(), nullptr, &ps_);
    }
    return hr;
}

void GridPipeline::reset() {
    vs_.Reset();
    ps_.Reset();
}

// ---------------------------------------------------------------------------

void CellCanvas::reset() {
    cells_.Reset();
    cellsSrv_.Reset();
    cellsUav_.Reset();
    for (int i = 0; i < 2; ++i) {
        state_[i].Reset();
        stateSrv_[i].Reset();
        stateUav_[i].Reset();
    }
    layout_ = {};
}

HRESULT CellCanvas::resize(ID3D11Device* device, const CanvasLayout& next) {
    const bool sameSize = valid() && next.cols == layout_.cols && next.rows == layout_.rows;
    if (sameSize) {
        layout_ = next; // a origem/máscara podem mudar sem recriar os buffers
        return S_OK;
    }
    reset();
    layout_ = next;
    const UINT count = static_cast<UINT>(std::max(1, layout_.cellCount()));
    HRESULT hr = createStructured(device, sizeof(CellData), count, cells_, cellsSrv_, cellsUav_);
    for (int i = 0; i < 2 && SUCCEEDED(hr); ++i) {
        hr = createStructured(device, 16, count, state_[i], stateSrv_[i], stateUav_[i]);
    }
    if (FAILED(hr)) {
        log::error(L"Canvas {}x{} falhou: {}", layout_.cols, layout_.rows, win::hrToString(hr));
        reset();
        return hr;
    }
    prev_ = 0;
    log::info(L"Canvas {}x{} células de {}x{} px, origem ({}, {}), {} monitor(es)", layout_.cols, layout_.rows,
              layout_.cellW, layout_.cellH, layout_.originPx.x, layout_.originPx.y, layout_.monitorCells.size());
    return S_OK;
}

void CellCanvas::writeText(ID3D11DeviceContext* ctx, const GlyphAtlas& atlas, int col, int row,
                           std::string_view text, Rgba8 fg, Rgba8 bg) {
    if (!valid() || row < 0 || row >= layout_.rows || col < 0 || col >= layout_.cols) return;
    std::vector<CellData> line;
    for (char32_t cp : decodeUtf8(text)) {
        if (col + static_cast<int>(line.size()) >= layout_.cols) break;
        if (cp == U'\t') cp = U' ';
        line.push_back({atlas.textSlot(cp), fg.packed(), bg.packed()}); // sem glifo -> '?'
    }
    if (line.empty()) return;
    // UpdateSubresource num buffer: a "caixa" é em bytes.
    const UINT start = static_cast<UINT>((row * layout_.cols + col) * sizeof(CellData));
    const D3D11_BOX box{start, 0, 0, start + static_cast<UINT>(line.size() * sizeof(CellData)), 1, 1};
    ctx->UpdateSubresource(cells_.Get(), 0, &box, line.data(), 0, 0);
}

void CellCanvas::clear(ID3D11DeviceContext* ctx) {
    if (!valid()) return;
    const UINT zeros[4] = {0, 0, 0, 0};
    ctx->ClearUnorderedAccessViewUint(cellsUav_.Get(), zeros);
}

// ---------------------------------------------------------------------------

void GridRenderer::setLayout(int widthPx, int heightPx, const GlyphAtlas& atlas) {
    width_ = widthPx;
    height_ = heightPx;
    layout_ = computeGrid(widthPx, heightPx, atlas.cellW(), atlas.cellH());
}

HRESULT GridRenderer::draw(ID3D11Device* device, ID3D11DeviceContext* ctx, const GridPipeline& pipeline,
                           ID3D11RenderTargetView* rtv, const GlyphAtlas& atlas, const CellCanvas& canvas,
                           const Rect& monitorRectPx, Rgba8 background, const PostFx& post, float time) {
    if (!constants_) {
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = sizeof(GridConstants);
        cb.Usage = D3D11_USAGE_DEFAULT;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (HRESULT hr = device->CreateBuffer(&cb, nullptr, &constants_); FAILED(hr)) return hr;
    }
    const CanvasLayout& c = canvas.layout();
    GridConstants k{};
    k.gridW = layout_.cols;
    k.gridH = layout_.rows;
    k.cellW = layout_.cellW;
    k.cellH = layout_.cellH;
    k.originX = layout_.originX;
    k.originY = layout_.originY;
    k.atlasCols = atlas.columns();
    k.monitorToCanvasX = static_cast<float>(monitorRectPx.left - c.originPx.x);
    k.monitorToCanvasY = static_cast<float>(monitorRectPx.top - c.originPx.y);
    k.canvasCellW = static_cast<float>(c.cellW);
    k.canvasCellH = static_cast<float>(c.cellH);
    k.canvasW = c.cols;
    k.canvasH = c.rows;
    k.slotCount = atlas.slotCount();
    toFloat4(background, k.background);
    k.post0[0] = post.glow;
    k.post0[1] = post.scanlines;
    k.post0[2] = post.curvature;
    k.post0[3] = post.vignette;
    k.post1[0] = post.aberration;
    k.post1[1] = post.grain;
    k.post1[2] = time;
    k.post1[3] = static_cast<float>(layout_.cellH) / 16.0f; // o tamanho dos efeitos acompanha a grelha
    k.flags = postFxActive(post) ? 1u : 0u;
    ctx->UpdateSubresource(constants_.Get(), 0, nullptr, &k, 0, 0);

    const D3D11_VIEWPORT vp{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(pipeline.vs(), nullptr, 0);
    ctx->PSSetShader(pipeline.ps(), nullptr, 0);
    ID3D11Buffer* cbs[] = {constants_.Get()};
    ctx->PSSetConstantBuffers(0, 1, cbs);
    ID3D11ShaderResourceView* srvs[] = {canvas.cellsSrv(), atlas.srv(), atlas.inkSrv()};
    ctx->PSSetShaderResources(0, 3, srvs);
    ctx->Draw(3, 0); // o triângulo de ecrã inteiro

    // Desligar o cell buffer do pixel shader: no próximo frame o compute shader escreve nele.
    ID3D11ShaderResourceView* none[2] = {};
    ctx->PSSetShaderResources(0, 2, none);
    return S_OK;
}

} // namespace ap::render
