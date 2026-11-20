#include "render/GpuTimer.h"

namespace ap::render {

void GpuTimer::begin(ID3D11Device* device, ID3D11DeviceContext* ctx) {
    Frame& f = frames_[index_];
    open_ = false;
    if (f.pending) return; // a GPU ainda não respondeu a este: saltar a medição
    if (!f.disjoint) {
        D3D11_QUERY_DESC d{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        D3D11_QUERY_DESC t{D3D11_QUERY_TIMESTAMP, 0};
        if (FAILED(device->CreateQuery(&d, &f.disjoint)) || FAILED(device->CreateQuery(&t, &f.start)) ||
            FAILED(device->CreateQuery(&t, &f.stop))) {
            return;
        }
    }
    ctx->Begin(f.disjoint.Get());
    ctx->End(f.start.Get());
    open_ = true;
}

void GpuTimer::end(ID3D11DeviceContext* ctx) {
    if (!open_) return;
    Frame& f = frames_[index_];
    ctx->End(f.stop.Get());
    ctx->End(f.disjoint.Get());
    f.pending = true;
    open_ = false;
    index_ = (index_ + 1) % kFrames;
}

void GpuTimer::collect(ID3D11DeviceContext* ctx, std::vector<double>& out) {
    for (Frame& f : frames_) {
        if (!f.pending) continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        if (ctx->GetData(f.disjoint.Get(), &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        UINT64 a = 0, b = 0;
        if (ctx->GetData(f.start.Get(), &a, sizeof(a), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            ctx->GetData(f.stop.Get(), &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
            continue;
        }
        f.pending = false;
        if (!dj.Disjoint && dj.Frequency > 0 && b >= a) {
            out.push_back(static_cast<double>(b - a) * 1000.0 / static_cast<double>(dj.Frequency));
        }
    }
}

void GpuTimer::reset() {
    for (Frame& f : frames_) f = Frame{};
    index_ = 0;
    open_ = false;
}

} // namespace ap::render
