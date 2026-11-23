#include "inputs/AudioCapture.h"

#include "util/Log.h"
#include "util/Win32.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace ap::inputs {

using Microsoft::WRL::ComPtr;

namespace {

constexpr int kFftSize = 2048;

// FFT radix-2 in-place (Cooley-Tukey). n tem de ser potência de 2.
void fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * 3.14159265f / static_cast<float>(len);
        const std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

} // namespace

void AudioCapture::start() {
    if (running()) return;
    stop_ = false;
    thread_ = std::thread(&AudioCapture::run, this);
}

void AudioCapture::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    std::lock_guard lock(mutex_);
    levels_ = AudioLevels{};
}

AudioLevels AudioCapture::levels() {
    std::lock_guard lock(mutex_);
    return levels_;
}

void AudioCapture::run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        bool warned = false;
        // Uma sessão acaba quando o dispositivo de saída deixa de ser válido ou deixa de ser o
        // predefinido (ex.: ligar auscultadores). Antes o espectro ficava em silêncio até
        // reiniciar a app; agora volta a abrir a saída predefinida.
        while (!stop_) {
            if (enumerator) session(enumerator.Get(), warned);
            {
                std::lock_guard lock(mutex_);
                levels_ = AudioLevels{};
            }
            for (int i = 0; i < 20 && !stop_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    CoUninitialize();
}

void AudioCapture::session(IMMDeviceEnumerator* enumerator, bool& warned) {
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    WAVEFORMATEX* format = nullptr;
    std::wstring deviceId;
    HRESULT hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (SUCCEEDED(hr)) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(device->GetId(&id))) {
            deviceId = id;
            CoTaskMemFree(id);
        }
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
    }
    if (SUCCEEDED(hr)) hr = client->GetMixFormat(&format);
    // Loopback: capturar o que vai para a saída (não o microfone).
    if (SUCCEEDED(hr)) hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10'000'000, 0, format, nullptr);
    if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&capture));
    if (SUCCEEDED(hr)) hr = client->Start();
    if (FAILED(hr)) {
        if (!warned) log::warn(L"Áudio: loopback indisponível ({})", win::hrToString(hr));
        warned = true;
        if (format) CoTaskMemFree(format);
        return;
    }
    warned = false;
    const int channels = format->nChannels;
    const float rate = static_cast<float>(format->nSamplesPerSec);
    const bool isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                         (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->wBitsPerSample == 32);
    log::info(L"Áudio: a capturar a saída ({} Hz, {} canais)", static_cast<int>(rate), channels);
    std::vector<float> ring(kFftSize, 0.0f);
    size_t writePos = 0;
    std::vector<std::complex<float>> bins(kFftSize);
    std::array<float, 64> smooth{};
    float energyAvg = 0.0f, beat = 0.0f;
    auto lastSample = std::chrono::steady_clock::now();
    auto lastDeviceCheck = lastSample;

    while (!stop_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        UINT32 packet = 0;
        bool gotData = false;
        while (SUCCEEDED(hr = capture->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
            for (UINT32 f = 0; f < frames; ++f) {
                float s = 0.0f;
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && isFloat) {
                    const float* p = reinterpret_cast<const float*>(data) + f * channels;
                    for (int c = 0; c < channels; ++c) s += p[c];
                    s /= static_cast<float>(channels);
                } else if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && format->wBitsPerSample == 16) {
                    const int16_t* p = reinterpret_cast<const int16_t*>(data) + f * channels;
                    for (int c = 0; c < channels; ++c) s += p[c] / 32768.0f;
                    s /= static_cast<float>(channels);
                }
                ring[writePos] = s;
                writePos = (writePos + 1) % kFftSize;
            }
            capture->ReleaseBuffer(frames);
            gotData = true;
        }
        if (FAILED(hr)) {
            // AUDCLNT_E_DEVICE_INVALIDATED (dispositivo removido/desativado) e afins.
            log::info(L"Áudio: a saída deixou de estar disponível ({}); a reabrir", win::hrToString(hr));
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - lastDeviceCheck > std::chrono::seconds(2)) {
            // A saída predefinida mudou mas a antiga continua válida (fica muda): reabrir.
            lastDeviceCheck = now;
            ComPtr<IMMDevice> current;
            LPWSTR id = nullptr;
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &current)) &&
                SUCCEEDED(current->GetId(&id))) {
                const bool changed = deviceId != id;
                CoTaskMemFree(id);
                if (changed) {
                    log::info(L"Áudio: a saída predefinida mudou; a reabrir");
                    break;
                }
            }
        }
        if (gotData) lastSample = now;
        // Sem pacotes = silêncio (o loopback não envia nada quando nada toca).
        const bool silent = now - lastSample > std::chrono::milliseconds(200);

        // Janela de Hann + FFT sobre os últimos kFftSize samples.
        for (int i = 0; i < kFftSize; ++i) {
            const float wv = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * i / (kFftSize - 1));
            bins[static_cast<size_t>(i)] = silent ? 0.0f : ring[(writePos + i) % kFftSize] * wv;
        }
        fft(bins);

        // 64 bandas logarítmicas entre 30 Hz e 16 kHz, em dB normalizados.
        std::array<float, 64> spec{};
        const float fMin = 30.0f, fMax = std::min(16000.0f, rate * 0.5f);
        for (int b = 0; b < 64; ++b) {
            const float f0 = fMin * std::pow(fMax / fMin, b / 64.0f);
            const float f1 = fMin * std::pow(fMax / fMin, (b + 1) / 64.0f);
            int i0 = std::max(1, static_cast<int>(f0 / rate * kFftSize));
            int i1 = std::max(i0 + 1, static_cast<int>(f1 / rate * kFftSize));
            float peak = 0.0f;
            for (int i = i0; i < i1 && i < kFftSize / 2; ++i) peak = std::max(peak, std::abs(bins[static_cast<size_t>(i)]));
            const float db = 20.0f * std::log10(peak / (kFftSize * 0.25f) + 1e-6f); // ~ -120..0
            spec[static_cast<size_t>(b)] = std::clamp((db + 70.0f) / 60.0f, 0.0f, 1.0f);
        }
        // Suavizar: sobe depressa, desce devagar (o olho gosta assim).
        for (int b = 0; b < 64; ++b) {
            float& sm = smooth[static_cast<size_t>(b)];
            const float v = spec[static_cast<size_t>(b)];
            sm = v > sm ? sm + (v - sm) * 0.6f : sm + (v - sm) * 0.12f;
        }
        auto avg = [&](int a, int z) {
            float sum = 0;
            for (int b = a; b < z; ++b) sum += smooth[static_cast<size_t>(b)];
            return sum / static_cast<float>(z - a);
        };
        const float bass = avg(0, 12), mid = avg(12, 40), treble = avg(40, 64);
        // Batida: energia de graves bem acima da média recente.
        energyAvg = energyAvg * 0.96f + bass * 0.04f;
        if (bass > energyAvg * 1.35f + 0.05f && beat < 0.3f) beat = 1.0f;
        beat *= 0.85f;

        std::lock_guard lock(mutex_);
        levels_.spectrum = smooth;
        levels_.bass = bass;
        levels_.mid = mid;
        levels_.treble = treble;
        levels_.beat = beat;
    }
    client->Stop();
    CoTaskMemFree(format);
}

} // namespace ap::inputs
