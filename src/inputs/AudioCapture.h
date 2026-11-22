#pragma once
// O som que está a tocar no PC (WASAPI em modo loopback: a mistura que vai para as colunas),
// reduzido a 64 bandas de frequência + grave/médios/agudos + deteção de batida.
// Corre num thread de trabalho; o thread de UI só lê os números mais recentes.
// Nada é gravado nem guardado: só a energia por frequência, frame a frame.

#include <array>
#include <atomic>
#include <mutex>
#include <thread>

namespace ap::inputs {

struct AudioLevels {
    std::array<float, 64> spectrum{}; // 0..1, graves à esquerda (escala logarítmica)
    float bass = 0, mid = 0, treble = 0;
    float beat = 0; // 1 numa batida, decai para 0
};

class AudioCapture {
public:
    ~AudioCapture() { stop(); }
    void start();
    void stop();
    bool running() const { return thread_.joinable(); }
    AudioLevels levels();

private:
    void run();

    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    AudioLevels levels_;
};

} // namespace ap::inputs
