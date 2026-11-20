#pragma once
// Uma pergunta simples ao utilizador, no estilo da app (em vez da caixa de mensagem do
// Windows): título, texto, dois botões e "lembrar a escolha". Não bloqueia o loop: a
// resposta chega por um callback.

#include <functional>
#include <string>

#include "ui/UiWindow.h"

namespace ap::ui {

class ConfirmDialog final : public UiWindow {
public:
    using Callback = std::function<void(bool accepted, bool remember)>;

    ConfirmDialog(Gfx& gfx, AppControl& app) : UiWindow(gfx, app) {}

    bool create(HINSTANCE instance);
    void ask(std::wstring title, std::wstring message, std::wstring acceptLabel, std::wstring declineLabel,
             Rgba8 accent, Callback done);
    HRESULT renderToPng(const std::wstring& path);

private:
    void build(Ui& ui, float w, float h) override;
    bool handleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override;
    void finish(bool accepted);

    std::wstring title_, message_, accept_, decline_;
    bool remember_ = false;
    Callback done_;
};

} // namespace ap::ui
