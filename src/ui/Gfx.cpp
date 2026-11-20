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

IWICBitmapSource* Gfx::image(const std::filesystem::path& path) {
    const std::wstring key = path.wstring();
    if (auto it = images_.find(key); it != images_.end()) return it->second.Get();
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    HRESULT hr = wic_->CreateDecoderFromFilename(key.c_str(), nullptr, GENERIC_READ,
                                                 WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = wic_->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                   0.0, WICBitmapPaletteTypeCustom);
    }
    // Guardar em memória (descodificar uma vez) e lembrar também as que falham.
    ComPtr<IWICBitmap> bitmap;
    if (SUCCEEDED(hr)) hr = wic_->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnLoad, &bitmap);
    images_[key] = SUCCEEDED(hr) ? ComPtr<IWICBitmapSource>(bitmap) : nullptr;
    return images_[key].Get();
}

} // namespace ap::ui
