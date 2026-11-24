#pragma once
// O launcher: a janela principal da app. Barra lateral com os wallpapers; à direita,
// personalizar o selecionado (variantes, grelha, cores, caracteres, parâmetros), criar um
// wallpaper novo e as definições gerais.

#include <string>
#include <vector>

#include "ui/UiWindow.h"
#include "ui/WallpaperEditor.h"

namespace ap::ui {

class Launcher final : public UiWindow {
public:
    enum class Page { Wallpaper = 0, New = 1, Settings = 2, Welcome = 3 };

    Launcher(Gfx& gfx, AppControl& app) : UiWindow(gfx, app) {}

    bool create(HINSTANCE instance);
    void show();
    void refresh(); // o wallpaper ativo ou o catálogo mudaram
    HRESULT renderToPng(const std::wstring& path, int page, int heightDip = 720, int widthDip = 1080);

private:
    void build(Ui& ui, float w, float h) override;
    bool handleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override;

    void buildSidebar(Ui& ui, float h);
    float buildWallpaperPage(Ui& ui, float x, float y, float w);
    float buildNewPage(Ui& ui, float x, float y, float w);
    float buildSettingsPage(Ui& ui, float x, float y, float w);
    void buildWelcome(Ui& ui, float w, float h); // primeiros passos (janela inteira, sem barra lateral)
    void finishWelcome();
    float section(Ui& ui, float x, float y, float w, std::wstring_view title, std::wstring_view hint = {});
    void select(const std::wstring& base);
    void selectVariant(const std::filesystem::path& path);
    std::wstring activeBase() const;

    Page page_ = Page::Wallpaper;
    int welcomeStep_ = 0; // 0 boas-vindas, 1 desempenho, 2 arranque, 3 pronto
    std::vector<WallpaperInfo> items_;
    std::wstring selectedBase_;
    WallpaperEditor editor_;
    std::vector<std::wstring> fonts_;
    int fontIndex_ = -1;
    int selectedSwatch_ = -1; // -1 nenhum, 0 fundo, 1.. paleta
    std::wstring charsetEdit_;
    std::wstring themeName_;
    std::wstring cityEdit_;   // cidade do tempo real (Definições), em edição
    std::wstring search_;     // pesquisa na lista de wallpapers
    bool cityLoaded_ = false;
    static constexpr UINT kMsgPickSource = WM_APP + 40; // abrir o diálogo de imagem/vídeo (fora da pintura)
    std::wstring message_;    // feedback curto ("Tema guardado")
    std::wstring newName_;
    int templateIndex_ = -1;  // -1 = em branco
    std::wstring newError_;
    float contentHeight_ = 0.0f;
};

} // namespace ap::ui
