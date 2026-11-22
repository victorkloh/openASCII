#pragma once
// Recursos partilhados da interface: fábricas Direct2D/DirectWrite/WIC, formatos de texto
// em cache e descodificação de imagens (as previews dos wallpapers).

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

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
    // A descodificação corre num thread à parte: enquanto não acaba devolve nullptr e
    // imagePending() diz que vale a pena pedir outra vez (wait = esperar, para capturas).
    IWICBitmapSource* image(const std::filesystem::path& path, bool wait = false);
    bool imagePending(const std::filesystem::path& path) const { return pending_.contains(path.wstring()); }
    bool imagesPending() const { return !pending_.empty(); }

    const std::wstring& iconFont() const { return iconFont_; }

private:
    ComPtr<ID2D1Factory> d2d_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<IWICImagingFactory> wic_;
    ComPtr<IDWriteInlineObject> ellipsis_;
    std::unordered_map<std::wstring, ComPtr<IDWriteTextFormat>> formats_;
    std::unordered_map<std::wstring, ComPtr<IWICBitmapSource>> images_;
    struct DecodedImage {
        UINT width = 0, height = 0;
        std::vector<uint8_t> pixels; // 32bpp PBGRA
    };
    static DecodedImage decodeFile(std::filesystem::path path);
    std::unordered_map<std::wstring, std::future<DecodedImage>> pending_;
    std::wstring uiFont_ = L"Segoe UI";
    std::wstring monoFont_ = L"Consolas";
    std::wstring iconFont_ = L"Segoe MDL2 Assets";
};

} // namespace ap::ui
