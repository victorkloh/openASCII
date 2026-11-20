#include "ui/TrayFlyout.h"

#include "ui/Labels.h"
#include "util/Win32.h"

#include <algorithm>
#include <format>

namespace ap::ui {

namespace {
constexpr float kWidth = 344.0f;
constexpr float kPad = 16.0f;
constexpr float kGap = 10.0f;

std::wstring eyebrowText(std::wstring_view s) { return L"// " + std::wstring(s); }

TextStyle eyebrowStyle() {
    TextStyle s;
    s.kind = FontKind::Mono;
    s.size = 11.0f;
    return s;
}
} // namespace

bool TrayFlyout::create(HINSTANCE instance) {
    // CS_DROPSHADOW: sombra do sistema; WS_EX_TOOLWINDOW: fora da barra de tarefas e do Alt+Tab.
    if (!createWindow(instance, L"AsciiPaperFlyout", L"AsciiPaper", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                      static_cast<int>(kWidth), 420, CS_DROPSHADOW)) {
        return false;
    }
    refresh();
    return true;
}

void TrayFlyout::refresh() {
    items_ = app_.wallpapers();
    editor_.load(app_, app_.activePath());
    if (editor_.valid) accent_ = accentFromPalette(editor_.effective.palette);
    applyWindowTheme();
    if (isVisible()) {
        place(anchor_);
        invalidate();
    }
}

float TrayFlyout::layoutHeight() {
    // Igual ao que build() desenha: cabeçalho, grelha de wallpapers, temas, ajustes, ações.
    int bases = 0;
    for (const auto& w : items_) bases += w.theme.empty() ? 1 : 0;
    const float cardW = (kWidth - 2 * kPad - kGap) / 2;
    const float cardH = cardW * 9 / 16 + 26;
    const int rows = (bases + 1) / 2;
    float y = kPad + 48 + 22 + rows * (cardH + kGap);
    // Linha de temas: só se o wallpaper ativo tiver variantes (igual a build()).
    std::wstring activeBase;
    for (const auto& w : items_) {
        std::error_code ec;
        if (std::filesystem::equivalent(w.path, app_.activePath(), ec)) activeBase = w.base;
    }
    int variants = 0;
    for (const auto& w : items_) variants += w.base == activeBase ? 1 : 0;
    if (variants > 1) y += 38;
    y += 30 + 48 * 2;                     // "ajustes" + 2 sliders
    if (!app_.lastError().empty()) y += 44;
    y += 16 + 36 + kPad;                  // divisória + ações
    return y;
}

void TrayFlyout::place(const RECT& a) {
    anchor_ = a;
    const float s = dpiScale();
    contentHeight_ = layoutHeight();
    const int w = static_cast<int>(kWidth * s), h = static_cast<int>(contentHeight_ * s);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromRect(&a, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT work = mi.rcWork;
    const int margin = static_cast<int>(12 * s);
    const int cx = (a.left + a.right) / 2;
    int x = std::clamp(static_cast<int>(cx) - w / 2, static_cast<int>(work.left) + margin, static_cast<int>(work.right) - w - margin);
    int y;
    // A barra de tarefas pode estar em baixo (o normal), em cima ou dos lados.
    if (a.top >= work.bottom - 2) y = work.bottom - h - margin;
    else if (a.bottom <= work.top + 2) y = work.top + margin;
    else y = std::clamp(static_cast<int>(a.top) - h / 2, static_cast<int>(work.top) + margin, static_cast<int>(work.bottom) - h - margin);
    if (a.right <= work.left + 2) x = work.left + margin;
    if (a.left >= work.right - 2) x = work.right - w - margin;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

void TrayFlyout::toggle(const RECT& anchorPx) {
    if (isVisible()) {
        hide();
        return;
    }
    // O clique no ícone que fechou o painel (ao perder o foco) não o deve reabrir logo.
    if (GetTickCount64() - hiddenAt_ < 300) return;
    refresh();
    place(anchorPx);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_); // para sabermos quando o utilizador clica fora (WM_ACTIVATE)
    invalidate();
}

void TrayFlyout::hide() {
    if (!isVisible()) return;
    ShowWindow(hwnd_, SW_HIDE);
    hiddenAt_ = GetTickCount64();
}

bool TrayFlyout::handleMessage(UINT msg, WPARAM wp, LPARAM, LRESULT& result) {
    if (msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE) {
        hide(); // clicar fora fecha, como os painéis do sistema
        result = 0;
        return true;
    }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        hide();
        result = 0;
        return true;
    }
    return false;
}

HRESULT TrayFlyout::renderToPng(const std::wstring& path) {
    refresh();
    return renderToPngImpl(path, kWidth, layoutHeight(), 2.0f);
}

void TrayFlyout::build(Ui& ui, float w, float) {
    const Tokens& t = ui.theme();
    float y = kPad;

    // ---- Cabeçalho: logótipo ASCII, nome, estado ----
    TextStyle logo;
    logo.kind = FontKind::Mono;
    logo.size = 15.0f;
    logo.weight = DWRITE_FONT_WEIGHT_BOLD;
    ui.text(rect(kPad, y, 30, 28), L"A_", t.accent, logo);
    TextStyle title;
    title.size = 14.0f;
    title.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    ui.text(rect(kPad + 30, y, 160, 28), L"AsciiPaper", t.text, title);
    const bool paused = app_.paused();
    TextStyle status = eyebrowStyle();
    status.align = DWRITE_TEXT_ALIGNMENT_TRAILING;
    ui.text(rect(w - kPad - 140, y, 122, 28), paused ? L"em pausa" : L"a correr", t.faint, status);
    const D2D1_COLOR_F dot = paused ? t.faint : t.accent;
    ui.fill(rect(w - kPad - 8, y + 10, 8, 8), dot, 4);
    y += 28 + 20;

    // ---- Wallpapers ----
    ui.text(rect(kPad, y, w - 2 * kPad, 18), eyebrowText(L"wallpaper"), t.faint, eyebrowStyle());
    y += 22;
    const float cardW = (w - 2 * kPad - kGap) / 2;
    const float thumbH = cardW * 9 / 16;
    const std::filesystem::path active = app_.activePath();
    std::wstring activeBase;
    for (const auto& it : items_) {
        std::error_code ec;
        if (std::filesystem::equivalent(it.path, active, ec)) activeBase = it.base;
    }
    int col = 0;
    for (size_t i = 0; i < items_.size(); ++i) {
        const auto& it = items_[i];
        if (!it.theme.empty()) continue;
        const R card = rect(kPad + col * (cardW + kGap), y, cardW, thumbH + 26);
        const R thumb = rect(card.left, card.top, cardW, thumbH);
        const bool isActive = it.base == activeBase;
        const bool over = ui.hovered(card);
        if (ui.clickable(makeId(L"card", static_cast<int>(i)), card) && !isActive) app_.activate(it.path);
        ui.image(thumb, it.preview, 8);
        if (isActive) ui.stroke(inset(thumb, -2, -2), t.accent, 10, 2);
        else if (over) ui.stroke(inset(thumb, -2, -2), t.border, 10, 1.5f);
        TextStyle name;
        name.size = 12.5f;
        name.weight = isActive ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
        ui.text(rect(card.left + 2, thumb.bottom + 4, cardW - 4, 20), it.label, isActive ? t.text : t.muted, name);
        if (++col == 2) {
            col = 0;
            y += thumbH + 26 + kGap;
        }
    }
    if (col != 0) y += thumbH + 26 + kGap;

    // ---- Temas do wallpaper ativo ----
    std::vector<const WallpaperInfo*> variants;
    for (const auto& it : items_) {
        if (it.base == activeBase) variants.push_back(&it);
    }
    if (variants.size() > 1) {
        float x = kPad;
        for (size_t i = 0; i < variants.size(); ++i) {
            const auto* v = variants[i];
            const std::wstring label = v->theme.empty() ? L"original" : v->theme;
            TextStyle cs;
            cs.size = 12.5f;
            const float cw = ui.textWidth(label, cs) + 24;
            if (x + cw > w - kPad) break;
            std::error_code ec;
            const bool sel = std::filesystem::equivalent(v->path, active, ec);
            if (ui.chip(makeId(L"variant", static_cast<int>(i)), rect(x, y, cw, 28), label, sel) && !sel) {
                app_.activate(v->path);
            }
            x += cw + 6;
        }
        y += 38;
    }

    // ---- Ajustes rápidos ----
    ui.text(rect(kPad, y, w - 2 * kPad, 18), eyebrowText(L"ajustes rápidos"), t.faint, eyebrowStyle());
    y += 30;
    bool changed = false;
    if (editor_.valid) {
        // Velocidade: o parâmetro "speed" se existir; senão o primeiro parâmetro.
        const ParamSpec* p = editor_.effective.findParam("speed");
        if (!p && !editor_.effective.params.empty()) p = &editor_.effective.params.front();
        if (p) {
            float v = p->value;
            const std::wstring label = paramLabel(p->name);
            if (ui.slider(makeId(L"quick-speed"), rect(kPad, y, w - 2 * kPad, 40), label, v, p->min, p->max,
                          (p->max - p->min) / 100.0f, std::format(L"{:.2f}", v))) {
                editor_.setParam(p->name, v);
                changed = true;
            }
        }
        y += 48;
        float cell = static_cast<float>(editor_.effective.cell);
        if (ui.slider(makeId(L"quick-cell"), rect(kPad, y, w - 2 * kPad, 40), L"Resolução da grelha", cell, 4, 32, 1,
                      std::format(L"{} px", static_cast<int>(cell)))) {
            editor_.draft.cell = static_cast<int>(cell);
            changed = true;
        }
        y += 48;
    } else {
        y += 96;
    }
    if (changed || ui.sliderReleased()) editor_.apply(app_, ui.sliderReleased());

    // ---- Erro do wallpaper ativo ----
    const std::wstring err = app_.lastError();
    if (!err.empty()) {
        const R box = rect(kPad, y, w - 2 * kPad, 36);
        ui.fill(box, D2D1_COLOR_F{t.danger.r, t.danger.g, t.danger.b, 0.12f}, 6);
        TextStyle es;
        es.size = 12.0f;
        ui.text(inset(box, 10, 0), err.substr(0, err.find(L'\n')), t.danger, es);
        y += 44;
    }

    // ---- Ações ----
    ui.line({kPad, y + 4}, {w - kPad, y + 4}, t.border);
    y += 16;
    const float bw = (w - 2 * kPad - 2 * 8) / 3;
    if (ui.button(makeId(L"pause"), rect(kPad, y, bw, 34), paused ? L"Retomar" : L"Pausar", ButtonKind::Ghost,
                  paused ? L'\xE768' : L'\xE769')) {
        app_.setPaused(!paused);
    }
    if (ui.button(makeId(L"customize"), rect(kPad + bw + 8, y, bw, 34), L"Personalizar", ButtonKind::Ghost,
                  L'\xE713')) {
        hide();
        app_.openLauncher();
    }
    if (ui.button(makeId(L"quit"), rect(kPad + 2 * (bw + 8), y, bw, 34), L"Sair", ButtonKind::Ghost, L'\xE7E8')) {
        hide();
        app_.quit();
    }
}

} // namespace ap::ui
