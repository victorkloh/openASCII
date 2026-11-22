#include "inputs/SourceMedia.h"

#include "util/Log.h"
#include "util/Win32.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace ap::inputs {

using Microsoft::WRL::ComPtr;

namespace {

bool hasExt(const std::wstring& path, std::initializer_list<const wchar_t*> exts) {
    const std::wstring e = std::filesystem::path(path).extension().wstring();
    for (const wchar_t* x : exts)
        if (_wcsicmp(e.c_str(), x) == 0) return true;
    return false;
}

UINT metaUint(IWICMetadataQueryReader* q, const wchar_t* name, UINT fallback) {
    PROPVARIANT v;
    PropVariantInit(&v);
    UINT out = fallback;
    if (q && SUCCEEDED(q->GetMetadataByName(name, &v))) {
        if (v.vt == VT_UI2) out = v.uiVal;
        else if (v.vt == VT_UI1) out = v.bVal;
        else if (v.vt == VT_UI4) out = v.ulVal;
    }
    PropVariantClear(&v);
    return out;
}

// Media Foundation tem de ser inicializado uma vez por processo.
bool ensureMediaFoundation() {
    static const bool ok = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    return ok;
}

} // namespace

bool SourceMedia::open(const std::wstring& spec, IWICImagingFactory* wic) {
    close();
    spec_ = spec;
    if (spec.empty()) return false;
    bool ok = false;
    if (_wcsicmp(spec.c_str(), L"webcam") == 0 || hasExt(spec, {L".mp4", L".mov", L".m4v", L".wmv", L".avi", L".mkv", L".webm"})) {
        ok = openMedia(spec);
    } else {
        ok = openImage(spec, wic);
    }
    if (!ok) {
        log::warn(L"Fonte de imagem \"{}\" não abriu", spec);
        kind_ = Kind::None;
    }
    return ok;
}

void SourceMedia::close() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    stop_ = false;
    kind_ = Kind::None;
    frames_.clear();
    delays_.clear();
    totalDelay_ = 0;
    uploadedFrame_ = -1;
    latest_.clear();
    fresh_ = false;
    width_ = height_ = 0;
    texture_.Reset();
    srv_.Reset();
    texW_ = texH_ = 0;
}

bool SourceMedia::openImage(const std::wstring& path, IWICImagingFactory* wic) {
    if (!wic) return false;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                              &decoder))) {
        return false;
    }
    UINT count = 0;
    decoder->GetFrameCount(&count);
    const bool gif = count > 1;

    // Tamanho lógico (num GIF, o "ecrã" do GIF; nas outras imagens, o do frame).
    UINT W = 0, H = 0;
    ComPtr<IWICMetadataQueryReader> globalMeta;
    if (gif && SUCCEEDED(decoder->GetMetadataQueryReader(&globalMeta))) {
        W = metaUint(globalMeta.Get(), L"/logscrdesc/Width", 0);
        H = metaUint(globalMeta.Get(), L"/logscrdesc/Height", 0);
    }
    std::vector<uint8_t> canvas;
    for (UINT i = 0; i < std::min<UINT>(count, 600); ++i) {
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> conv;
        if (FAILED(decoder->GetFrame(i, &frame)) || FAILED(wic->CreateFormatConverter(&conv)) ||
            FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                    WICBitmapPaletteTypeCustom))) {
            break;
        }
        UINT fw = 0, fh = 0;
        conv->GetSize(&fw, &fh);
        if (W == 0 || H == 0) {
            W = fw;
            H = fh;
        }
        // Limite de tamanho: o wallpaper é feito de células, não precisa de 8K.
        if (W > 4096 || H > 4096) return false;
        std::vector<uint8_t> pixels(static_cast<size_t>(fw) * fh * 4);
        if (FAILED(conv->CopyPixels(nullptr, fw * 4, static_cast<UINT>(pixels.size()), pixels.data()))) break;
        if (!gif) {
            frames_.push_back(std::move(pixels));
            delays_.push_back(1.0);
            break;
        }
        // GIF: cada frame pode cobrir só uma parte; compor por cima do anterior.
        ComPtr<IWICMetadataQueryReader> meta;
        frame->GetMetadataQueryReader(&meta);
        const UINT left = metaUint(meta.Get(), L"/imgdesc/Left", 0), top = metaUint(meta.Get(), L"/imgdesc/Top", 0);
        const UINT delay = metaUint(meta.Get(), L"/grctlext/Delay", 10);       // centésimos de segundo
        const UINT disposal = metaUint(meta.Get(), L"/grctlext/Disposal", 0);
        if (canvas.empty()) canvas.assign(static_cast<size_t>(W) * H * 4, 0);
        std::vector<uint8_t> before = canvas;
        for (UINT y = 0; y < fh && top + y < H; ++y) {
            for (UINT x = 0; x < fw && left + x < W; ++x) {
                const uint8_t* s = &pixels[(static_cast<size_t>(y) * fw + x) * 4];
                if (s[3] < 128) continue; // transparente: fica o que estava
                std::memcpy(&canvas[(static_cast<size_t>(top + y) * W + left + x) * 4], s, 4);
            }
        }
        frames_.push_back(canvas);
        delays_.push_back(std::max(0.02, delay / 100.0));
        if (disposal == 2) { // limpar a zona do frame para o fundo
            for (UINT y = 0; y < fh && top + y < H; ++y)
                std::memset(&canvas[(static_cast<size_t>(top + y) * W + left) * 4], 0, std::min(fw, W - left) * 4);
        } else if (disposal == 3) { // voltar ao que estava antes deste frame
            canvas = std::move(before);
        }
    }
    if (frames_.empty()) return false;
    for (double d : delays_) totalDelay_ += d;
    width_ = W;
    height_ = H;
    kind_ = gif ? Kind::Gif : Kind::Image;
    log::info(L"Fonte de imagem: {} ({}x{}{})", path, W, H, gif ? std::format(L", GIF com {} frames", frames_.size()) : L"");
    return true;
}

bool SourceMedia::openMedia(const std::wstring& pathOrWebcam) {
    if (!ensureMediaFoundation()) return false;
    kind_ = _wcsicmp(pathOrWebcam.c_str(), L"webcam") == 0 ? Kind::Webcam : Kind::Video;
    thread_ = std::thread(&SourceMedia::mediaThread, this, pathOrWebcam);
    return true;
}

void SourceMedia::mediaThread(std::wstring spec) {
    // O thread tem o seu próprio COM (MTA): o Media Foundation trabalha assim.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        ComPtr<IMFAttributes> attr;
        MFCreateAttributes(&attr, 3);
        // Processamento de vídeo avançado: converte para RGB32 e redimensiona por nós.
        attr->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        ComPtr<IMFSourceReader> reader;
        HRESULT hr = E_FAIL;
        if (kind_ == Kind::Webcam) {
            ComPtr<IMFAttributes> devAttr;
            MFCreateAttributes(&devAttr, 1);
            devAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
            IMFActivate** devices = nullptr;
            UINT32 n = 0;
            if (SUCCEEDED(MFEnumDeviceSources(devAttr.Get(), &devices, &n)) && n > 0) {
                ComPtr<IMFMediaSource> source;
                if (SUCCEEDED(devices[0]->ActivateObject(IID_PPV_ARGS(&source)))) {
                    hr = MFCreateSourceReaderFromMediaSource(source.Get(), attr.Get(), &reader);
                }
            }
            for (UINT32 i = 0; i < n; ++i) devices[i]->Release();
            CoTaskMemFree(devices);
        } else {
            hr = MFCreateSourceReaderFromURL(spec.c_str(), attr.Get(), &reader);
        }
        if (FAILED(hr) || !reader) {
            log::warn(L"Media Foundation não abriu \"{}\": {}", spec, win::hrToString(hr));
        } else {
            reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
            reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
            // Pedir RGB32 num tamanho modesto (até 640 de largura): chega para células de texto.
            ComPtr<IMFMediaType> native;
            UINT32 nw = 640, nh = 360;
            if (SUCCEEDED(reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &native))) {
                MFGetAttributeSize(native.Get(), MF_MT_FRAME_SIZE, &nw, &nh);
            }
            const UINT32 tw = std::min<UINT32>(nw, 640);
            const UINT32 th = std::max<UINT32>(1, static_cast<UINT32>(static_cast<double>(nh) * tw / std::max<UINT32>(nw, 1)) & ~1u);
            ComPtr<IMFMediaType> type;
            MFCreateMediaType(&type);
            type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, tw, th);
            hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type.Get());
            if (FAILED(hr)) { // sem redimensionar
                type->DeleteItem(MF_MT_FRAME_SIZE);
                hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type.Get());
            }
            UINT32 w = 0, h = 0;
            ComPtr<IMFMediaType> current;
            if (SUCCEEDED(hr) && SUCCEEDED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &current))) {
                MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &w, &h);
            }
            if (FAILED(hr) || w == 0 || h == 0) {
                log::warn(L"\"{}\": formato de vídeo não suportado ({})", spec, win::hrToString(hr));
            } else {
                log::info(L"Fonte de {}: {} ({}x{})", kind_ == Kind::Webcam ? L"webcam" : L"vídeo", spec, w, h);
                const auto start = std::chrono::steady_clock::now();
                LONGLONG firstTs = -1;
                auto playStart = start;
                while (!stop_) {
                    if (paused_) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        playStart = std::chrono::steady_clock::now();
                        firstTs = -1;
                        continue;
                    }
                    DWORD flags = 0;
                    LONGLONG ts = 0;
                    ComPtr<IMFSample> sample;
                    hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags,
                                            &ts, &sample);
                    if (FAILED(hr)) break;
                    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                        if (kind_ != Kind::Video) break;
                        PROPVARIANT pos;
                        InitPropVariantFromInt64(0, &pos); // voltar ao início (vídeo em ciclo)
                        reader->SetCurrentPosition(GUID_NULL, pos);
                        PropVariantClear(&pos);
                        firstTs = -1;
                        playStart = std::chrono::steady_clock::now();
                        continue;
                    }
                    if (!sample) continue;
                    // Vídeo: respeitar os tempos do ficheiro (a webcam já chega ao seu ritmo).
                    if (kind_ == Kind::Video) {
                        if (firstTs < 0) firstTs = ts;
                        const auto due = playStart + std::chrono::microseconds((ts - firstTs) / 10);
                        while (!stop_ && std::chrono::steady_clock::now() < due) {
                            std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(
                                due - std::chrono::steady_clock::now(), std::chrono::milliseconds(20)));
                        }
                    }
                    ComPtr<IMFMediaBuffer> buffer;
                    if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) continue;
                    BYTE* data = nullptr;
                    DWORD len = 0;
                    if (FAILED(buffer->Lock(&data, nullptr, &len))) continue;
                    const size_t rowBytes = static_cast<size_t>(w) * 4;
                    if (len >= rowBytes * h) {
                        std::lock_guard lock(mutex_);
                        latest_.resize(rowBytes * h);
                        // RGB32 do MF vem com a linha de cima primeiro quando o stride é positivo
                        // (o normal com o processador de vídeo). Copiar e forçar alpha opaco.
                        std::memcpy(latest_.data(), data, rowBytes * h);
                        for (size_t i = 3; i < latest_.size(); i += 4) latest_[i] = 255;
                        latestW_ = w;
                        latestH_ = h;
                        fresh_ = true;
                    }
                    buffer->Unlock();
                }
            }
        }
    }
    CoUninitialize();
}

void SourceMedia::releaseDeviceObjects() {
    texture_.Reset();
    srv_.Reset();
    texW_ = texH_ = 0;
    uploadedFrame_ = -1;
    std::lock_guard lock(mutex_);
    if (!latest_.empty()) fresh_ = true;
}

void SourceMedia::update(ID3D11Device* device, ID3D11DeviceContext* ctx, double timeSec) {
    const uint8_t* pixels = nullptr;
    UINT w = 0, h = 0;
    std::vector<uint8_t> copy;
    if (kind_ == Kind::Image || kind_ == Kind::Gif) {
        int index = 0;
        if (kind_ == Kind::Gif && totalDelay_ > 0) {
            double t = std::fmod(timeSec, totalDelay_);
            while (index < static_cast<int>(delays_.size()) - 1 && t >= delays_[static_cast<size_t>(index)]) {
                t -= delays_[static_cast<size_t>(index)];
                ++index;
            }
        }
        if (index == uploadedFrame_ && srv_) return;
        uploadedFrame_ = index;
        pixels = frames_[static_cast<size_t>(index)].data();
        w = width_;
        h = height_;
    } else if (kind_ == Kind::Video || kind_ == Kind::Webcam) {
        std::lock_guard lock(mutex_);
        if (!fresh_) return;
        copy = latest_;
        w = latestW_;
        h = latestH_;
        fresh_ = false;
        pixels = copy.data();
        width_ = w;
        height_ = h;
    } else {
        return;
    }
    if (!pixels || w == 0 || h == 0) return;

    if (!texture_ || texW_ != w || texH_ != h) {
        texture_.Reset();
        srv_.Reset();
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(&td, nullptr, &texture_)) ||
            FAILED(device->CreateShaderResourceView(texture_.Get(), nullptr, &srv_))) {
            texture_.Reset();
            srv_.Reset();
            return;
        }
        texW_ = w;
        texH_ = h;
    }
    ctx->UpdateSubresource(texture_.Get(), 0, nullptr, pixels, w * 4, 0);
}

} // namespace ap::inputs
