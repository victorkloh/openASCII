#pragma once
// Mede quanto tempo a GPU demora num frame, com queries de timestamp do D3D11.
// As respostas chegam alguns frames depois; lemos sem bloquear (nunca esperamos pela GPU).

#include <d3d11.h>
#include <wrl/client.h>

#include <vector>

namespace ap::render {

class GpuTimer {
public:
    void begin(ID3D11Device* device, ID3D11DeviceContext* ctx);
    void end(ID3D11DeviceContext* ctx);
    // Junta a `out` os tempos (ms) dos frames que a GPU já acabou.
    void collect(ID3D11DeviceContext* ctx, std::vector<double>& out);
    void reset();

private:
    struct Frame {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint, start, stop;
        bool pending = false;
    };
    static constexpr int kFrames = 8;
    Frame frames_[kFrames];
    int index_ = 0;
    bool open_ = false;
};

} // namespace ap::render
