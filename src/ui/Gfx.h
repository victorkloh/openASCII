#pragma once
// Recursos partilhados da interface: fábricas Direct2D/DirectWrite/WIC, formatos de texto
// em cache e descodificação de imagens (as previews dos wallpapers).

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace ap::ui {

using Microsoft::WRL::ComPtr;

enum class FontKind { Ui, Mono, Icon };

struct TextStyle {
    float size = 13.0f;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    FontKind kind = FontKind::Ui;
    DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
    DWRITE_PARAGRAPH_ALIGNMENT valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    bool wrap = false; // false = uma linha, com reticências se não couber
};

class Gfx {
public:
    HRESULT init();

    ID2D1Factory* d2d() const { return d2d_.Get(); }
    IDWriteFactory* dwrite() const { return dwrite_.Get(); }
    IWICImagingFactory* wic() const { return wic_.Get(); }

    IDWriteTextFormat* format(const TextStyle& style);
    // Imagem descodificada em CPU (32bpp PBGRA), pronta a virar ID2D1Bitmap em qualquer render target.
    IWICBitmapSource* image(const std::filesystem::path& path);

    const std::wstring& iconFont() const { return iconFont_; }

private:
    ComPtr<ID2D1Factory> d2d_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<IWICImagingFactory> wic_;
    ComPtr<IDWriteInlineObject> ellipsis_;
    std::unordered_map<std::wstring, ComPtr<IDWriteTextFormat>> formats_;
    std::unordered_map<std::wstring, ComPtr<IWICBitmapSource>> images_;
    std::wstring uiFont_ = L"Segoe UI";
    std::wstring monoFont_ = L"Consolas";
    std::wstring iconFont_ = L"Segoe MDL2 Assets";
};

} // namespace ap::ui
