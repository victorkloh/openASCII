#include "ui/Launcher.h"

#include "core/Utf8.h"
#include "ui/Labels.h"
#include "util/Log.h"
#include "util/Win32.h"
#include "version.h"
#include "wallpaper/Wallpaper.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace ap::ui {

namespace fs = std::filesystem;

namespace {

constexpr float kSidebar = 256.0f;
constexpr float kPad = 32.0f;
constexpr float kMaxContent = 780.0f;

TextStyle eyebrow() {
    TextStyle s;
    s.kind = FontKind::Mono;
    s.size = 11.0f;
    return s;
}
TextStyle smallText(float size = 12.0f) {
    TextStyle s;
    s.size = size;
    return s;
}
TextStyle heading(float size) {
    TextStyle s;
    s.size = size;
    s.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    return s;
}
TextStyle mono(float size) {
    TextStyle s;
    s.kind = FontKind::Mono;
    s.size = size;
    return s;
}
std::wstring widen(std::string_view s) { return wallpaper::toWide(s); }
std::wstring toWide32(const std::u32string& s) {
    std::wstring out;
    for (char32_t c : s) out += c <= 0xFFFF ? static_cast<wchar_t>(c) : L'?';
    return out;
}
bool same(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return !a.empty() && !b.empty() && fs::equivalent(a, b, ec);
}

// Rampas de caracteres prontas a usar.
struct CharsetPreset {
    const wchar_t* label;
    const char32_t* chars;
};
constexpr CharsetPreset kCharsets[] = {
    {L"clássico", U" .:-=+*#%@"},
    {L"blocos", U" ░▒▓█"},
    {L"braille", U" ⠁⠃⠇⡇⡏⡟⡿⣿"},
    {L"pontos", U" .·•●"},
    {L"binário", U" 01"},
};

} // namespace

bool Launcher::create(HINSTANCE instance) {
    return createWindow(instance, L"AsciiPaperLauncher", L"AsciiPaper", WS_OVERLAPPEDWINDOW, 0, 1080, 720);
}

void Launcher::show() {
    refresh();
    ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    invalidate();
}

std::wstring Launcher::activeBase() const {
    for (const auto& it : items_) {
        if (same(it.path, app_.activePath())) return it.base;
    }
    return {};
}

void Launcher::refresh() {
    items_ = app_.wallpapers();
    if (selectedBase_.empty() ||
        std::none_of(items_.begin(), items_.end(), [&](const auto& it) { return it.base == selectedBase_; })) {
        selectedBase_ = activeBase();
        if (selectedBase_.empty() && !items_.empty()) selectedBase_ = items_.front().base;
    }
    // Manter a variante em edição se ainda pertencer ao selecionado; senão a ativa ou a original.
    fs::path variant = editor_.path;
    const bool stillValid = std::any_of(items_.begin(), items_.end(),
                                        [&](const auto& it) { return it.base == selectedBase_ && same(it.path, variant); });
    if (!stillValid) {
        variant.clear();
        for (const auto& it : items_) {
            if (it.base != selectedBase_) continue;
            if (same(it.path, app_.activePath())) variant = it.path;
            if (variant.empty() && it.theme.empty()) variant = it.path;
        }
    }
    if (!variant.empty()) selectVariant(variant);

    Manifest base, active;
    std::string err;
    if (app_.inspect(app_.activePath(), base, active, err)) accent_ = accentFromPalette(active.palette);
    applyWindowTheme();
    invalidate();
}

void Launcher::select(const std::wstring& base) {
    selectedBase_ = base;
    page_ = Page::Wallpaper;
    fs::path variant;
    for (const auto& it : items_) {
        if (it.base != base) continue;
        if (same(it.path, app_.activePath())) variant = it.path;
        if (variant.empty() && it.theme.empty()) variant = it.path;
    }
    selectVariant(variant);
}

void Launcher::selectVariant(const fs::path& path) {
    editor_.load(app_, path);
    selectedSwatch_ = -1;
    message_.clear();
    charsetEdit_ = editor_.valid ? toWide32(editor_.effective.charset) : L"";
    if (fonts_.empty()) fonts_ = app_.monospaceFonts();
    fontIndex_ = -1;
    for (size_t i = 0; i < fonts_.size(); ++i) {
        if (widen(editor_.effective.font) == fonts_[i]) fontIndex_ = static_cast<int>(i);
    }
}

HRESULT Launcher::renderToPng(const std::wstring& path, int page) {
    page_ = static_cast<Page>(std::clamp(page, 0, 2));
    refresh();
    if (page_ == Page::New) newName_ = L"Aurora";
    return renderToPngImpl(path, 1080, 720, 1.5f);
}

bool Launcher::handleMessage(UINT msg, WPARAM, LPARAM lp, LRESULT& result) {
    if (msg == WM_CLOSE) {
        ShowWindow(hwnd_, SW_HIDE); // fechar só esconde: a app continua na bandeja
        result = 0;
        return true;
    }
    if (msg == WM_GETMINMAXINFO) {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
        const float s = dpiScale();
        mm->ptMinTrackSize = {static_cast<LONG>(860 * s), static_cast<LONG>(580 * s)};
        result = 0;
        return true;
    }
    return false;
}

float Launcher::section(Ui& ui, float x, float y, float w, std::wstring_view title, std::wstring_view hint) {
    const Tokens& t = ui.theme();
    ui.text(rect(x, y, w, 18), L"// " + std::wstring(title), t.faint, eyebrow());
    y += 22;
    if (!hint.empty()) {
        ui.text(rect(x, y, w, 18), hint, t.faint, smallText());
        y += 24;
    } else {
        y += 6;
    }
    return y;
}

void Launcher::build(Ui& ui, float w, float h) {
    const Tokens& t = ui.theme();
    // Barra lateral
    ui.fill(rect(0, 0, kSidebar, h), t.surface);
    ui.line({kSidebar, 0}, {kSidebar, h}, t.border);
    buildSidebar(ui, h);

    // Conteúdo, com scroll
    const R viewport{kSidebar + 1, 0, w, h};
    const float offset = ui.scrollArea(makeId(L"main-scroll", static_cast<int>(page_)), viewport, contentHeight_);
    const float cw = std::min(w - kSidebar - 2 * kPad, kMaxContent);
    const float x = kSidebar + kPad;
    ui.pushClip(viewport);
    float bottom = 0;
    switch (page_) {
    case Page::Wallpaper: bottom = buildWallpaperPage(ui, x, kPad - offset, cw); break;
    case Page::New: bottom = buildNewPage(ui, x, kPad - offset, cw); break;
    case Page::Settings: bottom = buildSettingsPage(ui, x, kPad - offset, cw); break;
    }
    ui.popClip();
    contentHeight_ = bottom + offset + kPad;
}

void Launcher::buildSidebar(Ui& ui, float h) {
    const Tokens& t = ui.theme();
    const float x = 16, w = kSidebar - 32;
    float y = 22;
    TextStyle logo = mono(18);
    logo.weight = DWRITE_FONT_WEIGHT_BOLD;
    ui.text(rect(x + 2, y, 34, 30), L"A_", t.accent, logo);
    ui.text(rect(x + 38, y, 150, 30), L"AsciiPaper", t.text, heading(15));
    TextStyle ver = mono(11);
    ver.align = DWRITE_TEXT_ALIGNMENT_TRAILING;
    ui.text(rect(x, y, w, 30), L"v" ASCIIPAPER_VERSION_WSTR, t.faint, ver);
    y += 58;
    ui.text(rect(x, y, w, 18), L"// wallpapers", t.faint, eyebrow());
    y += 26;

    const std::wstring active = activeBase();
    const float listBottom = h - 112;
    const R listArea{0, y, kSidebar, listBottom};
    int bases = 0;
    for (const auto& it : items_) bases += it.theme.empty() ? 1 : 0;
    const float rowH = 60;
    const float off = ui.scrollArea(makeId(L"side-scroll"), listArea, bases * rowH);
    ui.pushClip(listArea);
    float ry = y - off;
    for (size_t i = 0; i < items_.size(); ++i) {
        const auto& it = items_[i];
        if (!it.theme.empty()) continue;
        const R row = rect(8, ry, kSidebar - 16, rowH - 4);
        const bool sel = page_ == Page::Wallpaper && it.base == selectedBase_;
        const bool over = ui.hovered(row) && ui.hovered(listArea);
        if (ui.clickable(makeId(L"side", static_cast<int>(i)), row) && ui.hovered(listArea)) select(it.base);
        if (sel) ui.fill(row, t.raised, 8);
        else if (over) ui.fill(row, D2D1_COLOR_F{t.raised.r, t.raised.g, t.raised.b, 0.6f}, 8);
        if (sel) ui.fill(rect(row.left, row.top + 14, 3, rowH - 32), t.accent, 1.5f);
        ui.image(rect(row.left + 12, row.top + 10, 64, 36), it.preview, 5);
        int variants = 0;
        for (const auto& v : items_) variants += v.base == it.base ? 1 : 0;
        const bool isActive = it.base == active;
        TextStyle name = smallText(13);
        name.weight = sel ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
        ui.text(rect(row.left + 88, row.top + 9, width(row) - 96, 20), it.label, t.text, name);
        std::wstring sub = variants > 1 ? std::format(L"{} variantes", variants) : L"1 variante";
        if (it.userOwned) sub += L" · teu";
        ui.text(rect(row.left + 88, row.top + 29, width(row) - 96, 18), sub, t.faint, smallText(11.5f));
        if (isActive) ui.fill(rect(row.right - 18, row.top + 16, 7, 7), t.accent, 3.5f);
        ry += rowH;
    }
    ui.popClip();

    // Rodapé
    float by = h - 100;
    ui.line({16, by - 8}, {kSidebar - 16, by - 8}, t.border);
    if (ui.button(makeId(L"new"), rect(x, by, w, 34), L"Novo wallpaper",
                  page_ == Page::New ? ButtonKind::Secondary : ButtonKind::Ghost, L'\xE710')) {
        page_ = Page::New;
        newError_.clear();
    }
    by += 40;
    if (ui.button(makeId(L"settings"), rect(x, by, w, 34), L"Definições",
                  page_ == Page::Settings ? ButtonKind::Secondary : ButtonKind::Ghost, L'\xE713')) {
        page_ = Page::Settings;
    }
}

float Launcher::buildWallpaperPage(Ui& ui, float x, float y, float w) {
    const Tokens& t = ui.theme();
    if (!editor_.valid) {
        ui.text(rect(x, y, w, 24), L"Não foi possível ler este wallpaper.", t.text, heading(15));
        ui.text(rect(x, y + 30, w, 60), widen(editor_.error), t.danger, [] {
            TextStyle s = smallText(12.5f);
            s.wrap = true;
            s.valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
            return s;
        }());
        return y + 100;
    }
    const Manifest& m = editor_.effective;
    const bool isActive = same(editor_.path, app_.activePath());
    bool changed = false;

    // ---- Cabeçalho: preview grande + nome + ações ----
    const float imgW = std::min(w * 0.48f, 380.0f), imgH = imgW * 9 / 16;
    fs::path preview;
    for (const auto& it : items_) {
        if (same(it.path, editor_.path)) preview = it.preview;
    }
    ui.image(rect(x, y, imgW, imgH), preview, 10);
    const float tx = x + imgW + 24, tw = w - imgW - 24;
    ui.text(rect(tx, y + 2, tw, 30), widen(m.name.empty() ? "Sem nome" : m.name), t.text, heading(22));
    std::wstring meta = m.author.empty() ? L"" : L"por " + widen(m.author);
    if (!m.version.empty()) meta += (meta.empty() ? L"v" : L" · v") + widen(m.version);
    ui.text(rect(tx, y + 36, tw, 18), meta, t.faint, mono(11.5f));
    TextStyle desc = smallText(13);
    desc.wrap = true;
    desc.valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    ui.text(rect(tx, y + 62, tw, 44), widen(m.description), t.muted, desc);

    const float by = y + imgH - 34;
    if (isActive) {
        ui.button(makeId(L"applied"), rect(tx, by, 120, 34), L"Em uso", ButtonKind::Secondary, L'\xE73E', false);
    } else if (ui.button(makeId(L"apply"), rect(tx, by, 120, 34), L"Aplicar", ButtonKind::Primary)) {
        app_.activate(editor_.path);
    }
    if (ui.button(makeId(L"edit-code"), rect(tx + 128, by, 140, 34), L"Editar código", ButtonKind::Secondary,
                  L'\xE943')) {
        app_.openInEditor(editor_.path);
    }
    if (ui.iconButton(makeId(L"open-folder"), rect(tx + 276, by, 34, 34), L'\xE8B7')) app_.openFolder(editor_.path);
    y += imgH + 20;

    if (isActive && !app_.lastError().empty()) {
        const std::wstring err = app_.lastError();
        const R box = rect(x, y, w, 52);
        ui.fill(box, D2D1_COLOR_F{t.danger.r, t.danger.g, t.danger.b, 0.12f}, 8);
        TextStyle es = mono(11.5f);
        es.wrap = true;
        es.valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
        ui.text(inset(box, 12, 8), err, t.danger, es);
        y += 62;
    }

    // ---- Variantes ----
    y = section(ui, x, y + 8, w, L"variantes",
                isActive ? L"" : L"As alterações aparecem ao vivo quando este wallpaper está em uso.");
    float cx = x;
    for (size_t i = 0; i < items_.size(); ++i) {
        const auto& it = items_[i];
        if (it.base != selectedBase_) continue;
        const std::wstring label = it.theme.empty() ? L"original" : it.theme;
        const float cw = ui.textWidth(label, smallText(12.5f)) + 26;
        if (cx + cw > x + w) {
            cx = x;
            y += 36;
        }
        const bool sel = same(it.path, editor_.path);
        if (ui.chip(makeId(L"var", static_cast<int>(i)), rect(cx, y, cw, 28), label, sel) && !sel) {
            selectVariant(it.path);
            if (activeBase() == selectedBase_) app_.activate(it.path); // no ativo, trocar já
        }
        cx += cw + 6;
    }
    // Guardar como tema
    const float fieldW = 170;
    if (cx + fieldW + 90 > x + w) {
        cx = x;
        y += 36;
    }
    const bool submit = ui.textField(makeId(L"theme-name"), rect(cx + 8, y - 2, fieldW, 32), themeName_, L"nome do tema");
    if ((ui.button(makeId(L"save-theme"), rect(cx + 8 + fieldW + 6, y - 2, 82, 32), L"Guardar",
                   ButtonKind::Secondary) ||
         submit) &&
        !themeName_.empty()) {
        std::wstring error;
        if (app_.saveAsTheme(editor_.path, themeName_, error)) {
            message_ = L"Tema \"" + themeName_ + L"\" guardado.";
            themeName_.clear();
            items_ = app_.wallpapers();
        } else {
            message_ = error;
        }
    }
    y += 34;
    if (!message_.empty()) {
        ui.text(rect(x, y, w, 18), message_, t.muted, smallText());
        y += 22;
    }

    // ---- Grelha ----
    y = section(ui, x, y + 14, w, L"grelha",
                L"A resolução muda o tamanho dos caracteres; a escala muda o tamanho do efeito.");
    const float colW = (w - 24) / 2;
    float cell = static_cast<float>(m.cell);
    if (ui.slider(makeId(L"cell"), rect(x, y, colW, 40), L"Resolução da grelha", cell, 4, 40, 1,
                  std::format(L"{} px", static_cast<int>(cell)))) {
        editor_.draft.cell = static_cast<int>(cell);
        changed = true;
    }
    float scale = m.scale;
    if (ui.slider(makeId(L"scale"), rect(x + colW + 24, y, colW, 40), L"Escala do efeito", scale, 2, 40, 0.5f,
                  std::format(L"{:.1f}", scale))) {
        editor_.draft.scale = scale;
        changed = true;
    }
    y += 54;
    float fps = static_cast<float>(m.fps);
    if (ui.slider(makeId(L"fps"), rect(x, y, colW, 40), L"Fotogramas por segundo", fps, 10, 120, 1,
                  std::format(L"{} fps", static_cast<int>(fps)))) {
        editor_.draft.fps = static_cast<int>(fps);
        changed = true;
    }
    ui.text(rect(x + colW + 24, y, colW, 18), L"Fonte", t.muted, smallText(12.5f));
    if (ui.dropdown(makeId(L"font"), rect(x + colW + 24, y + 20, colW, 32), fonts_, fontIndex_) && fontIndex_ >= 0) {
        editor_.draft.font = win::toUtf8(fonts_[static_cast<size_t>(fontIndex_)]);
        changed = true;
    }
    y += 64;

    // ---- Cores ----
    y = section(ui, x, y + 10, w, L"cores", L"Fundo e paleta (da cor mais escura à mais clara).");
    std::vector<Rgba8> palette = m.palette;
    Rgba8 background = m.background;
    ui.text(rect(x, y, 60, 28), L"Fundo", t.muted, smallText(12.5f));
    if (ui.swatch(makeId(L"bg-swatch"), rect(x + 52, y, 28, 28), background, selectedSwatch_ == 0)) {
        selectedSwatch_ = selectedSwatch_ == 0 ? -1 : 0;
    }
    float sx = x + 112;
    ui.text(rect(sx, y, 60, 28), L"Paleta", t.muted, smallText(12.5f));
    sx += 54;
    for (size_t i = 0; i < palette.size(); ++i) {
        const int idx = static_cast<int>(i) + 1;
        if (ui.swatch(makeId(L"pal", idx), rect(sx, y, 28, 28), palette[i], selectedSwatch_ == idx)) {
            selectedSwatch_ = selectedSwatch_ == idx ? -1 : idx;
        }
        sx += 36;
    }
    if (palette.size() < 8 && ui.iconButton(makeId(L"pal-add"), rect(sx, y, 28, 28), L'\xE710')) {
        palette.push_back(palette.empty() ? Rgba8{255, 255, 255, 255} : palette.back());
        editor_.draft.palette = palette;
        selectedSwatch_ = static_cast<int>(palette.size());
        changed = true;
    }
    if (selectedSwatch_ >= 1 && palette.size() > 1 &&
        ui.iconButton(makeId(L"pal-del"), rect(sx + 32, y, 28, 28), L'\xE74D')) {
        palette.erase(palette.begin() + (selectedSwatch_ - 1));
        editor_.draft.palette = palette;
        selectedSwatch_ = -1;
        changed = true;
    }
    y += 40;
    if (selectedSwatch_ >= 0 && selectedSwatch_ <= static_cast<int>(palette.size())) {
        Rgba8& target = selectedSwatch_ == 0 ? background : palette[static_cast<size_t>(selectedSwatch_ - 1)];
        const R panel = rect(x, y, std::min(w, 420.0f), 196);
        ui.fill(panel, t.surface, 10);
        ui.stroke(panel, t.border, 10);
        if (ui.colorPicker(makeId(L"picker", selectedSwatch_), inset(panel, 14, 10), target)) {
            if (selectedSwatch_ == 0) editor_.draft.background = background;
            else editor_.draft.palette = palette;
            changed = true;
        }
        y += 208;
    }

    // ---- Caracteres ----
    y = section(ui, x, y + 10, w, L"caracteres", L"Do mais vazio ao mais cheio. Enter para aplicar.");
    const bool charsetSubmit = ui.textField(makeId(L"charset"), rect(x, y, std::min(w, 360.0f), 32), charsetEdit_,
                                            L" .:-=+*#%@");
    float px = x + std::min(w, 360.0f) + 12;
    for (size_t i = 0; i < std::size(kCharsets); ++i) {
        const float cw = ui.textWidth(kCharsets[i].label, smallText(12.5f)) + 24;
        if (px + cw > x + w) break;
        if (ui.chip(makeId(L"cs", static_cast<int>(i)), rect(px, y + 2, cw, 28), kCharsets[i].label,
                    m.charset == kCharsets[i].chars)) {
            editor_.draft.charset = std::u32string(kCharsets[i].chars);
            charsetEdit_ = toWide32(*editor_.draft.charset);
            changed = true;
        }
        px += cw + 6;
    }
    if (charsetSubmit && !charsetEdit_.empty()) {
        editor_.draft.charset = std::u32string(charsetEdit_.begin(), charsetEdit_.end());
        changed = true;
    }
    y += 44;

    // ---- Parâmetros ----
    if (!m.params.empty()) {
        y = section(ui, x, y + 10, w, L"parâmetros");
        for (size_t i = 0; i < m.params.size(); ++i) {
            const ParamSpec& p = m.params[i];
            const float px2 = x + (i % 2) * (colW + 24);
            float v = p.value;
            const float range = p.max - p.min;
            const float step = range > 20 ? 1.0f : range / 200.0f;
            const std::wstring vt = range > 20 ? std::format(L"{:.0f}", v) : std::format(L"{:.2f}", v);
            if (ui.slider(makeId(L"param", static_cast<int>(i)), rect(px2, y, colW, 40), paramLabel(p.name), v, p.min,
                          p.max, step, vt)) {
                editor_.setParam(p.name, v);
                changed = true;
            }
            if (i % 2 == 1 || i + 1 == m.params.size()) y += 54;
        }
    }

    // ---- Repor ----
    y += 10;
    if (ui.button(makeId(L"reset"), rect(x, y, 190, 34), L"Repor predefinições", ButtonKind::Danger, L'\xE72C',
                  !themeIsEmpty(editor_.draft))) {
        editor_.reset(app_);
        selectVariant(editor_.path);
    }
    y += 34;

    if (changed || ui.sliderReleased()) editor_.apply(app_, !ui.anyActive() || ui.sliderReleased());
    return y;
}

float Launcher::buildNewPage(Ui& ui, float x, float y, float w) {
    const Tokens& t = ui.theme();
    ui.text(rect(x, y, w, 32), L"Novo wallpaper", t.text, heading(22));
    TextStyle sub = smallText(13);
    ui.text(rect(x, y + 36, w, 20), L"Uma pasta com um wallpaper.toml e um main.hlsl. Grava o código e vês o resultado ao vivo.",
            t.muted, sub);
    y += 76;

    y = section(ui, x, y, w, L"nome");
    const bool submit = ui.textField(makeId(L"new-name"), rect(x, y, std::min(w, 360.0f), 34), newName_,
                                     L"ex.: Aurora boreal");
    y += 50;

    y = section(ui, x, y, w, L"começar a partir de");
    const float cardW = std::min(168.0f, (w - 3 * 12) / 4), thumbH = cardW * 9 / 16;
    float cx = x;
    auto card = [&](int index, std::wstring_view label, const fs::path& preview) {
        if (cx + cardW > x + w + 1) {
            cx = x;
            y += thumbH + 40;
        }
        const R r = rect(cx, y, cardW, thumbH + 28);
        const R thumb = rect(r.left, r.top, cardW, thumbH);
        const bool sel = templateIndex_ == index;
        if (ui.clickable(makeId(L"tpl", index + 1), r)) templateIndex_ = index;
        ui.image(thumb, preview, 8);
        if (sel) ui.stroke(inset(thumb, -2, -2), t.accent, 10, 2);
        else if (ui.hovered(r)) ui.stroke(inset(thumb, -2, -2), t.border, 10, 1.5f);
        ui.text(rect(r.left + 2, thumb.bottom + 6, cardW, 18), label, sel ? t.text : t.muted, smallText(12.5f));
        cx += cardW + 12;
    };
    card(-1, L"em branco", {});
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].theme.empty()) card(static_cast<int>(i), items_[i].label, items_[i].preview);
    }
    y += thumbH + 48;

    const bool create = ui.button(makeId(L"create"), rect(x, y, 210, 36), L"Criar e abrir o código",
                                  ButtonKind::Primary, 0, !newName_.empty());
    if ((create || submit) && !newName_.empty()) {
        fs::path created;
        const fs::path tpl = templateIndex_ >= 0 && templateIndex_ < static_cast<int>(items_.size())
                                 ? items_[static_cast<size_t>(templateIndex_)].path
                                 : fs::path{};
        if (app_.createWallpaper(newName_, tpl, created, newError_)) {
            newName_.clear();
            newError_.clear();
            items_ = app_.wallpapers();
            app_.activate(created);
            app_.openInEditor(created);
            selectedBase_ = created.filename().wstring();
            page_ = Page::Wallpaper;
            refresh();
        }
    }
    y += 48;
    if (!newError_.empty()) {
        ui.text(rect(x, y, w, 18), newError_, t.danger, smallText());
        y += 24;
    }
    TextStyle hint = mono(11.5f);
    ui.text(rect(x, y, w, 18), L"Fica em %APPDATA%\\AsciiPaper\\wallpapers. Guia: docs/CREATING_WALLPAPERS.md", t.faint,
            hint);
    return y + 24;
}

float Launcher::buildSettingsPage(Ui& ui, float x, float y, float w) {
    const Tokens& t = ui.theme();
    ui.text(rect(x, y, w, 32), L"Definições", t.text, heading(22));
    y += 52;
    GlobalSettings s = app_.settings();
    bool changed = false;
    const float rw = std::min(w, 620.0f);

    y = section(ui, x, y, w, L"monitores e rato");
    changed |= ui.toggle(makeId(L"span"), rect(x - 12, y, rw + 12, 54), L"Estender por todos os monitores",
                         L"Um só efeito atravessa os ecrãs, como no modo Estender do Windows.", s.spanMonitors);
    y += 58;
    changed |= ui.toggle(makeId(L"interaction"), rect(x - 12, y, rw + 12, 54), L"Reagir ao rato sobre qualquer janela",
                         L"Por defeito, o wallpaper só reage quando o rato está sobre o ambiente de trabalho.",
                         s.interactionAlways);
    y += 70;

    y = section(ui, x, y, w, L"desempenho");
    changed |= ui.toggle(makeId(L"pause-max"), rect(x - 12, y, rw + 12, 54), L"Pausar com uma janela maximizada",
                         L"O monitor tapado deixa de desenhar (0% de CPU).", s.pauseOnMaximized);
    y += 58;
    changed |= ui.toggle(makeId(L"pause-battery"), rect(x - 12, y, rw + 12, 54), L"Pausar na poupança de bateria",
                         L"Quando o Windows liga a poupança de bateria.", s.pauseOnBatterySaver);
    y += 62;
    float maxFps = static_cast<float>(s.maxFps);
    if (ui.slider(makeId(L"max-fps"), rect(x, y, rw, 40), L"Limite de fotogramas por segundo", maxFps, 15, 144, 1,
                  std::format(L"{} fps", static_cast<int>(maxFps)))) {
        s.maxFps = static_cast<int>(maxFps);
    }
    if (ui.sliderReleased()) changed = true;
    y += 66;

    y = section(ui, x, y, w, L"sistema");
    changed |= ui.toggle(makeId(L"autostart"), rect(x - 12, y, rw + 12, 54), L"Iniciar com o Windows",
                         L"Arranca discretamente na bandeja quando inicias sessão.", s.autostart);
    y += 70;
    if (changed) app_.setSettings(s);

    y = section(ui, x, y, w, L"pastas");
    if (ui.button(makeId(L"open-wp"), rect(x, y, 200, 34), L"Pasta de wallpapers", ButtonKind::Secondary, L'\xE8B7')) {
        app_.openFolder(wallpaper::userWallpaperDir());
    }
    if (ui.button(makeId(L"open-logs"), rect(x + 208, y, 160, 34), L"Pasta de logs", ButtonKind::Secondary,
                  L'\xE9F9')) {
        app_.openFolder(log::folder());
    }
    y += 58;
    ui.text(rect(x, y, w, 18), L"AsciiPaper " ASCIIPAPER_VERSION_WSTR L" · licença MIT", t.faint, mono(11.5f));
    return y + 24;
}

} // namespace ap::ui
