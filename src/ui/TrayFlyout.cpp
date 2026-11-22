#include "ui/TrayFlyout.h"

#include "ui/Labels.h"
#include "util/Win32.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace ap::ui {

// O menu da bandeja tem um só trabalho: trocar o que está atrás das janelas depressa. Por
// isso as miniaturas são o conteúdo principal e tudo o resto é discreto:
//   - a grelha de wallpapers tem scroll e no máximo ~3,5 linhas visíveis (a linha cortada
//     mostra que há mais); a altura do painel adapta-se para caber SEMPRE na área de trabalho;
//   - os ajustes rápidos ficam recolhidos ("Ajustes rápidos ›"), a pausa é um ícone no
//     cabeçalho e o rodapé só tem "Personalizar" e "Sair";
//   - teclado: setas para escolher, Enter para aplicar, Esc para fechar.

namespace {
constexpr float kWidth = 344.0f;
constexpr float kPad = 16.0f;
constexpr int kCols = 3;
constexpr float kGridGap = 8.0f;
constexpr float kLabelH = 18.0f;
constexpr float kMaxRows = 3.5f; // linhas visíveis no máximo (meia linha = "há mais")
constexpr float kMinRows = 1.5f; // num ecrã muito baixo, nunca menos do que isto
constexpr UINT kMsgRelayout = WM_APP + 41;

constexpr float kHeaderH = 28.0f, kHeaderGap = 14.0f;
constexpr float kEyebrowH = 18.0f, kEyebrowGap = 8.0f;
constexpr float kVariantsH = 28.0f;
constexpr float kRowGap = 12.0f;
constexpr float kDisclosureH = 34.0f;
constexpr float kSliderH = 48.0f, kFpsH = 38.0f;
constexpr float kErrorH = 44.0f;
constexpr float kFooterH = 16.0f + 34.0f;

std::wstring eyebrowText(std::wstring_view s) { return L"// " + std::wstring(s); }

TextStyle eyebrowStyle() {
    TextStyle s;
    s.kind = FontKind::Mono;
    s.size = 11.0f;
    return s;
}

float cardWidth() { return (kWidth - 2 * kPad - (kCols - 1) * kGridGap) / kCols; }
float thumbHeight() { return std::round(cardWidth() * 9.0f / 16.0f); }
float rowHeight() { return thumbHeight() + 4.0f + kLabelH + kGridGap; }
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
    bases_.clear();
    for (size_t i = 0; i < items_.size(); ++i)
        if (items_[i].theme.empty()) bases_.push_back(i);
    editor_.load(app_, app_.activePath());
    if (editor_.valid) accent_ = accentFromPalette(editor_.effective.palette);
    applyWindowTheme();
    if (isVisible()) {
        place(anchor_);
        invalidate();
    }
}

std::wstring TrayFlyout::activeBase() const {
    const std::filesystem::path active = app_.activePath();
    for (const auto& w : items_)
        if (samePath(w.path, active)) return w.base;
    return {};
}

int TrayFlyout::activeIndex() const {
    const std::wstring base = activeBase();
    for (size_t k = 0; k < bases_.size(); ++k)
        if (items_[bases_[k]].base == base) return static_cast<int>(k);
    return -1;
}

int TrayFlyout::variantCount() const {
    const std::wstring base = activeBase();
    int n = 0;
    for (const auto& w : items_) n += w.base == base ? 1 : 0;
    return n;
}

float TrayFlyout::gridContentHeight() const {
    const int rows = (static_cast<int>(bases_.size()) + kCols - 1) / kCols;
    return std::max(0.0f, rows * rowHeight() - kGridGap);
}

// Tudo o que não é a grelha. layoutHeight() e build() usam a mesma conta.
float TrayFlyout::fixedHeight() const {
    float h = kPad + kHeaderH + kHeaderGap + kEyebrowH + kEyebrowGap;
    h += kRowGap;                                         // depois da grelha
    if (variantCount() > 1) h += kVariantsH + kRowGap;
    h += kDisclosureH;
    if (adjustOpen_) h += 6.0f + kSliderH * 2 + kFpsH;
    if (!app_.lastError().empty()) h += kErrorH;
    h += kFooterH + kPad;
    return h;
}

float TrayFlyout::layoutHeight() {
    return fixedHeight() + std::min(gridContentHeight(), kMaxRows * rowHeight() - kGridGap);
}

void TrayFlyout::place(const RECT& a) {
    anchor_ = a;
    const float s = dpiScale();
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromRect(&a, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT work = mi.rcWork;
    const int margin = static_cast<int>(12 * s);
    // Altura: a desejada, mas nunca mais do que cabe na área de trabalho deste monitor.
    const float available = static_cast<float>(work.bottom - work.top - 2 * margin) / s;
    const float minimum = fixedHeight() + std::min(gridContentHeight(), kMinRows * rowHeight() - kGridGap);
    contentHeight_ = std::max(std::min(layoutHeight(), available), minimum);
    const int w = static_cast<int>(kWidth * s), h = static_cast<int>(contentHeight_ * s);
    const int cx = (a.left + a.right) / 2;
    int x = std::clamp(cx - w / 2, static_cast<int>(work.left) + margin, static_cast<int>(work.right) - w - margin);
    int y;
    // A barra de tarefas pode estar em baixo (o normal), em cima ou dos lados.
    if (a.top >= work.bottom - 2) y = work.bottom - h - margin;
    else if (a.bottom <= work.top + 2) y = work.top + margin;
    else y = std::clamp(static_cast<int>(a.top) - h / 2, static_cast<int>(work.top) + margin,
                        static_cast<int>(work.bottom) - h - margin);
    if (a.right <= work.left + 2) x = work.left + margin;
    if (a.left >= work.right - 2) x = work.right - w - margin;
    y = std::max(y, static_cast<int>(work.top) + margin); // nunca acima do topo
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
    focus_ = -1;
    scrollToActive_ = true; // abrir já com o wallpaper em uso à vista
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
    if (msg == kMsgRelayout) {
        if (isVisible()) place(anchor_);
        invalidate();
        result = 0;
        return true;
    }
    if (msg == WM_KEYDOWN) {
        const int n = static_cast<int>(bases_.size());
        int f = focus_ < 0 ? std::max(activeIndex(), 0) : focus_;
        bool handled = true;
        switch (wp) {
        case VK_ESCAPE: hide(); break;
        case VK_LEFT: f = std::max(0, f - (focus_ < 0 ? 0 : 1)); break;
        case VK_RIGHT: f = std::min(n - 1, f + (focus_ < 0 ? 0 : 1)); break;
        case VK_UP: f = focus_ < 0 ? f : std::max(0, f - kCols); break;
        case VK_DOWN: f = focus_ < 0 ? f : std::min(n - 1, f + kCols); break;
        case VK_HOME: f = 0; break;
        case VK_END: f = n - 1; break;
        case VK_RETURN:
        case VK_SPACE:
            if (focus_ >= 0 && focus_ < n) app_.activate(items_[bases_[static_cast<size_t>(focus_)]].path);
            break;
        default: handled = false; break;
        }
        if (handled) {
            if (wp != VK_ESCAPE && wp != VK_RETURN && wp != VK_SPACE && n > 0) {
                focus_ = std::clamp(f, 0, n - 1);
                scrollToFocus_ = true;
            }
            invalidate();
            result = 0;
            return true;
        }
    }
    if (msg == WM_CLOSE) {
        // Como no launcher: um WM_CLOSE que não veio do utilizador é um pedido de fora para sair.
        if (closingByUser() && isVisible()) hide();
        else app_.quitImmediately();
        result = 0;
        return true;
    }
    return false;
}

HRESULT TrayFlyout::renderToPng(const std::wstring& path) {
    refresh();
    scrollToActive_ = true;
    return renderToPngImpl(path, kWidth, layoutHeight(), 2.0f);
}

void TrayFlyout::build(Ui& ui, float w, float h) {
    const Tokens& t = ui.theme();
    float y = kPad;
    const bool paused = app_.paused();

    // ---- Cabeçalho: marca, estado e pausa ----
    TextStyle logo;
    logo.kind = FontKind::Mono;
    logo.size = 15.0f;
    logo.weight = DWRITE_FONT_WEIGHT_BOLD;
    ui.text(rect(kPad, y, 30, kHeaderH), L"A_", t.accent, logo);
    TextStyle title;
    title.size = 14.0f;
    title.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    ui.text(rect(kPad + 30, y, 140, kHeaderH), L"AsciiPaper", t.text, title);
    const R pauseBtn = rect(w - kPad - 28, y, 28, 28);
    if (ui.iconButton(makeId(L"pause"), pauseBtn, paused ? L'\xE768' : L'\xE769')) app_.setPaused(!paused);
    TextStyle status = eyebrowStyle();
    status.align = DWRITE_TEXT_ALIGNMENT_TRAILING;
    ui.text(rect(pauseBtn.left - 118, y, 100, kHeaderH), paused ? L"em pausa" : L"a correr", t.faint, status);
    ui.fill(rect(pauseBtn.left - 14, y + 10, 8, 8), paused ? t.faint : t.accent, 4);
    y += kHeaderH + kHeaderGap;

    // ---- Wallpapers: grelha com scroll ----
    ui.text(rect(kPad, y, w - 2 * kPad, kEyebrowH), eyebrowText(L"wallpapers"), t.faint, eyebrowStyle());
    TextStyle count = eyebrowStyle();
    count.align = DWRITE_TEXT_ALIGNMENT_TRAILING;
    ui.text(rect(kPad, y, w - 2 * kPad, kEyebrowH), std::to_wstring(bases_.size()), t.faint, count);
    y += kEyebrowH + kEyebrowGap;

    const float gridH = std::max(rowHeight() - kGridGap, h - fixedHeight());
    const R viewport{kPad - 4, y, w - kPad + 4, y + gridH}; // 4 px a mais de cada lado para o anel de seleção
    const Id scrollId = makeId(L"flyout-grid");
    const float cardW = cardWidth(), thumbH = thumbHeight(), rowH = rowHeight();
    const int active = activeIndex();
    // Abrir com o wallpaper em uso à vista; com o teclado, manter a seleção à vista.
    const int target = scrollToFocus_ ? focus_ : (scrollToActive_ ? active : -1);
    if (target >= 0) {
        const float top = static_cast<float>(target / kCols) * rowH;
        float off = ui.scrollOffset(scrollId);
        if (scrollToActive_) off = top - (gridH - rowH) * 0.5f;           // centrar
        else if (top < off) off = top;                                    // só o necessário
        else if (top + rowH - kGridGap > off + gridH) off = top + rowH - kGridGap - gridH;
        ui.setScrollOffset(scrollId, off);
    }
    scrollToActive_ = scrollToFocus_ = false;
    const float content = gridContentHeight();
    const float off = ui.scrollArea(scrollId, viewport, content);
    ui.pushClip(viewport);
    for (size_t k = 0; k < bases_.size(); ++k) {
        const auto& it = items_[bases_[k]];
        const int row = static_cast<int>(k) / kCols, col = static_cast<int>(k) % kCols;
        const R card = rect(kPad + col * (cardW + kGridGap), y + row * rowH - off, cardW, thumbH + 4 + kLabelH);
        if (card.bottom < viewport.top || card.top > viewport.bottom) continue; // fora da vista
        const R thumb = rect(card.left, card.top, cardW, thumbH);
        const bool isActive = static_cast<int>(k) == active;
        const bool over = ui.hovered(card) && ui.hovered(viewport);
        if (ui.clickable(makeId(L"card", static_cast<int>(k)), card) && ui.hovered(viewport) && !isActive)
            app_.activate(it.path);
        ui.image(thumb, it.preview, 6);
        if (samePath(app_.loadingPath(), it.path)) {
            // A preparar (compilar) este wallpaper: dizê-lo, para não parecer que o clique falhou.
            static constexpr wchar_t kSpin[] = L"|/-\\";
            ui.fill(thumb, D2D1_COLOR_F{t.bg.r, t.bg.g, t.bg.b, 0.72f}, 6);
            TextStyle ls = eyebrowStyle();
            ls.align = DWRITE_TEXT_ALIGNMENT_CENTER;
            ui.text(thumb, std::wstring(L"a preparar ") + kSpin[(GetTickCount64() / 120) % 4], t.text, ls);
        }
        if (isActive) ui.stroke(inset(thumb, -2, -2), t.accent, 8, 2);
        else if (over) ui.stroke(inset(thumb, -1.5f, -1.5f), t.border, 7.5f, 1.5f);
        if (static_cast<int>(k) == focus_) ui.stroke(inset(card, -4, -3), t.text, 9, 1.5f); // seleção do teclado
        TextStyle name;
        name.size = 12.0f;
        name.weight = isActive ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
        ui.text(rect(card.left + 1, thumb.bottom + 4, cardW - 2, kLabelH), it.base, isActive ? t.text : t.muted, name);
    }
    // Sombra suave nas bordas onde há mais conteúdo (a meia linha de baixo já o sugere).
    auto fade = [&](float edgeY, float dir) {
        for (int s = 0; s < 6; ++s) {
            const float a = 0.85f * (1.0f - s / 6.0f);
            const float yy = dir > 0 ? edgeY + s * 3.0f : edgeY - (s + 1) * 3.0f;
            ui.fill(R{viewport.left, yy, viewport.right - 7, yy + 3.0f}, D2D1_COLOR_F{t.bg.r, t.bg.g, t.bg.b, a});
        }
    };
    if (off > 0.5f) fade(viewport.top, 1.0f);
    if (off < content - gridH - 0.5f) fade(viewport.bottom, -1.0f);
    ui.popClip();
    y += gridH + kRowGap;

    // ---- Variantes do wallpaper em uso ----
    if (variantCount() > 1) {
        const std::wstring base = activeBase();
        const std::filesystem::path activePath = app_.activePath();
        TextStyle cs;
        cs.size = 12.0f;
        float x = kPad;
        int shown = 0, total = 0;
        for (size_t i = 0; i < items_.size(); ++i) {
            const auto& v = items_[i];
            if (v.base != base) continue;
            ++total;
            const std::wstring label = v.theme.empty() ? L"original" : v.theme;
            const float cw = ui.textWidth(label, cs) + 22;
            if (x + cw > w - kPad - 40) continue; // deixa lugar para "+N"
            const bool sel = samePath(v.path, activePath);
            if (ui.chip(makeId(L"variant", static_cast<int>(i)), rect(x, y, cw, kVariantsH), label, sel) && !sel)
                app_.activate(v.path);
            x += cw + 6;
            ++shown;
        }
        if (shown < total &&
            ui.chip(makeId(L"variant-more"), rect(x, y, 36, kVariantsH), std::format(L"+{}", total - shown), false)) {
            hide();
            app_.openLauncher();
        }
        y += kVariantsH + kRowGap;
    }

    // ---- Ajustes rápidos (recolhidos por defeito) ----
    {
        const R row = rect(kPad - 8, y, w - 2 * kPad + 16, kDisclosureH);
        const bool over = ui.hovered(row);
        if (over) ui.fill(row, t.hover, 6);
        if (ui.clickable(makeId(L"adjust-toggle"), row)) {
            adjustOpen_ = !adjustOpen_;
            PostMessageW(hwnd_, kMsgRelayout, 0, 0); // a altura muda: reposicionar fora da pintura
        }
        TextStyle ls;
        ls.size = 13.0f;
        ui.text(rect(kPad, y, 200, kDisclosureH), L"Ajustes rápidos", over ? t.text : t.muted, ls);
        ui.icon(rect(w - kPad - 20, y, 20, kDisclosureH), adjustOpen_ ? L'\xE70D' : L'\xE76C', t.faint, 10);
        y += kDisclosureH;
    }
    if (adjustOpen_) {
        y += 6;
        bool changed = false;
        if (editor_.valid) {
            // Velocidade: o parâmetro "speed" se existir; senão o primeiro parâmetro.
            const ParamSpec* p = editor_.effective.findParam("speed");
            if (!p && !editor_.effective.params.empty()) p = &editor_.effective.params.front();
            if (p) {
                float v = p->value;
                if (ui.slider(makeId(L"quick-speed"), rect(kPad, y, w - 2 * kPad, 40), paramLabel(p->name), v, p->min,
                              p->max, (p->max - p->min) / 100.0f, std::format(L"{:.2f}", v))) {
                    editor_.setParam(p->name, v);
                    changed = true;
                }
            }
            y += kSliderH;
            float cell = static_cast<float>(editor_.effective.cell);
            if (ui.slider(makeId(L"quick-cell"), rect(kPad, y, w - 2 * kPad, 40), L"Resolução da grelha", cell, 4, 32,
                          1, std::format(L"{} px", static_cast<int>(cell)))) {
                editor_.draft.cell = static_cast<int>(cell);
                changed = true;
            }
            y += kSliderH;
        } else {
            y += kSliderH * 2;
        }
        if (changed || ui.sliderReleased()) editor_.apply(app_, ui.sliderReleased());

        // FPS do fundo (global): automático ou fixo.
        GlobalSettings gs = app_.settings();
        TextStyle ls;
        ls.size = 12.0f;
        ui.text(rect(kPad, y, 40, 28), L"FPS", t.muted, ls);
        static constexpr int kChoices[] = {0, 15, 30, 60, 120};
        float cx = kPad + 40;
        for (int k = 0; k < static_cast<int>(std::size(kChoices)); ++k) {
            const int v = kChoices[k];
            const std::wstring label = v == 0 ? L"auto" : std::to_wstring(v);
            const float cw = ui.textWidth(label, ls) + 22;
            if (ui.chip(makeId(L"flyout-fps", k), rect(cx, y, cw, 28), label, gs.fps == v) && gs.fps != v) {
                gs.fps = v;
                app_.setSettings(gs);
            }
            cx += cw + 5;
        }
        y += kFpsH;
    }

    // ---- Erro do wallpaper ativo ----
    const std::wstring err = app_.lastError();
    if (!err.empty()) {
        const R box = rect(kPad, y + 4, w - 2 * kPad, 36);
        ui.fill(box, D2D1_COLOR_F{t.danger.r, t.danger.g, t.danger.b, 0.12f}, 6);
        TextStyle es;
        es.size = 12.0f;
        ui.text(inset(box, 10, 0), err.substr(0, err.find(L'\n')), t.danger, es);
        y += kErrorH;
    }

    // ---- Ações ----
    ui.line({kPad, y + 6}, {w - kPad, y + 6}, t.border);
    y += 16;
    const float bw = (w - 2 * kPad - 8) / 2;
    if (ui.button(makeId(L"customize"), rect(kPad, y, bw, 34), L"Personalizar", ButtonKind::Ghost, L'\xE713')) {
        hide();
        app_.openLauncher();
    }
    if (ui.button(makeId(L"quit"), rect(kPad + bw + 8, y, bw, 34), L"Sair", ButtonKind::Ghost, L'\xE7E8')) {
        hide();
        app_.quit();
    }
}

} // namespace ap::ui
