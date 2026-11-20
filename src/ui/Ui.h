#pragma once
// Um pequeno toolkit de interface em "modo imediato": a cada repintura, o código descreve a
// interface de cima a baixo ("aqui um botão; se foi clicado, faz X"). Não há árvore de
// objetos para manter sincronizada com o estado: o estado É a interface.
// Desenha com Direct2D; só repinta quando há input (0% de CPU parado).

#include <d2d1.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Color.h"
#include "ui/Gfx.h"

namespace ap::ui {

using Id = uint64_t;
Id makeId(std::wstring_view text, int index = 0);
inline Id makeId(Id parent, int index) { return parent * 1099511628211ull ^ static_cast<Id>(index + 1); }

using R = D2D1_RECT_F;
inline R rect(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }
inline float width(const R& r) { return r.right - r.left; }
inline float height(const R& r) { return r.bottom - r.top; }
inline R inset(const R& r, float dx, float dy) { return {r.left + dx, r.top + dy, r.right - dx, r.bottom - dy}; }
inline bool contains(const R& r, D2D1_POINT_2F p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

// Tokens de cor. Escolhidos para contraste: texto >= 4.5:1 sobre as superfícies.
struct Tokens {
    bool dark = true;
    D2D1_COLOR_F bg, surface, raised, hover, border, text, muted, faint, accent, onAccent, danger;
    static Tokens make(bool dark, Rgba8 accent);
};

struct Input {
    D2D1_POINT_2F mouse{-1e6f, -1e6f};
    bool down = false;     // botão esquerdo premido
    bool pressed = false;  // foi premido neste evento
    bool released = false; // foi largado neste evento
    float wheel = 0.0f;    // entalhes (+ = para cima)
    std::wstring text;     // caracteres escritos
    bool backspace = false, enter = false, escape = false;
    void clearTransient() {
        pressed = released = backspace = enter = escape = false;
        wheel = 0.0f;
        text.clear();
    }
};

enum class ButtonKind { Primary, Secondary, Ghost, Danger };

class Ui {
public:
    explicit Ui(Gfx& gfx) : gfx_(gfx) {}

    void begin(ID2D1RenderTarget* rt, Input& input, const Tokens& theme);
    void end();
    void resetDeviceResources(); // o render target mudou: largar bitmaps e pincéis

    const Tokens& theme() const { return theme_; }
    Input& input() { return *in_; }
    bool repaintRequested() const { return repaint_; }
    void requestRepaint() { repaint_ = true; }
    bool anyActive() const { return active_ != 0; }
    bool mouseInside(const R& r) const { return !blocked_ && contains(r, in_->mouse); }

    // ---- Desenho ----
    void fill(const R& r, D2D1_COLOR_F c, float radius = 0.0f);
    void stroke(const R& r, D2D1_COLOR_F c, float radius = 0.0f, float width = 1.0f);
    void line(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_COLOR_F c, float width = 1.0f);
    void text(const R& r, std::wstring_view s, D2D1_COLOR_F c, const TextStyle& style = {});
    void icon(const R& r, wchar_t glyph, D2D1_COLOR_F c, float size = 14.0f);
    void image(const R& r, const std::filesystem::path& path, float radius = 0.0f, D2D1_COLOR_F placeholder = {});
    void pushClip(const R& r);
    void popClip();
    float textWidth(std::wstring_view s, const TextStyle& style);

    // ---- Widgets (devolvem true quando o utilizador fez alguma coisa) ----
    bool button(Id id, const R& r, std::wstring_view label, ButtonKind kind = ButtonKind::Secondary,
                wchar_t iconGlyph = 0, bool enabled = true);
    bool iconButton(Id id, const R& r, wchar_t glyph, bool enabled = true);
    bool clickable(Id id, const R& r); // zona clicável sem desenho (o chamador desenha)
    bool hovered(const R& r) const { return mouseInside(r); }
    bool slider(Id id, const R& r, std::wstring_view label, float& value, float lo, float hi, float step,
                std::wstring_view valueText);
    bool toggle(Id id, const R& r, std::wstring_view label, std::wstring_view description, bool& value);
    bool chip(Id id, const R& r, std::wstring_view label, bool selected);
    bool swatch(Id id, const R& r, Rgba8 color, bool selected);
    bool textField(Id id, const R& r, std::wstring& value, std::wstring_view placeholder);
    bool dropdown(Id id, const R& r, const std::vector<std::wstring>& items, int& selected);
    bool colorPicker(Id id, const R& r, Rgba8& color); // quadrado S/V + barra de matiz + hex
    float scrollArea(Id id, const R& viewport, float contentHeight); // devolve o deslocamento
    bool sliderReleased() const { return sliderReleased_; } // um slider largado neste evento (gravar)

private:
    ID2D1Brush* brush(D2D1_COLOR_F c);
    ID2D1Bitmap* bitmap(const std::filesystem::path& path);
    void drawFocusRing(const R& r, float radius);

    Gfx& gfx_;
    ID2D1RenderTarget* rt_ = nullptr;
    Input* in_ = nullptr;
    Tokens theme_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    std::unordered_map<std::wstring, ComPtr<ID2D1Bitmap>> bitmaps_;

    Id hot_ = 0, active_ = 0, focus_ = 0;
    bool repaint_ = false;
    bool sliderReleased_ = false;
    bool blocked_ = false; // o rato está por cima de um dropdown aberto

    // Dropdown aberto (desenhado por cima de tudo em end()).
    Id openDropdown_ = 0;
    R dropdownRect_{};
    R lastOverlayRect_{};
    std::vector<std::wstring> dropdownItems_;
    int* dropdownTarget_ = nullptr;
    bool dropdownChanged_ = false;
    float dropdownScroll_ = 0.0f;

    // Estado da roda de cores (matiz guardada para não se perder com S=0 ou V=0).
    std::unordered_map<Id, float> hues_;
    std::unordered_map<Id, std::wstring> hexEdits_;
    std::unordered_map<Id, float> scroll_;
};

} // namespace ap::ui
