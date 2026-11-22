#pragma once
// Dados ao vivo para os wallpapers:
// - SystemStats: uso de CPU e RAM (lido 1x/s, sem custo quando não é usado).
// - Weather: o tempo real numa cidade escolhida pelo utilizador, via Open-Meteo (serviço
//   gratuito, sem chave). Só é contactado se o utilizador escrever uma cidade nas Definições;
//   só a cidade e as coordenadas saem do PC. Atualiza a cada 15 minutos num thread.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace ap::inputs {

class SystemStats {
public:
    void update(); // chamar ~1x por segundo
    float cpu() const { return cpu_; }
    float ram() const { return ram_; }

private:
    uint64_t lastIdle_ = 0, lastKernel_ = 0, lastUser_ = 0;
    float cpu_ = 0, ram_ = 0;
};

struct WeatherNow {
    bool valid = false;
    float rainMm = 0;      // precipitação na última hora (mm)
    float cloud = 0;       // 0..1
    float windMs = 0;      // m/s
    float temperature = 0; // °C
    bool isDay = true;
    int code = 0;          // código WMO (0 limpo, 61 chuva, 71 neve, 95 trovoada...)
    float sunElevation = 0; // graus (calculado localmente a partir das coordenadas)
    double latitude = 0, longitude = 0;
};

class Weather {
public:
    ~Weather() { stop(); }
    // Começa (ou recomeça, se a cidade mudou) a atualizar em segundo plano.
    void start(const std::wstring& city);
    void stop();
    WeatherNow now();

private:
    void run(std::wstring city);

    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    WeatherNow data_;
    std::wstring city_;
};

// Elevação do sol (graus) para uma posição e um instante UTC (aproximação NOAA, ±0,5°).
float solarElevation(double latitude, double longitude, std::chrono::system_clock::time_point when);

} // namespace ap::inputs
