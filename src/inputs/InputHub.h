#pragma once
// Liga as entradas opcionais dos wallpapers ([inputs] no wallpaper.toml) ao motor:
//   windows / icons  as janelas abertas e os ícones do ambiente de trabalho (obstáculos)
//   audio            o som que está a tocar (espectro de 64 bandas, batida)
//   source           uma imagem, GIF, vídeo, o wallpaper do Windows ou a webcam (textura)
//   system           uso de CPU e RAM
//   weather          o tempo real na cidade escolhida nas Definições
//   text             uma frase, um ficheiro, o código de uma pasta, ou uma linha de cada vez
// Só arranca o que o wallpaper ativo pede: o resto não custa nada (nem threads, nem rede).

#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/Canvas.h"
#include "core/Manifest.h"
#include "inputs/AudioCapture.h"
#include "inputs/LiveData.h"
#include "inputs/SourceMedia.h"
#include "wallpaper/ShaderBackend.h"

namespace ap::render {
class GlyphAtlas;
}

namespace ap::inputs {

struct InputSettings {
    std::wstring weatherCity;      // vazio = sem meteorologia (a app nunca contacta a internet)
    std::wstring windowsWallpaper; // o wallpaper original do Windows (source = "wallpaper")
    bool webcamAllowed = false;    // o utilizador autorizou a webcam
};

// Os ícones do ambiente de trabalho (posição e tamanho), lidos ao Explorer num thread
// à parte de 2 em 2 segundos (IShellWindows -> IFolderView, a via documentada).
class DesktopIcons {
public:
    ~DesktopIcons() { stop(); }
    void start();
    void stop();
    std::vector<RECT> rects(); // px do ecrã virtual

private:
    void run();
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::vector<RECT> rects_;
};

class InputHub {
public:
    InputHub() = default;
    ~InputHub() { stop(); }
    InputHub(const InputHub&) = delete;
    InputHub& operator=(const InputHub&) = delete;

    // Ao ativar um wallpaper ou mudar as definições: arranca/para o que o manifest pede.
    void configure(const Manifest& m, const std::filesystem::path& wallpaperDir, const InputSettings& s,
                   IWICImagingFactory* wic);
    void stop();
    // O wallpaper quer a webcam e o utilizador ainda não disse que sim.
    bool webcamConsentNeeded() const { return webcamConsentNeeded_; }

    // Uma vez por frame, antes dos dispatches. `atlas` = o do monitor de referência (slots do texto).
    void update(ID3D11Device* device, ID3D11DeviceContext* ctx, double timeSec, const render::GlyphAtlas& atlas);
    // Por canvas: preenche as entradas nas constantes do frame e devolve os recursos a ligar.
    wallpaper::ShaderInputs bind(ID3D11Device* device, ID3D11DeviceContext* ctx, const void* canvasKey,
                                 const CanvasLayout& layout, wallpaper::FrameConstants& f);
    void releaseDeviceObjects();
    void setPaused(bool paused);
    // --windowed: as janelas por trás da janela de desenvolvimento não tapam o "desktop".
    void setStopWindow(HWND hwnd) { stopAt_ = hwnd; }

private:
    struct ObstacleBuffer {
        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        UINT capacity = 0;
        uint32_t count = 0, windows = 0;
    };
    void loadText(const std::string& spec, const std::filesystem::path& dir, float interval);
    void uploadText(ID3D11Device* device, ID3D11DeviceContext* ctx, const render::GlyphAtlas& atlas);
    void refreshWindows();

    Manifest manifest_;
    bool configured_ = false;

    // Janelas e ícones (px do ecrã virtual), lidos no máximo a cada 50 ms.
    std::vector<RECT> windows_, icons_;
    HWND stopAt_ = nullptr;
    double windowsTime_ = -1.0;
    std::unordered_map<const void*, ObstacleBuffer> obstacles_;
    DesktopIcons desktopIcons_;

    // Texto.
    std::u32string text_;                // o que está no buffer agora
    std::vector<std::u32string> lines_;  // "lines:"
    float lineInterval_ = 15.0f;
    int lineIndex_ = -1;
    double lineChanged_ = 0.0;
    bool textDirty_ = false;
    std::wstring textAtlasKey_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> textBuffer_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> textSrv_;
    UINT textCapacity_ = 0;
    float textInfo_[4] = {};

    // Imagem/vídeo.
    SourceMedia source_;
    std::wstring sourceSpec_;
    bool webcamConsentNeeded_ = false;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;

    // Áudio, sistema, tempo.
    AudioCapture audio_;
    AudioLevels levels_;
    SystemStats system_;
    double systemTime_ = -10.0;
    Weather weather_;
    WeatherNow weatherNow_;
    double weatherTime_ = -10.0;
};

} // namespace ap::inputs
