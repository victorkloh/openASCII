#pragma once
// O menu da bandeja: um painel próprio (em vez do menu de contexto do Windows) com os
// wallpapers em miniatura, os temas, ajustes rápidos e as ações principais.

#include <vector>

#include "ui/UiWindow.h"
#include "ui/WallpaperEditor.h"

namespace ap::ui {

class TrayFlyout final : public UiWindow {
public:
    TrayFlyout(Gfx& gfx, AppControl& app) : UiWindow(gfx, app) {}

    bool create(HINSTANCE instance);
    void toggle(const RECT& anchorPx); // abre junto ao ícone da bandeja (ou fecha)
    void refresh();                    // o wallpaper ativo ou o estado mudou
    HRESULT renderToPng(const std::wstring& path);

private:
    void build(Ui& ui, float w, float h) override;
    bool handleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override;
    void hide();
    void place(const RECT& anchorPx);
    float layoutHeight();

    std::vector<WallpaperInfo> items_;
    WallpaperEditor editor_;
    float contentHeight_ = 420.0f;
    ULONGLONG hiddenAt_ = 0;
    RECT anchor_{};
};

} // namespace ap::ui
