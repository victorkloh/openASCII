#pragma once
// Base das janelas da interface: cria a janela e o render target Direct2D, traduz o rato e
// o teclado para o Ui, segue o DPI e o tema claro/escuro do Windows, e repinta só quando
// há input (nada de animações em ciclo: 0% de CPU com a janela parada).

#include <windows.h>

#include <string>

#include "core/Color.h"
#include "ui/AppControl.h"
#include "ui/Gfx.h"
#include "ui/Ui.h"

namespace ap::ui {

// A cor de destaque da interface vem da paleta do wallpaper ativo.
Rgba8 accentFromPalette(const std::vector<Rgba8>& palette);
bool systemUsesDarkTheme();

class UiWindow {
public:
    virtual ~UiWindow();
    UiWindow(const UiWindow&) = delete;
    UiWindow& operator=(const UiWindow&) = delete;

    HWND hwnd() const { return hwnd_; }
    bool isVisible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    void invalidate() const;

protected:
    UiWindow(Gfx& gfx, AppControl& app) : gfx_(gfx), app_(app), ui_(gfx) {}

    bool createWindow(HINSTANCE instance, const wchar_t* className, const wchar_t* title, DWORD style,
                      DWORD exStyle, int widthDip, int heightDip, UINT classStyle = 0);
    // Desenha a interface inteira (layout + widgets) num espaço de w×h DIPs.
    virtual void build(Ui& ui, float w, float h) = 0;
    virtual bool handleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
        (void)msg, (void)wp, (void)lp, (void)result;
        return false;
    }
    // Desenha para um PNG (sem janela visível): para documentação e testes visuais.
    HRESULT renderToPngImpl(const std::wstring& path, float widthDip, float heightDip, float scale);

    Tokens theme() const { return Tokens::make(systemUsesDarkTheme(), accent_); }
    float dpiScale() const;
    void applyWindowTheme(); // barra de título escura/clara, cantos, borda

    Gfx& gfx_;
    AppControl& app_;
    Ui ui_;
    Input input_;
    HWND hwnd_ = nullptr;
    Rgba8 accent_{92, 200, 240, 255};

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT onMessage(UINT msg, WPARAM wp, LPARAM lp);
    void paint();
    D2D1_POINT_2F toDip(LPARAM lp) const;

    ComPtr<ID2D1HwndRenderTarget> rt_;
    bool trackingMouse_ = false;
    bool followUp_ = false;
};

} // namespace ap::ui
