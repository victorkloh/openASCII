#include "ui/Launcher.h"

#include "core/Utf8.h"
#include "ui/Labels.h"
#include "util/Log.h"
#include "util/Win32.h"
#include "version.h"
#include "wallpaper/Wallpaper.h"

#include <shobjidl.h>
#include <wrl/client.h>

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
bool same(const fs::path& a, const fs::path& b) { return samePath(a, b); }

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
    return createWindow(instance, L"OpenASCIILauncher", L"OpenASCII", WS_OVERLAPPEDWINDOW, 0, 1080, 720);
}

void Launcher::show() {
    refresh();
    if (app_.firstRunPending()) { // primeira vez: começar pelos primeiros passos
        page_ = Page::Welcome;
        welcomeStep_ = 0;
    }
    ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    RECT r{};
    GetWindowRect(hwnd_, &r);
    log::debug(L"Launcher aberto em ({}, {}) - ({}, {})", r.left, r.top, r.right, r.bottom);
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

HRESULT Launcher::renderToPng(const std::wstring& path, int page, int heightDip, int widthDip) {
    // 0 wallpaper, 1 novo, 2 definições; 4..7 os passos dos primeiros passos (3 é o diálogo).
    page_ = page >= 4 ? Page::Welcome : static_cast<Page>(std::clamp(page, 0, 2));
    welcomeStep_ = std::clamp(page - 4, 0, 3);
    if (page_ == Page::Settings) contentHeight_ = 0; // desenhar do topo
    refresh();
    if (page_ == Page::New) newName_ = L"Aurora";
    return renderToPngImpl(path, static_cast<float>(widthDip), static_cast<float>(heightDip), heightDip > 720 ? 1.0f : 1.5f);
}

namespace {
// Diálogo do Windows para escolher uma imagem, GIF ou vídeo.
bool pickMediaFile(HWND owner, std::wstring& out) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
    const COMDLG_FILTERSPEC types[] = {
        {L"Imagens, GIFs e vídeos", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.mp4;*.mov;*.m4v;*.wmv;*.avi;*.mkv;*.webm"},
        {L"Todos os ficheiros", L"*.*"},
    };
    dlg->SetFileTypes(static_cast<UINT>(std::size(types)), types);
    dlg->SetTitle(L"Escolher imagem, GIF ou vídeo");
    if (FAILED(dlg->Show(owner))) return false; // cancelado
    Microsoft::WRL::ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (FAILED(dlg->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return false;
    out = path;
    CoTaskMemFree(path);
    return true;
}
} // namespace

bool Launcher::handleMessage(UINT msg, WPARAM, LPARAM lp, LRESULT& result) {
    if (msg == kMsgPickSource) {
        std::wstring path;
        app_.setModalLoop(true); // o diálogo tem o seu próprio loop: o fundo continua a desenhar
        const bool ok = pickMediaFile(hwnd_, path);
        app_.setModalLoop(false);
        if (ok && editor_.valid) {
            editor_.draft.source = win::toUtf8(path);
            editor_.apply(app_, true);
        }
        invalidate();
        result = 0;
        return true;
    }
    if (msg == WM_CLOSE) {
        // O utilizador fechou a janela (X, Alt+F4): só esconder, a app continua na bandeja.
        // Um WM_CLOSE direto vem de fora (ex.: "taskkill" sem /f de um instalador): sair com
        // limpeza, mesmo com o launcher aberto (antes só o escondia e a app ficava a correr).
        if (closingByUser() && IsWindowVisible(hwnd_)) ShowWindow(hwnd_, SW_HIDE);
        else app_.quitImmediately();
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
    if (page_ == Page::Welcome) {
        buildWelcome(ui, w, h);
        return;
    }
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
    ui.text(rect(x + 38, y, 150, 30), L"OpenASCII", t.text, heading(15));
    TextStyle ver = mono(11);
    ver.align = DWRITE_TEXT_ALIGNMENT_TRAILING;
    ui.text(rect(x, y, w, 30), L"v" OPENASCII_VERSION_WSTR, t.faint, ver);
    y += 58;
    // Pesquisa: pelo nome, título, categoria ou descrição (sem distinguir maiúsculas).
    ui.textField(makeId(L"search"), rect(x, y, w, 32), search_, L"Procurar wallpapers");
    y += 44;

    auto lower = [](std::wstring s) {
        if (!s.empty()) CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
        return s;
    };
    const std::wstring query = lower(search_);
    // Grupos por tipo, numa ordem fixa; os teus wallpapers primeiro.
    static constexpr const wchar_t* kGroups[] = {L"Os teus", L"Paisagens", L"Ambiente", L"Retro", L"3D e espaço",
                                                 L"Simulações", L"Dados e média", L"Jogos", L"Outros"};
    auto groupOf = [&](const WallpaperInfo& it) -> size_t {
        if (it.userOwned) return 0;
        for (size_t g = 1; g + 1 < std::size(kGroups); ++g)
            if (it.category == kGroups[g]) return g;
        return std::size(kGroups) - 1;
    };
    struct Row {
        size_t item;  // índice em items_ (ou SIZE_MAX = cabeçalho)
        size_t group;
    };
    std::vector<Row> rows;
    for (size_t g = 0; g < std::size(kGroups); ++g) {
        bool header = false;
        for (size_t i = 0; i < items_.size(); ++i) {
            const auto& it = items_[i];
            if (!it.theme.empty() || groupOf(it) != g) continue;
            if (!query.empty()) {
                const std::wstring hay = lower(it.base + L" " + it.title + L" " + it.category + L" " + it.description);
                if (hay.find(query) == std::wstring::npos) continue;
            }
            if (!header) rows.push_back({SIZE_MAX, g});
            header = true;
            rows.push_back({i, g});
        }
    }

    const std::wstring active = activeBase();
    const float listBottom = h - 112;
    const R listArea{0, y, kSidebar, listBottom};
    const float rowH = 60, headerH = 30;
    float contentH = 0;
    for (const Row& r : rows) contentH += r.item == SIZE_MAX ? headerH : rowH;
    const float off = ui.scrollArea(makeId(L"side-scroll"), listArea, contentH);
    ui.pushClip(listArea);
    float ry = y - off;
    if (rows.empty()) {
        ui.text(rect(x, y + 6, w, 20), items_.empty() ? L"Ainda não há wallpapers." : L"Nada encontrado.", t.faint,
                smallText(12.5f));
    }
    for (const Row& r : rows) {
        if (r.item == SIZE_MAX) {
            ui.text(rect(x, ry + 8, w, 18), L"// " + lower(kGroups[r.group]), t.faint, eyebrow());
            ry += headerH;
            continue;
        }
        const size_t i = r.item;
        const auto& it = items_[i];
        if (ry > listBottom || ry + rowH < y) { // fora da vista
            ry += rowH;
            continue;
        }
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
    if (items_.empty()) {
        // Instalado sem os wallpapers predefinidos (ou apagaste-os todos): convidar a criar.
        ui.text(rect(x, y, w, 32), L"Ainda não tens wallpapers", t.text, heading(22));
        TextStyle body = smallText(13);
        body.wrap = true;
        body.valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
        ui.text(rect(x, y + 44, std::min(w, 560.0f), 80),
                L"Enquanto não houver nenhum, o fundo do Windows continua à vista. Cria o primeiro a partir de um "
                L"modelo em branco: o código abre no teu editor e cada gravação aparece logo no ambiente de trabalho.",
                t.muted, body);
        y += 132;
        if (ui.button(makeId(L"empty-new"), rect(x, y, 220, 36), L"Criar o primeiro wallpaper", ButtonKind::Primary,
                      L'\xE710')) {
            page_ = Page::New;
            newError_.clear();
        }
        y += 60;
        TextStyle hint = smallText(12);
        hint.wrap = true;
        hint.valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
        ui.text(rect(x, y, std::min(w, 560.0f), 40),
                L"Queres os 28 wallpapers que vêm com a app? Volta a correr o instalador e escolhe "
                L"\"Wallpapers predefinidos\".",
                t.faint, hint);
        return y + 48;
    }
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
    if (!m.category.empty()) meta += (meta.empty() ? L"" : L" · ") + widen(m.category);
    ui.text(rect(tx, y + 36, tw, 18), meta, t.faint, mono(11.5f));
    TextStyle desc = smallText(13);
    desc.wrap = true;
    desc.valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    // O cabeçalho cresce com a descrição: com o launcher estreito, uma descrição longa (o que
    // o wallpaper faz, o que o rato muda) passava por cima dos botões.
    const std::wstring description = widen(m.description);
    const float descH = ui.textHeight(description, desc, tw);
    const float headerH = std::max(imgH, 62 + descH + 14 + 34);
    ui.text(rect(tx, y + 62, tw, descH + 4), description, t.muted, desc);

    const float by = y + headerH - 34;
    const bool isLoading = same(app_.loadingPath(), editor_.path);
    if (isActive) {
        ui.button(makeId(L"applied"), rect(tx, by, 120, 34), L"Em uso", ButtonKind::Secondary, L'\xE73E', false);
    } else if (isLoading) {
        // A compilar o shader (pode levar uns segundos na primeira vez): o clique não se perdeu.
        static constexpr wchar_t kSpin[] = L"|/-\\";
        ui.button(makeId(L"loading"), rect(tx, by, 120, 34),
                  std::wstring(L"A preparar ") + kSpin[(GetTickCount64() / 120) % 4], ButtonKind::Secondary, 0, false);
    } else if (ui.button(makeId(L"apply"), rect(tx, by, 120, 34), L"Aplicar", ButtonKind::Primary)) {
        app_.activate(editor_.path);
    }
    if (ui.button(makeId(L"edit-code"), rect(tx + 128, by, 140, 34), L"Editar código", ButtonKind::Secondary,
                  L'\xE943')) {
        app_.openInEditor(editor_.path);
    }
    if (ui.iconButton(makeId(L"open-folder"), rect(tx + 276, by, 34, 34), L'\xE8B7')) app_.openFolder(editor_.path);
    y += headerH + 20;

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

    // ---- Entradas: o que o wallpaper usa, a imagem/vídeo e a playlist ----
    {
        std::wstring uses;
        auto add = [&](bool on, const wchar_t* label) {
            if (on) uses += (uses.empty() ? L"" : L" · ") + std::wstring(label);
        };
        add(m.useWindows, L"janelas");
        add(m.useIcons, L"ícones");
        add(m.useAudio, L"som do PC");
        add(m.useSystem, L"CPU e RAM");
        add(m.useWeather, L"tempo real");
        add(!m.text.empty(), L"texto");
        add(m.source == "webcam", L"webcam");
        add(!m.source.empty() && m.source != "webcam", L"imagem/vídeo");
        y = section(ui, x, y + 8, w, L"entradas",
                    uses.empty() ? L"Este wallpaper não usa entradas externas." : L"Usa: " + uses);
        if (!m.source.empty() && m.source != "webcam") {
            const std::wstring current = m.source == "wallpaper" ? L"O wallpaper do Windows"
                                                                  : fs::path(widen(m.source)).filename().wstring();
            ui.text(rect(x, y, w, 18), L"Imagem: " + current, t.muted, smallText(12.5f));
            y += 24;
            if (ui.button(makeId(L"pick-source"), rect(x, y, 220, 34), L"Escolher imagem ou vídeo", ButtonKind::Secondary,
                          L'\xE8B9')) {
                PostMessageW(hwnd_, kMsgPickSource, 0, 0); // o diálogo abre fora da pintura
            }
            if (m.source != "wallpaper" &&
                ui.button(makeId(L"source-windows"), rect(x + 228, y, 220, 34), L"Usar o wallpaper do Windows",
                          ButtonKind::Ghost)) {
                editor_.draft.source = "wallpaper";
                changed = true;
            }
            y += 46;
        }
        // Webcam e cidade configuram-se aqui mesmo (também estão nas Definições).
        if (m.source == "webcam" || m.useWeather) {
            GlobalSettings gs = app_.settings();
            bool settingsChanged = false;
            if (m.source == "webcam") {
                settingsChanged |= ui.toggle(makeId(L"wp-webcam"), rect(x - 12, y, std::min(w, 620.0f) + 12, 54),
                                             L"Permitir a webcam",
                                             L"A imagem nunca sai do PC. O Windows mostra quando a câmara está ligada.",
                                             gs.webcamAllowed);
                y += 60;
            }
            if (m.useWeather) {
                if (!cityLoaded_) {
                    cityEdit_ = gs.weatherCity;
                    cityLoaded_ = true;
                }
                ui.text(rect(x, y, w, 18), L"Cidade para o tempo real (Open-Meteo; vazio = sem internet)", t.muted,
                        smallText(12.5f));
                y += 22;
                const bool citySubmit = ui.textField(makeId(L"wp-city"), rect(x, y, 240, 32), cityEdit_, L"ex.: Lisboa");
                if ((ui.button(makeId(L"wp-city-save"), rect(x + 248, y, 90, 32), L"Guardar", ButtonKind::Secondary) ||
                     citySubmit) &&
                    cityEdit_ != gs.weatherCity) {
                    gs.weatherCity = cityEdit_;
                    settingsChanged = true;
                }
                if (!gs.weatherCity.empty())
                    ui.text(rect(x + 348, y, w - 348, 32), L"Em uso: " + gs.weatherCity, t.faint, smallText(12));
                y += 44;
            }
            if (settingsChanged) app_.setSettings(gs);
        }
        bool listed = app_.inPlaylist(editor_.path);
        if (ui.toggle(makeId(L"in-playlist"), rect(x - 12, y, std::min(w, 620.0f) + 12, 54), L"Incluir na playlist",
                      L"A playlist (em Definições) vai trocando entre os wallpapers marcados.", listed)) {
            app_.setInPlaylist(editor_.path, listed);
        }
        y += 58;
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
    // O charset é a rampa (o.value escolhe daqui). Os desenhos próprios do wallpaper (sprites,
    // setas, texto) não passam pela rampa: por isso escolher um conjunto liga também "Usar só
    // estes caracteres", que os troca pelo caractere do conjunto com a tinta mais parecida.
    // Antes, no aquário com "binário", os peixes continuavam com < > ° ~ e os conjuntos nem
    // apareciam nos wallpapers que só desenham com caracteres próprios (charset de 1 caractere).
    const bool usesRamp = editor_.base.charset.size() > 1;
    y = section(ui, x, y + 10, w, L"caracteres",
                usesRamp ? L"Do mais vazio ao mais cheio. Enter para aplicar."
                         : L"Este wallpaper desenha com os seus próprios caracteres. Escolhe um conjunto para os trocar.");
    const bool charsetSubmit = ui.textField(makeId(L"charset"), rect(x, y, std::min(w, 360.0f), 32), charsetEdit_,
                                            L" .:-=+*#%@");
    y += 42;
    float chipX = x;
    for (size_t i = 0; i < std::size(kCharsets); ++i) {
        const float cw = ui.textWidth(kCharsets[i].label, smallText(12.5f)) + 24;
        if (chipX + cw > x + w) { // não cabe: próxima linha (antes ficavam escondidos)
            chipX = x;
            y += 36;
        }
        if (ui.chip(makeId(L"cs", static_cast<int>(i)), rect(chipX, y, cw, 28), kCharsets[i].label,
                    m.charset == kCharsets[i].chars)) {
            editor_.draft.charset = std::u32string(kCharsets[i].chars);
            editor_.draft.charsetOnly = true;
            charsetEdit_ = toWide32(*editor_.draft.charset);
            changed = true;
        }
        chipX += cw + 6;
    }
    if (charsetSubmit && !charsetEdit_.empty()) {
        editor_.draft.charset = std::u32string(charsetEdit_.begin(), charsetEdit_.end());
        editor_.draft.charsetOnly = true;
        changed = true;
    }
    y += 40;
    bool only = m.charsetOnly;
    if (ui.toggle(makeId(L"charset-only"), rect(x - 12, y, std::min(w, 620.0f) + 12, 54), L"Usar só estes caracteres",
                  L"Também nos desenhos do wallpaper: peixes, setas, texto...", only)) {
        editor_.draft.charsetOnly = only;
        changed = true;
    }
    y += 60;

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

    // ---- Efeitos de ecrã: em qualquer wallpaper (o terminal traz CRT, mas qualquer um o pode ter) ----
    {
        y = section(ui, x, y + 10, w, L"efeitos de ecrã",
                    L"Aplicados ao desenhar os caracteres. Funcionam em qualquer wallpaper.");
        struct FxPreset {
            const wchar_t* label;
            float v[6]; // brilho, linhas, curvatura, vinheta, aberração, grão
        };
        static constexpr FxPreset kFx[] = {
            {L"Nenhum", {0, 0, 0, 0, 0, 0}},
            {L"CRT", {0.6f, 0.5f, 0.4f, 0.55f, 0.3f, 0.2f}},
            {L"Néon", {0.75f, 0, 0, 0.3f, 0, 0}},
            {L"Cinema", {0.2f, 0, 0, 0.6f, 0.15f, 0.3f}},
        };
        float px = x;
        for (int k = 0; k < static_cast<int>(std::size(kFx)); ++k) {
            bool match = true;
            for (size_t j = 0; j < 6; ++j) match = match && std::abs(postFxValue(m.post, j) - kFx[k].v[j]) < 0.02f;
            const float cw = ui.textWidth(kFx[k].label, smallText(12.5f)) + 26;
            if (ui.chip(makeId(L"fx-preset", k), rect(px, y, cw, 28), kFx[k].label, match) && !match) {
                for (size_t j = 0; j < 6; ++j) editor_.draft.post[j] = kFx[k].v[j];
                changed = true;
            }
            px += cw + 6;
        }
        y += 40;
        static constexpr const wchar_t* kFxLabels[] = {L"Brilho", L"Linhas (CRT)", L"Curvatura",
                                                       L"Vinheta", L"Aberração", L"Grão"};
        for (size_t j = 0; j < 6; ++j) {
            const float px2 = x + (j % 2) * (colW + 24);
            float v = postFxValue(m.post, j);
            if (ui.slider(makeId(L"fx", static_cast<int>(j)), rect(px2, y, colW, 40), kFxLabels[j], v, 0, 1, 0.01f,
                          std::format(L"{:.0f}%", v * 100))) {
                editor_.draft.post[j] = v;
                changed = true;
            }
            if (j % 2 == 1) y += 54;
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
    ui.text(rect(x, y, w, 18), L"Fica em %APPDATA%\\OpenASCII\\wallpapers. Guia: docs/CREATING_WALLPAPERS.md", t.faint,
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
    // FPS do fundo: automático (cada wallpaper decide) ou fixo para todos.
    ui.text(rect(x, y, rw, 20), L"FPS do fundo", t.text, smallText(13));
    ui.text(rect(x, y + 22, rw, 18),
            s.fps == 0 ? std::format(L"Automático: cada wallpaper decide (agora a {} fps).", app_.currentFps())
                       : std::wstring(L"Fixo para todos os wallpapers. Menos FPS = menos CPU e GPU."),
            t.faint, smallText(12));
    y += 50;
    {
        static constexpr int kChoices[] = {0, 15, 24, 30, 45, 60, 90, 120, 144};
        float cx = x;
        for (int k = 0; k < static_cast<int>(std::size(kChoices)); ++k) {
            const int v = kChoices[k];
            const std::wstring label = v == 0 ? L"auto" : std::to_wstring(v);
            const float cw = ui.textWidth(label, smallText(12.5f)) + 26;
            if (ui.chip(makeId(L"fps-chip", k), rect(cx, y, cw, 28), label, s.fps == v) && s.fps != v) {
                s.fps = v;
                changed = true;
            }
            cx += cw + 6;
        }
    }
    y += 46;
    float maxFps = static_cast<float>(s.maxFps);
    if (ui.slider(makeId(L"max-fps"), rect(x, y, rw, 40), L"Limite no modo automático", maxFps, 15, 144, 1,
                  std::format(L"{} fps", static_cast<int>(maxFps)))) {
        s.maxFps = static_cast<int>(maxFps);
    }
    if (ui.sliderReleased()) changed = true;
    y += 66;

    y = section(ui, x, y, w, L"entradas dos wallpapers",
                L"Só são usadas pelos wallpapers que as pedem; nada é gravado.");
    ui.text(rect(x, y, rw, 20), L"Cidade para o tempo real", t.text, smallText(13));
    ui.text(rect(x, y + 22, rw, 18),
            L"Para o HUD e o tema \"weather\" (via Open-Meteo). Vazio = a app nunca vai à internet.",
            t.faint, smallText(12));
    y += 46;
    if (!cityLoaded_) {
        cityEdit_ = s.weatherCity;
        cityLoaded_ = true;
    }
    const bool citySubmit = ui.textField(makeId(L"city"), rect(x, y, 260, 32), cityEdit_, L"ex.: Lisboa");
    if ((ui.button(makeId(L"city-save"), rect(x + 268, y, 90, 32), L"Guardar", ButtonKind::Secondary) || citySubmit) &&
        cityEdit_ != s.weatherCity) {
        s.weatherCity = cityEdit_;
        changed = true;
    }
    y += 46;
    changed |= ui.toggle(makeId(L"webcam"), rect(x - 12, y, rw + 12, 54), L"Permitir a webcam",
                         L"Para wallpapers como o Mirror. A imagem nunca sai do PC; o Windows mostra quando a câmara está ligada.",
                         s.webcamAllowed);
    y += 70;

    y = section(ui, x, y, w, L"playlist",
                std::format(L"{} wallpaper(s) marcados (no ecrã de cada wallpaper: \"Incluir na playlist\").",
                            s.playlistCount));
    float minutes = static_cast<float>(s.playlistMinutes);
    if (ui.slider(makeId(L"playlist-min"), rect(x, y, rw, 40), L"Trocar de wallpaper a cada", minutes, 0, 240, 5,
                  minutes < 1 ? std::wstring(L"desligada") : std::format(L"{} min", static_cast<int>(minutes)))) {
        s.playlistMinutes = static_cast<int>(minutes);
    }
    if (ui.sliderReleased()) changed = true;
    y += 54;
    changed |= ui.toggle(makeId(L"playlist-shuffle"), rect(x - 12, y, rw + 12, 54), L"Ordem aleatória",
                         L"Sem repetir o mesmo duas vezes seguidas.", s.playlistShuffle);
    y += 70;

    y = section(ui, x, y, w, L"sistema");
    changed |= ui.toggle(makeId(L"autostart"), rect(x - 12, y, rw + 12, 54), L"Iniciar com o Windows",
                         L"Arranca discretamente na bandeja quando inicias sessão.", s.autostart);
    y += 58;
    if (s.autostart) {
        changed |= ui.toggle(makeId(L"autostart-fast"), rect(x - 12, y, rw + 12, 54), L"Arranque rápido",
                             L"Logo que entras na conta, sem a espera do Windows (tarefa agendada, sem admin).",
                             s.autostartFast);
        y += 58;
    }
    y += 12;

    // Wallpaper Engine / Lively: pausar enquanto o OpenASCII corre (com os comandos deles).
    y = section(ui, x, y, w, L"outras apps de wallpaper",
                L"O Wallpaper Engine e o Lively podem aparecer por baixo do OpenASCII. As definições deles não mudam.");
    const std::wstring status = app_.otherAppsStatus();
    ui.text(rect(x, y, rw, 18), status.empty() ? L"Nenhuma app pausada agora." : status + L" pelo OpenASCII.",
            status.empty() ? t.faint : t.accent, smallText(12.5f));
    y += 28;
    auto choiceRow = [&](std::wstring_view label, Id id, const std::wstring_view* options, int count, int& value) {
        ui.text(rect(x, y, 110, 28), label, t.muted, smallText(12.5f));
        float cx = x + 110;
        for (int k = 0; k < count; ++k) {
            const float cw = ui.textWidth(options[k], smallText(12.5f)) + 26;
            if (ui.chip(makeId(id, k), rect(cx, y, cw, 28), options[k], value == k) && value != k) {
                value = k;
                changed = true;
            }
            cx += cw + 6;
        }
        y += 38;
    };
    static constexpr std::wstring_view kOnStart[] = {L"perguntar", L"pausar sempre", L"ignorar"};
    static constexpr std::wstring_view kOnExit[] = {L"perguntar", L"retomar sempre", L"deixar pausado"};
    choiceRow(L"Ao arrancar", makeId(L"apps-start"), kOnStart, 3, s.otherAppsOnStart);
    choiceRow(L"Ao sair", makeId(L"apps-exit"), kOnExit, 3, s.otherAppsOnExit);
    y += 12;
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
    ui.text(rect(x, y, w, 18), L"OpenASCII " OPENASCII_VERSION_WSTR L" · licença MIT", t.faint, mono(11.5f));
    return y + 24;
}

// ---------------------------------------------------------------------------
// Primeiros passos: na primeira vez que a app arranca, quatro passos curtos ocupam a janela
// inteira (sem a barra lateral): boas-vindas, desempenho, arranque com o Windows e "pronto".
// Cada escolha aplica-se logo (é o mesmo que mudar nas Definições).
// ---------------------------------------------------------------------------

void Launcher::finishWelcome() {
    app_.finishFirstRun();
    page_ = Page::Wallpaper;
    welcomeStep_ = 0;
    refresh();
}

void Launcher::buildWelcome(Ui& ui, float w, float h) {
    const Tokens& t = ui.theme();
    const float cw = std::min(w - 96.0f, 640.0f);
    const float x = (w - cw) / 2;
    float y = 40;
    constexpr int kSteps = 4;
    const int step = std::clamp(welcomeStep_, 0, kSteps - 1);

    // Marca e progresso.
    TextStyle logo = mono(18);
    logo.weight = DWRITE_FONT_WEIGHT_BOLD;
    ui.text(rect(x + 2, y, 34, 30), L"A_", t.accent, logo);
    ui.text(rect(x + 38, y, 200, 30), L"OpenASCII", t.text, heading(15));
    for (int k = 0; k < kSteps; ++k) { // pontos: o atual mais comprido
        const float dw = k == step ? 22.0f : 8.0f;
        float dx = x + cw;
        for (int j = kSteps - 1; j >= k; --j) dx -= (j == step ? 22.0f : 8.0f) + 6.0f;
        ui.fill(rect(dx + 6, y + 11, dw, 8), k <= step ? t.accent : t.raised, 4);
    }
    y += 64;

    GlobalSettings s = app_.settings();
    bool changed = false;
    TextStyle body = smallText(13.5f);
    body.wrap = true;
    body.valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    auto title = [&](std::wstring_view text) {
        ui.text(rect(x, y, cw, 36), text, t.text, heading(26));
        y += 48;
    };
    auto para = [&](std::wstring_view text, D2D1_COLOR_F color) {
        const float th = ui.textHeight(text, body, cw);
        ui.text(rect(x, y, cw, th + 4), text, color, body);
        y += th + 16;
    };
    auto infoBox = [&](wchar_t icon, std::wstring_view heading2, std::wstring_view text) {
        const float th = ui.textHeight(text, body, cw - 60);
        const R box = rect(x, y, cw, th + 50);
        ui.fill(box, t.raised, 10);
        ui.icon(rect(x + 16, y + 15, 20, 20), icon, t.accent, 15);
        TextStyle hs = smallText(13.5f);
        hs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        ui.text(rect(x + 46, y + 13, cw - 60, 22), heading2, t.text, hs);
        ui.text(rect(x + 46, y + 37, cw - 60, th + 4), text, t.muted, body);
        y += th + 50 + 14;
    };
    auto toggleRow = [&](std::wstring_view id, std::wstring_view label, std::wstring_view desc, bool& value) {
        changed |= ui.toggle(makeId(id), rect(x - 12, y, cw + 12, 54), label, desc, value);
        y += 58;
    };

    switch (step) {
    case 0:
        title(L"Bem-vindo ao OpenASCII");
        para(L"Wallpapers ASCII animados que vivem atrás dos ícones do ambiente de trabalho. Muitos reagem ao rato, e "
             L"todos param sozinhos quando não estão à vista, para não gastarem bateria nem desempenho.",
             t.muted);
        infoBox(L'\xE946', L"Onde fica a app",
                L"No ícone da área de notificação, junto ao relógio (talvez escondido na seta ^). Clica nele para "
                L"trocar de wallpaper ou pausar. Esta janela, o launcher, é onde personalizas tudo.");
        para(L"Vamos acertar o desempenho e o arranque com o Windows. Leva meio minuto, e fica tudo nas Definições "
             L"para mudares quando quiseres.",
             t.muted);
        break;

    case 1: {
        title(L"Desempenho");
        para(L"Quantas vezes por segundo o wallpaper se redesenha. Menos FPS gasta menos energia.", t.muted);
        struct Choice {
            const wchar_t* label;
            int fps;
            const wchar_t* hint;
        };
        static constexpr Choice kChoices[] = {
            {L"30 fps · poupança", 30, L"Bom para portáteis: a maioria dos efeitos continua suave."},
            {L"60 fps · fluido", 60, L"Recomendado num PC de secretária."},
            {L"cada wallpaper decide", 0, L"Cada wallpaper usa o ritmo que pede (até 60 fps)."},
        };
        float cx = x;
        const wchar_t* hint = L"";
        for (int k = 0; k < static_cast<int>(std::size(kChoices)); ++k) {
            const float chipW = ui.textWidth(kChoices[k].label, smallText(12.5f)) + 28;
            const bool sel = s.fps == kChoices[k].fps;
            if (sel) hint = kChoices[k].hint;
            if (ui.chip(makeId(L"wel-fps", k), rect(cx, y, chipW, 30), kChoices[k].label, sel) && !sel) {
                s.fps = kChoices[k].fps;
                if (s.fps == 0) s.maxFps = 60;
                changed = true;
            }
            cx += chipW + 8;
        }
        y += 40;
        if (*hint == 0) hint = L"Fixo noutro valor (podes mudar nas Definições).";
        ui.text(rect(x, y, cw, 20), hint, t.faint, smallText(12.5f));
        y += 40;
        toggleRow(L"wel-pause-max", L"Pausar com uma janela maximizada", L"O monitor tapado deixa de desenhar (0% de CPU).",
                  s.pauseOnMaximized);
        toggleRow(L"wel-pause-bat", L"Pausar na poupança de bateria", L"Quando o Windows liga a poupança de bateria.",
                  s.pauseOnBatterySaver);
        toggleRow(L"wel-span", L"Estender por todos os monitores",
                  L"Um só efeito atravessa os ecrãs, como no modo Estender do Windows.", s.spanMonitors);
        break;
    }

    case 2:
        title(L"Arrancar com o Windows");
        para(L"Opcional. Se ligares, o OpenASCII arranca sozinho quando entras na tua conta e fica no ícone da "
             L"bandeja.",
             t.muted);
        toggleRow(L"wel-autostart", L"Iniciar com o Windows", L"Podes desligar quando quiseres, nas Definições.",
                  s.autostart);
        if (s.autostart) {
            toggleRow(L"wel-fast", L"Arranque rápido",
                      L"Logo que entras na conta, sem a espera que o Windows impõe às apps de arranque.",
                      s.autostartFast);
            para(L"O arranque rápido usa uma tarefa do Agendador de Tarefas do Windows, só para a tua conta e sem "
                 L"pedir administrador. O normal usa a lista de arranque do Windows, que as atrasa uns segundos.",
                 t.faint);
            infoBox(L'\xE72E', L"E com o PC bloqueado?",
                    L"Se bloqueares o PC (Win+L), o OpenASCII continua aberto, em pausa e a 0% de CPU: quando "
                    L"desbloqueias, o wallpaper já lá está. Antes de entrares na conta nenhuma app pode correr, e o "
                    L"ecrã de bloqueio é sempre o do Windows.");
        }
        break;

    default: {
        title(L"Tudo pronto");
        static constexpr std::pair<wchar_t, const wchar_t*> kTips[] = {
            {L'\xE8B9', L"Escolhe um wallpaper na lista à esquerda ou no ícone da bandeja."},
            {L'\xE790', L"Na página de cada wallpaper mudas cores, caracteres e efeitos de ecrã, e vês tudo ao vivo."},
            {L'\xE943', L"Novo wallpaper cria o teu a partir de um modelo e abre o código no teu editor."},
            {L'\xE713', L"Estas opções ficam todas nas Definições."},
        };
        for (const auto& [icon, text] : kTips) {
            const float th = ui.textHeight(text, body, cw - 44);
            ui.icon(rect(x, y + 1, 22, 22), icon, t.accent, 15);
            ui.text(rect(x + 38, y, cw - 44, th + 4), text, t.muted, body);
            y += std::max(26.0f, th) + 14;
        }
        break;
    }
    }
    if (changed) app_.setSettings(s);

    // Navegação, fixa em baixo.
    const float by = h - 40 - 36;
    ui.line({x, by - 20}, {x + cw, by - 20}, t.border);
    if (step < kSteps - 1 && ui.button(makeId(L"wel-skip"), rect(x, by, 110, 36), L"Saltar", ButtonKind::Ghost)) {
        finishWelcome();
        return;
    }
    const wchar_t* next = step == 0 ? L"Vamos lá" : step == kSteps - 1 ? L"Começar a usar" : L"Seguinte";
    if (step > 0 && ui.button(makeId(L"wel-back"), rect(x + cw - 160 - 8 - 110, by, 110, 36), L"Voltar",
                              ButtonKind::Secondary)) {
        welcomeStep_ = step - 1;
    }
    if (ui.button(makeId(L"wel-next"), rect(x + cw - 160, by, 160, 36), next, ButtonKind::Primary)) {
        if (step == kSteps - 1) {
            finishWelcome();
            return;
        }
        welcomeStep_ = step + 1;
    }
}

} // namespace ap::ui
