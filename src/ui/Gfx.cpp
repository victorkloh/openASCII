#include "ui/Gfx.h"

#include "util/Log.h"

#include <format>

namespace ap::ui {

namespace {
bool fontExists(IDWriteFactory* dw, const wchar_t* family) {
    ComPtr<IDWriteFontCollection> fonts;
    if (FAILED(dw->GetSystemFontCollection(&fonts))) return false;
    UINT32 index = 0;
    BOOL exists = FALSE;
    fonts->FindFamilyName(family, &index, &exists);
    return exists != FALSE;
}
} // namespace

HRESULT Gfx::init() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf());
    if (SUCCEEDED(hr)) {
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
    }
    if (SUCCEEDED(hr)) {
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_));
    }
    if (FAILED(hr)) return hr;

    // As fontes do Windows 11 quando existem; as do Windows 10 como alternativa.
    if (fontExists(dwrite_.Get(), L"Segoe UI Variable Text")) uiFont_ = L"Segoe UI Variable Text";
    if (fontExists(dwrite_.Get(), L"Cascadia Mono")) monoFont_ = L"Cascadia Mono";
    if (fontExists(dwrite_.Get(), L"Segoe Fluent Icons")) iconFont_ = L"Segoe Fluent Icons";
    return S_OK;
}

IDWriteTextFormat* Gfx::format(const TextStyle& s) {
    const std::wstring key = std::format(L"{}|{}|{}|{}|{}|{}", s.size, static_cast<int>(s.weight),
                                         static_cast<int>(s.kind), static_cast<int>(s.align),
                                         static_cast<int>(s.valign), s.wrap);
    if (auto it = formats_.find(key); it != formats_.end()) return it->second.Get();

    const std::wstring& family = s.kind == FontKind::Mono ? monoFont_ : s.kind == FontKind::Icon ? iconFont_ : uiFont_;
    ComPtr<IDWriteTextFormat> f;
    if (FAILED(dwrite_->CreateTextFormat(family.c_str(), nullptr, s.weight, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, s.size, L"pt-pt", &f))) {
        return nullptr;
    }
    f->SetTextAlignment(s.align);
    f->SetParagraphAlignment(s.valign);
    if (!s.wrap) {
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        if (!ellipsis_) dwrite_->CreateEllipsisTrimmingSign(f.Get(), &ellipsis_);
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        f->SetTrimming(&trimming, ellipsis_.Get());
    }
    formats_[key] = f;
    return f.Get();
}

Gfx::DecodedImage Gfx::decodeFile(std::filesystem::path path) {
    // Thread de trabalho: COM próprio e fábrica WIC própria; só os píxeis voltam ao thread de UI.
    DecodedImage out;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        ComPtr<IWICImagingFactory> wic;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
        if (SUCCEEDED(hr)) {
            hr = wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                &decoder);
        }
        if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
        if (SUCCEEDED(hr)) hr = wic->CreateFormatConverter(&converter);
        if (SUCCEEDED(hr)) {
            hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                       0.0, WICBitmapPaletteTypeCustom);
        }
        UINT w = 0, h = 0;
        if (SUCCEEDED(hr)) hr = converter->GetSize(&w, &h);
        if (SUCCEEDED(hr) && w > 0 && h > 0 && w <= 8192 && h <= 8192) {
            std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
            if (SUCCEEDED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data()))) {
                out.width = w;
                out.height = h;
                out.pixels = std::move(pixels);
            }
        }
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return out;
}

IWICBitmapSource* Gfx::image(const std::filesystem::path& path, bool wait) {
    const std::wstring key = path.wstring();
    if (auto it = images_.find(key); it != images_.end()) return it->second.Get();
    auto pending = pending_.find(key);
    if (pending == pending_.end()) {
        // Ler um PNG do disco e descodificá-lo chegava a 400 ms na primeira pintura do launcher,
        // no thread que também desenha o wallpaper. Agora vai para segundo plano.
        pending = pending_.emplace(key, std::async(std::launch::async, &Gfx::decodeFile, path)).first;
    }
    if (!wait && pending->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;
    DecodedImage img = pending->second.get();
    pending_.erase(pending);
    // Guardar em memória (descodificar uma vez) e lembrar também as que falham.
    ComPtr<IWICBitmap> bitmap;
    if (img.width > 0) {
        wic_->CreateBitmapFromMemory(img.width, img.height, GUID_WICPixelFormat32bppPBGRA, img.width * 4,
                                     static_cast<UINT>(img.pixels.size()), img.pixels.data(), &bitmap);
    }
    images_[key] = bitmap;
    return bitmap.Get();
}

} // namespace ap::ui
