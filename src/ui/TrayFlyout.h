#pragma once
// O menu da bandeja: um painel próprio (em vez do menu de contexto do Windows) com os
// wallpapers em miniatura (grelha com scroll), as variantes do que está em uso, ajustes
// rápidos recolhidos e as ações principais. Cabe sempre na área de trabalho.

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
    float fixedHeight() const;       // tudo menos a grelha (layoutHeight e build usam a mesma conta)
    float gridContentHeight() const; // altura total da grelha de wallpapers
    std::wstring activeBase() const;
    int activeIndex() const;         // índice em bases_ do wallpaper em uso (-1 = nenhum)
    int variantCount() const;        // variantes do wallpaper em uso (1 = só o original)

    std::vector<WallpaperInfo> items_;
    std::vector<size_t> bases_;      // índices em items_ dos wallpapers (sem os temas)
    bool adjustOpen_ = false;        // "Ajustes rápidos" expandidos
    int focus_ = -1;                 // seleção do teclado na grelha (-1 = nenhuma)
    bool scrollToActive_ = false;    // ao abrir: centrar o wallpaper em uso
    bool scrollToFocus_ = false;     // depois de uma tecla: manter a seleção à vista
    WallpaperEditor editor_;
    float contentHeight_ = 420.0f;
    ULONGLONG hiddenAt_ = 0;
    RECT anchor_{};
};

} // namespace ap::ui
