#include "ui/ConfirmDialog.h"

namespace ap::ui {

namespace {
constexpr float kW = 460.0f, kH = 236.0f;
}

bool ConfirmDialog::create(HINSTANCE instance) {
    return createWindow(instance, L"OpenASCIIDialog", L"OpenASCII", WS_POPUP, WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                        static_cast<int>(kW), static_cast<int>(kH), CS_DROPSHADOW);
}

void ConfirmDialog::ask(std::wstring title, std::wstring message, std::wstring acceptLabel, std::wstring declineLabel,
                        Rgba8 accent, Callback done) {
    Question q{std::move(title), std::move(message), std::move(acceptLabel), std::move(declineLabel), accent,
               std::move(done)};
    if (showing_) {
        if (q.title == title_) {
            // A mesma pergunta outra vez (ex.: o wallpaper recarregou): atualizar a que está aberta.
            message_ = std::move(q.message);
            done_ = std::move(q.done);
            invalidate();
            return;
        }
        for (auto& queued : queue_) {
            if (queued.title == q.title) {
                queued = std::move(q);
                return;
            }
        }
        queue_.push_back(std::move(q));
        return;
    }
    show(std::move(q));
}

void ConfirmDialog::show(Question q) {
    title_ = std::move(q.title);
    message_ = std::move(q.message);
    accept_ = std::move(q.accept);
    decline_ = std::move(q.decline);
    accent_ = q.accent;
    remember_ = false;
    done_ = std::move(q.done);
    showing_ = true;
    applyWindowTheme();

    // Centrado na área de trabalho do monitor principal.
    const float s = dpiScale();
    const int w = static_cast<int>(kW * s), h = static_cast<int>(kH * s);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    const RECT& wa = mi.rcWork;
    SetWindowPos(hwnd_, HWND_TOPMOST, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w,
                 h, SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd_);
    invalidate();
}

void ConfirmDialog::finish(bool accepted) {
    if (!showing_) return; // Esc e um clique no mesmo instante: responder só uma vez
    ShowWindow(hwnd_, SW_HIDE);
    showing_ = false;
    Callback cb = std::move(done_);
    done_ = nullptr;
    if (cb) cb(accepted, remember_); // pode fazer outra pergunta (fica logo no ecrã)
    if (!showing_ && !queue_.empty()) {
        Question next = std::move(queue_.front());
        queue_.pop_front();
        show(std::move(next));
    }
}

HRESULT ConfirmDialog::renderToPng(const std::wstring& path) { return renderToPngImpl(path, kW, kH, 2.0f); }

bool ConfirmDialog::handleMessage(UINT msg, WPARAM wp, LPARAM, LRESULT& result) {
    if (msg == WM_KEYDOWN && (wp == VK_ESCAPE || wp == VK_RETURN)) {
        finish(wp == VK_RETURN);
        result = 0;
        return true;
    }
    if (msg == WM_CLOSE) {
        finish(false);
        result = 0;
        return true;
    }
    return false;
}

void ConfirmDialog::build(Ui& ui, float w, float h) {
    const Tokens& t = ui.theme();
    ui.fill(rect(0, 0, w, h), t.surface);
    ui.stroke(rect(0, 0, w, h), t.border);
    const float pad = 24;
    TextStyle logo;
    logo.kind = FontKind::Mono;
    logo.size = 14.0f;
    logo.weight = DWRITE_FONT_WEIGHT_BOLD;
    ui.text(rect(pad, pad, 30, 26), L"A_", t.accent, logo);
    TextStyle title;
    title.size = 15.0f;
    title.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    ui.text(rect(pad + 32, pad, w - 2 * pad - 32, 26), title_, t.text, title);
    TextStyle body;
    body.size = 13.0f;
    body.wrap = true;
    body.valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    ui.text(rect(pad, pad + 40, w - 2 * pad, 90), message_, t.muted, body);

    const float by = h - pad - 34;
    bool remember = remember_;
    // "Lembrar" como uma caixa de seleção simples.
    const R box = rect(pad, by + 8, 18, 18);
    if (ui.clickable(makeId(L"remember"), rect(pad, by, 200, 34))) remember = !remember;
    ui.fill(box, remember ? t.accent : t.raised, 4);
    if (!remember) ui.stroke(box, t.border, 4);
    if (remember) ui.icon(box, L'\xE73E', t.onAccent, 11);
    TextStyle rs;
    rs.size = 12.5f;
    ui.text(rect(pad + 26, by, 170, 34), L"Lembrar a escolha", t.muted, rs);
    remember_ = remember;

    const float acceptW = std::max(120.0f, ui.textWidth(accept_, {}) + 32);
    const float declineW = std::max(100.0f, ui.textWidth(decline_, {}) + 28);
    if (ui.button(makeId(L"accept"), rect(w - pad - acceptW, by, acceptW, 34), accept_, ButtonKind::Primary)) {
        finish(true);
        return;
    }
    if (ui.button(makeId(L"decline"), rect(w - pad - acceptW - 8 - declineW, by, declineW, 34), decline_,
                  ButtonKind::Secondary)) {
        finish(false);
    }
}

} // namespace ap::ui
