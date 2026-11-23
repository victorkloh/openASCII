#pragma once
// A imagem de entrada de um wallpaper (render.source): uma foto, um GIF animado, um vídeo ou
// a webcam, sempre entregue ao shader como uma textura (Texture2D, BGRA).
//
// - Imagens e GIFs: descodificados uma vez com o WIC (os frames do GIF ficam em memória).
// - Vídeo e webcam: Media Foundation num thread próprio, que guarda só o frame mais recente;
//   o thread de UI apenas copia esse frame para a GPU quando muda. Nada disto bloqueia o loop.

#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ap::inputs {

class SourceMedia {
public:
    SourceMedia() = default;
    ~SourceMedia() { close(); }
    SourceMedia(const SourceMedia&) = delete;
    SourceMedia& operator=(const SourceMedia&) = delete;

    // `spec`: caminho de imagem/GIF/vídeo, ou "webcam". Devolve false (e regista porquê) se falhar.
    bool open(const std::wstring& spec, IWICImagingFactory* wic);
    void close();
    const std::wstring& spec() const { return spec_; }

    // Envia para a GPU o frame atual, se mudou. timeSec anima os GIFs.
    void update(ID3D11Device* device, ID3D11DeviceContext* ctx, double timeSec);
    void releaseDeviceObjects(); // device lost: recria a textura no próximo update
    void setPaused(bool paused) { paused_ = paused; }

    ID3D11ShaderResourceView* srv() const { return srv_.Get(); }
    UINT width() const { return width_; }
    UINT height() const { return height_; }
    bool isWebcam() const { return kind_ == Kind::Webcam; }

private:
    enum class Kind { None, Image, Gif, Video, Webcam };
    enum class SessionEnd { Done, Failed, Paused }; // como acabou uma sessão do Media Foundation
    bool openImage(const std::wstring& path, IWICImagingFactory* wic);
    bool openMedia(const std::wstring& pathOrWebcam);
    void mediaThread(std::wstring pathOrWebcam);
    // Abre o vídeo/webcam e lê frames até parar. A webcam é desligada ao sair (pausa longa,
    // falha); o thread decide se volta a abrir.
    SessionEnd runMediaSession(const std::wstring& spec, bool& warned);

    Kind kind_ = Kind::None;
    std::wstring spec_;
    UINT width_ = 0, height_ = 0;

    // Imagem / GIF (BGRA, linhas contíguas).
    std::vector<std::vector<uint8_t>> frames_;
    std::vector<double> delays_; // segundos por frame
    double totalDelay_ = 0.0;
    int uploadedFrame_ = -1;

    // Vídeo / webcam.
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> paused_{false};
    std::mutex mutex_;
    std::vector<uint8_t> latest_;
    UINT latestW_ = 0, latestH_ = 0;
    bool fresh_ = false;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv_;
    UINT texW_ = 0, texH_ = 0;
};

} // namespace ap::inputs
