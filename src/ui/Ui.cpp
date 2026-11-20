#include "ui/Ui.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace ap::ui {

namespace {

D2D1_COLOR_F hex(uint32_t rgb, float a = 1.0f) {
    return {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a};
}

D2D1_COLOR_F withAlpha(D2D1_COLOR_F c, float a) { return {c.r, c.g, c.b, c.a * a}; }

D2D1_COLOR_F mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

float channelLum(float c) { return c <= 0.03928f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float luminance(D2D1_COLOR_F c) { return 0.2126f * channelLum(c.r) + 0.7152f * channelLum(c.g) + 0.0722f * channelLum(c.b); }

D2D1_COLOR_F fromRgba(Rgba8 c) { return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f}; }

void rgbToHsv(Rgba8 c, float& h, float& s, float& v) {
    const float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    v = mx;
    s = mx > 0 ? d / mx : 0;
    if (d <= 0) {
        h = 0;
        return;
    }
    if (mx == r) h = std::fmod((g - b) / d, 6.0f);
    else if (mx == g) h = (b - r) / d + 2.0f;
    else h = (r - g) / d + 4.0f;
    h /= 6.0f;
    if (h < 0) h += 1.0f;
}

Rgba8 hsvToRgb(float h, float s, float v, uint8_t a) {
    const float i = std::floor(h * 6.0f), f = h * 6.0f - i;
    const float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    float r = v, g = t, b = p;
    switch (static_cast<int>(i) % 6) {
    case 0: r = v, g = t, b = p; break;
    case 1: r = q, g = v, b = p; break;
    case 2: r = p, g = v, b = t; break;
    case 3: r = p, g = q, b = v; break;
    case 4: r = t, g = p, b = v; break;
    default: r = v, g = p, b = q; break;
    }
    auto to8 = [](float x) { return static_cast<uint8_t>(std::lround(std::clamp(x, 0.0f, 1.0f) * 255.0f)); };
    return {to8(r), to8(g), to8(b), a};
}

std::wstring toHex(Rgba8 c) { return std::format(L"#{:02x}{:02x}{:02x}", c.r, c.g, c.b); }

} // namespace

Id makeId(std::wstring_view text, int index) {
    Id h = 1469598103934665603ull;
    for (wchar_t c : text) {
        h ^= static_cast<Id>(c);
        h *= 1099511628211ull;
    }
    return makeId(h, index);
}

Tokens Tokens::make(bool dark, Rgba8 accentRgb) {
    Tokens t;
    t.dark = dark;
    D2D1_COLOR_F accent = fromRgba(accentRgb);
    if (dark) {
        t.bg = hex(0x0f1115);
        t.surface = hex(0x161920);
        t.raised = hex(0x1e222b);
        t.hover = hex(0x272c37);
        t.border = hex(0x2a2f3a);
        t.text = hex(0xe8eaf0);
        t.muted = hex(0xa0a7b4);
        t.faint = hex(0x7d8593);
        t.danger = hex(0xf47171);
        // Destaque claro o suficiente para se ler sobre o fundo escuro (luminância >= 0.3).
        for (int i = 0; i < 12 && luminance(accent) < 0.3f; ++i) accent = mix(accent, hex(0xffffff), 0.15f);
        t.onAccent = hex(0x0b0d10);
    } else {
        t.bg = hex(0xf5f6f8);
        t.surface = hex(0xffffff);
        t.raised = hex(0xeceef2);
        t.hover = hex(0xe2e5ea);
        t.border = hex(0xdadee5);
        t.text = hex(0x15171c);
        t.muted = hex(0x4d5562);
        t.faint = hex(0x667080);
        t.danger = hex(0xc0392b);
        // No modo claro o destaque escurece até ter contraste com o branco.
        for (int i = 0; i < 12 && luminance(accent) > 0.18f; ++i) accent = mix(accent, hex(0x000000), 0.15f);
        t.onAccent = hex(0xffffff);
    }
    t.accent = accent;
    return t;
}

// ---------------------------------------------------------------------------

void Ui::begin(ID2D1RenderTarget* rt, Input& input, const Tokens& theme) {
    if (rt != rt_) {
        bitmaps_.clear();
        brush_.Reset();
    }
    rt_ = rt;
    in_ = &input;
    theme_ = theme;
    repaint_ = false;
    sliderReleased_ = false;
    hot_ = 0;
    // Um dropdown aberto "come" o rato: os widgets por baixo dele não reagem.
    blocked_ = openDropdown_ != 0 && contains(lastOverlayRect_, in_->mouse);
    if (!in_->down && !in_->released) active_ = 0;
}

void Ui::end() {
    if (openDropdown_ != 0) {
        const float itemH = 30.0f;
        const int visible = std::min(static_cast<int>(dropdownItems_.size()), 9);
        R list{dropdownRect_.left, dropdownRect_.bottom + 4, dropdownRect_.right,
               dropdownRect_.bottom + 4 + visible * itemH + 8};
        // Se não cabe por baixo, abre por cima.
        const D2D1_SIZE_F size = rt_->GetSize();
        if (list.bottom > size.height - 8) {
            const float h = height(list);
            list = {list.left, dropdownRect_.top - 4 - h, list.right, dropdownRect_.top - 4};
        }
        lastOverlayRect_ = list;
        const bool inside = contains(list, in_->mouse);
        const float maxScroll = std::max(0.0f, dropdownItems_.size() * itemH - (height(list) - 8));
        if (inside && in_->wheel != 0) dropdownScroll_ = std::clamp(dropdownScroll_ - in_->wheel * itemH * 2, 0.0f, maxScroll);

        fill(inset(list, -1, -1), withAlpha(hex(0x000000), theme_.dark ? 0.35f : 0.12f), 10); // sombra simples
        fill(list, theme_.surface, 8);
        stroke(list, theme_.border, 8);
        pushClip(inset(list, 0, 4));
        for (size_t i = 0; i < dropdownItems_.size(); ++i) {
            const R row{list.left + 4, list.top + 4 + i * itemH - dropdownScroll_, list.right - 4,
                        list.top + 4 + (i + 1) * itemH - dropdownScroll_};
            if (row.bottom < list.top || row.top > list.bottom) continue;
            const bool over = contains(row, in_->mouse) && contains(list, in_->mouse);
            const bool sel = dropdownTarget_ && *dropdownTarget_ == static_cast<int>(i);
            if (over) fill(row, theme_.hover, 6);
            if (sel) fill(rect(row.left + 2, row.top + 8, 3, itemH - 16), theme_.accent, 1.5f);
            text(inset(row, 12, 0), dropdownItems_[i], sel ? theme_.text : theme_.muted);
            if (over && in_->pressed) {
                if (dropdownTarget_) *dropdownTarget_ = static_cast<int>(i);
                dropdownChanged_ = true;
                openDropdown_ = 0;
                repaint_ = true;
            }
        }
        popClip();
        if (in_->pressed && !inside && openDropdown_ != 0) {
            openDropdown_ = 0; // clicar fora fecha
            repaint_ = true;
        }
        if (in_->escape) openDropdown_ = 0;
    } else {
        lastOverlayRect_ = {};
    }
    // Clicar fora de um campo de texto tira-lhe o foco.
    if (in_->pressed && hot_ == 0 && !blocked_) focus_ = 0;
}

void Ui::resetDeviceResources() {
    bitmaps_.clear();
    brush_.Reset();
    rt_ = nullptr;
}

ID2D1Brush* Ui::brush(D2D1_COLOR_F c) {
    if (!brush_) rt_->CreateSolidColorBrush(c, &brush_);
    brush_->SetColor(c);
    return brush_.Get();
}

void Ui::fill(const R& r, D2D1_COLOR_F c, float radius) {
    if (radius > 0) rt_->FillRoundedRectangle({r, radius, radius}, brush(c));
    else rt_->FillRectangle(r, brush(c));
}

void Ui::stroke(const R& r, D2D1_COLOR_F c, float radius, float w) {
    const R s = inset(r, w / 2, w / 2); // traço por dentro do retângulo
    if (radius > 0) rt_->DrawRoundedRectangle({s, radius, radius}, brush(c), w);
    else rt_->DrawRectangle(s, brush(c), w);
}

void Ui::line(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_COLOR_F c, float w) { rt_->DrawLine(a, b, brush(c), w); }

void Ui::text(const R& r, std::wstring_view s, D2D1_COLOR_F c, const TextStyle& style) {
    if (s.empty()) return;
    IDWriteTextFormat* f = gfx_.format(style);
    if (!f) return;
    rt_->DrawTextW(s.data(), static_cast<UINT32>(s.size()), f, r, brush(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Ui::icon(const R& r, wchar_t glyph, D2D1_COLOR_F c, float size) {
    TextStyle s;
    s.kind = FontKind::Icon;
    s.size = size;
    s.align = DWRITE_TEXT_ALIGNMENT_CENTER;
    text(r, std::wstring_view(&glyph, 1), c, s);
}

float Ui::textWidth(std::wstring_view s, const TextStyle& style) {
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(gfx_.dwrite()->CreateTextLayout(s.data(), static_cast<UINT32>(s.size()), gfx_.format(style), 10000,
                                               100, &layout))) {
        return 0;
    }
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

ID2D1Bitmap* Ui::bitmap(const std::filesystem::path& path) {
    const std::wstring key = path.wstring();
    if (auto it = bitmaps_.find(key); it != bitmaps_.end()) return it->second.Get();
    ComPtr<ID2D1Bitmap> bmp;
    if (IWICBitmapSource* src = gfx_.image(path)) rt_->CreateBitmapFromWicBitmap(src, nullptr, &bmp);
    bitmaps_[key] = bmp;
    return bmp.Get();
}

void Ui::image(const R& r, const std::filesystem::path& path, float radius, D2D1_COLOR_F placeholder) {
    ID2D1Bitmap* bmp = path.empty() ? nullptr : bitmap(path);
    if (!bmp) {
        fill(r, placeholder.a > 0 ? placeholder : theme_.raised, radius);
        TextStyle s;
        s.kind = FontKind::Mono;
        s.size = std::clamp(height(r) * 0.28f, 10.0f, 28.0f);
        s.align = DWRITE_TEXT_ALIGNMENT_CENTER;
        text(r, L"A_", theme_.faint, s);
        return;
    }
    // "Cover": preencher o retângulo mantendo a proporção, cortando o que sobra.
    const D2D1_SIZE_F bs = bmp->GetSize();
    const float scale = std::max(width(r) / bs.width, height(r) / bs.height);
    const float ox = r.left + (width(r) - bs.width * scale) / 2, oy = r.top + (height(r) - bs.height * scale) / 2;
    ComPtr<ID2D1BitmapBrush> bb;
    D2D1_BITMAP_BRUSH_PROPERTIES props{D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP,
                                       D2D1_BITMAP_INTERPOLATION_MODE_LINEAR};
    if (FAILED(rt_->CreateBitmapBrush(bmp, props, &bb))) return;
    bb->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(ox, oy));
    if (radius > 0) rt_->FillRoundedRectangle({r, radius, radius}, bb.Get());
    else rt_->FillRectangle(r, bb.Get());
}

void Ui::pushClip(const R& r) { rt_->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED); }
void Ui::popClip() { rt_->PopAxisAlignedClip(); }

void Ui::drawFocusRing(const R& r, float radius) { stroke(inset(r, -2, -2), withAlpha(theme_.accent, 0.8f), radius + 2, 1.5f); }

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

bool Ui::clickable(Id id, const R& r) {
    const bool over = mouseInside(r);
    if (over) hot_ = id;
    if (over && in_->pressed) active_ = id;
    const bool clicked = in_->released && active_ == id && over;
    if (clicked) repaint_ = true;
    return clicked;
}

bool Ui::button(Id id, const R& r, std::wstring_view label, ButtonKind kind, wchar_t iconGlyph, bool enabled) {
    const bool over = enabled && mouseInside(r);
    const bool clicked = enabled && clickable(id, r);
    const bool pressing = active_ == id && in_->down;
    D2D1_COLOR_F bgc{}, fg = theme_.text;
    switch (kind) {
    case ButtonKind::Primary:
        bgc = over ? mix(theme_.accent, hex(0xffffff), theme_.dark ? 0.12f : 0.0f) : theme_.accent;
        if (!theme_.dark && over) bgc = mix(theme_.accent, hex(0x000000), 0.12f);
        fg = theme_.onAccent;
        break;
    case ButtonKind::Secondary: bgc = over ? theme_.hover : theme_.raised; break;
    case ButtonKind::Ghost: bgc = over ? theme_.raised : D2D1_COLOR_F{0, 0, 0, 0}; break;
    case ButtonKind::Danger:
        bgc = over ? withAlpha(theme_.danger, 0.14f) : D2D1_COLOR_F{0, 0, 0, 0};
        fg = theme_.danger;
        break;
    }
    if (pressing) bgc = mix(bgc, theme_.dark ? hex(0x000000) : hex(0xffffff), 0.12f);
    if (!enabled) {
        bgc = withAlpha(bgc, 0.5f);
        fg = withAlpha(fg, 0.45f);
    }
    if (bgc.a > 0) fill(r, bgc, 6);
    if (kind == ButtonKind::Secondary) stroke(r, withAlpha(theme_.border, 0.9f), 6);

    TextStyle s;
    s.weight = kind == ButtonKind::Primary ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    s.align = DWRITE_TEXT_ALIGNMENT_CENTER;
    if (iconGlyph && !label.empty()) {
        const float tw = textWidth(label, s);
        const float total = 16 + 8 + tw;
        const float x0 = r.left + (width(r) - total) / 2;
        icon(rect(x0, r.top, 16, height(r)), iconGlyph, fg, 13);
        s.align = DWRITE_TEXT_ALIGNMENT_LEADING;
        text({x0 + 24, r.top, r.right, r.bottom}, label, fg, s);
    } else if (iconGlyph) {
        icon(r, iconGlyph, fg, 14);
    } else {
        text(r, label, fg, s);
    }
    return clicked;
}

bool Ui::iconButton(Id id, const R& r, wchar_t glyph, bool enabled) {
    return button(id, r, L"", ButtonKind::Ghost, glyph, enabled);
}

bool Ui::slider(Id id, const R& r, std::wstring_view label, float& value, float lo, float hi, float step,
                std::wstring_view valueText) {
    // Etiqueta à esquerda, valor (monoespaçado) à direita, trilho por baixo.
    TextStyle ls;
    ls.size = 12.5f;
    text(rect(r.left, r.top, width(r) - 80, 18), label, theme_.muted, ls);
    TextStyle vs;
    vs.kind = FontKind::Mono;
    vs.size = 12.0f;
    vs.align = DWRITE_TEXT_ALIGNMENT_TRAILING;
    text(rect(r.right - 90, r.top, 90, 18), valueText, theme_.text, vs);

    const float trackY = r.top + 28;
    const R track{r.left + 7, trackY - 2, r.right - 7, trackY + 2};
    const R hit{r.left, trackY - 12, r.right, trackY + 12};
    const bool over = mouseInside(hit);
    if (over) hot_ = id;
    if (over && in_->pressed) active_ = id;

    bool changed = false;
    if (active_ == id && (in_->down || in_->released)) {
        float t = (in_->mouse.x - track.left) / std::max(1.0f, width(track));
        t = std::clamp(t, 0.0f, 1.0f);
        float v = lo + t * (hi - lo);
        if (step > 0) v = lo + std::round((v - lo) / step) * step;
        v = std::clamp(v, std::min(lo, hi), std::max(lo, hi));
        if (v != value) {
            value = v;
            changed = true;
        }
        if (in_->released) sliderReleased_ = true;
        repaint_ = true;
    }

    const float t = hi > lo ? std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
    const float x = track.left + t * width(track);
    fill(track, theme_.raised, 2);
    fill({track.left, track.top, x, track.bottom}, theme_.accent, 2);
    const float rad = (over || active_ == id) ? 8.0f : 7.0f;
    rt_->FillEllipse({{x, trackY}, rad, rad}, brush(theme_.dark ? hex(0xf2f4f8) : hex(0xffffff)));
    rt_->DrawEllipse({{x, trackY}, rad, rad}, brush(active_ == id ? theme_.accent : theme_.border), 1.5f);
    return changed;
}

bool Ui::toggle(Id id, const R& r, std::wstring_view label, std::wstring_view description, bool& value) {
    const bool clicked = clickable(id, r);
    if (clicked) value = !value;
    if (mouseInside(r)) fill(r, withAlpha(theme_.raised, 0.6f), 8);
    const bool hasDesc = !description.empty();
    text(rect(r.left + 12, r.top + (hasDesc ? 8 : 0), width(r) - 80, hasDesc ? 20 : height(r)), label, theme_.text);
    if (hasDesc) {
        TextStyle ds;
        ds.size = 12.0f;
        text(rect(r.left + 12, r.top + 28, width(r) - 80, 18), description, theme_.faint, ds);
    }
    const R sw = rect(r.right - 12 - 40, r.top + (height(r) - 22) / 2, 40, 22);
    fill(sw, value ? theme_.accent : theme_.raised, 11);
    if (!value) stroke(sw, theme_.border, 11);
    const float kx = value ? sw.right - 11 : sw.left + 11;
    rt_->FillEllipse({{kx, sw.top + 11}, 7.5f, 7.5f}, brush(value ? theme_.onAccent : theme_.muted));
    return clicked;
}

bool Ui::chip(Id id, const R& r, std::wstring_view label, bool selected) {
    const bool clicked = clickable(id, r);
    const bool over = mouseInside(r);
    const float rad = height(r) / 2;
    if (selected) {
        fill(r, withAlpha(theme_.accent, 0.16f), rad);
        stroke(r, withAlpha(theme_.accent, 0.7f), rad);
    } else {
        fill(r, over ? theme_.hover : theme_.raised, rad);
    }
    TextStyle s;
    s.size = 12.5f;
    s.align = DWRITE_TEXT_ALIGNMENT_CENTER;
    text(r, label, selected ? theme_.text : theme_.muted, s);
    return clicked;
}

bool Ui::swatch(Id id, const R& r, Rgba8 color, bool selected) {
    const bool clicked = clickable(id, r);
    if (selected) stroke(inset(r, -3, -3), theme_.accent, 9, 2);
    else if (mouseInside(r)) stroke(inset(r, -3, -3), theme_.border, 9, 1.5f);
    fill(r, fromRgba(color), 6);
    stroke(r, withAlpha(theme_.dark ? hex(0xffffff) : hex(0x000000), 0.12f), 6);
    return clicked;
}

bool Ui::textField(Id id, const R& r, std::wstring& value, std::wstring_view placeholder) {
    const bool over = mouseInside(r);
    if (over) hot_ = id;
    if (over && in_->pressed) {
        focus_ = id;
        repaint_ = true;
    }
    const bool focused = focus_ == id;
    bool submitted = false;
    if (focused) {
        for (wchar_t c : in_->text) {
            if (c >= 0x20 && value.size() < 64) value += c;
        }
        if (in_->backspace && !value.empty()) value.pop_back();
        if (in_->enter) submitted = true;
        if (in_->escape) focus_ = 0;
    }
    fill(r, theme_.raised, 6);
    stroke(r, focused ? theme_.accent : (over ? theme_.hover : theme_.border), 6, focused ? 1.5f : 1.0f);
    const R inner = inset(r, 10, 0);
    if (value.empty() && !focused) {
        text(inner, placeholder, theme_.faint);
    } else {
        text(inner, value, theme_.text);
        if (focused) {
            const float x = std::min(inner.left + textWidth(value, {}) + 1, inner.right);
            line({x, r.top + 8}, {x, r.bottom - 8}, theme_.accent, 1.5f);
        }
    }
    return submitted;
}

bool Ui::dropdown(Id id, const R& r, const std::vector<std::wstring>& items, int& selected) {
    bool changed = false;
    if (dropdownChanged_ && dropdownTarget_ == &selected) {
        dropdownChanged_ = false;
        changed = true;
    }
    const bool open = openDropdown_ == id;
    if (clickable(id, r)) {
        if (open) {
            openDropdown_ = 0;
        } else {
            openDropdown_ = id;
            dropdownItems_ = items;
            dropdownTarget_ = &selected;
            dropdownScroll_ = std::max(0.0f, (selected - 4) * 30.0f);
        }
    }
    if (openDropdown_ == id) dropdownRect_ = r;
    const bool over = mouseInside(r);
    fill(r, over || open ? theme_.hover : theme_.raised, 6);
    stroke(r, open ? theme_.accent : theme_.border, 6);
    const std::wstring_view current =
        selected >= 0 && selected < static_cast<int>(items.size()) ? std::wstring_view(items[selected]) : L"—";
    text({r.left + 10, r.top, r.right - 30, r.bottom}, current, theme_.text);
    icon({r.right - 30, r.top, r.right - 6, r.bottom}, open ? L'\xE70E' : L'\xE70D', theme_.muted, 10);
    return changed;
}

bool Ui::colorPicker(Id id, const R& r, Rgba8& color) {
    float h, s, v;
    rgbToHsv(color, h, s, v);
    if (s < 0.001f || v < 0.001f) {
        if (auto it = hues_.find(id); it != hues_.end()) h = it->second; // manter a matiz escolhida
    }

    const float barW = 18;
    const R sv{r.left, r.top, r.right - barW - 12, r.top + 132};
    const R bar{r.right - barW, r.top, r.right, r.top + 132};
    const Id svId = makeId(id, 1), hueId = makeId(id, 2), hexId = makeId(id, 3);

    bool changed = false;
    auto drag = [&](Id part, const R& area) {
        const bool over = mouseInside(area);
        if (over) hot_ = part;
        if (over && in_->pressed) active_ = part;
        return active_ == part && (in_->down || in_->released);
    };
    if (drag(svId, sv)) {
        s = std::clamp((in_->mouse.x - sv.left) / width(sv), 0.0f, 1.0f);
        v = 1.0f - std::clamp((in_->mouse.y - sv.top) / height(sv), 0.0f, 1.0f);
        changed = true;
        if (in_->released) sliderReleased_ = true;
    }
    if (drag(hueId, bar)) {
        h = std::clamp((in_->mouse.y - bar.top) / height(bar), 0.0f, 0.999f);
        changed = true;
        if (in_->released) sliderReleased_ = true;
    }
    hues_[id] = h;

    // Quadrado saturação/valor: cor pura + gradiente branco->transparente + transparente->preto.
    const Rgba8 pure = hsvToRgb(h, 1, 1, 255);
    fill(sv, fromRgba(pure), 6);
    auto gradient = [&](const R& area, std::initializer_list<D2D1_GRADIENT_STOP> stops, bool vertical) {
        ComPtr<ID2D1GradientStopCollection> col;
        std::vector<D2D1_GRADIENT_STOP> v(stops);
        rt_->CreateGradientStopCollection(v.data(), static_cast<UINT32>(v.size()), &col);
        ComPtr<ID2D1LinearGradientBrush> b;
        const D2D1_POINT_2F a{area.left, area.top};
        const D2D1_POINT_2F z = vertical ? D2D1_POINT_2F{area.left, area.bottom} : D2D1_POINT_2F{area.right, area.top};
        rt_->CreateLinearGradientBrush({a, z}, col.Get(), &b);
        rt_->FillRoundedRectangle({area, 6, 6}, b.Get());
    };
    gradient(sv, {{0, {1, 1, 1, 1}}, {1, {1, 1, 1, 0}}}, false);
    gradient(sv, {{0, {0, 0, 0, 0}}, {1, {0, 0, 0, 1}}}, true);
    gradient(bar,
             {{0.0f, {1, 0, 0, 1}}, {1 / 6.0f, {1, 1, 0, 1}}, {2 / 6.0f, {0, 1, 0, 1}}, {3 / 6.0f, {0, 1, 1, 1}},
              {4 / 6.0f, {0, 0, 1, 1}}, {5 / 6.0f, {1, 0, 1, 1}}, {1.0f, {1, 0, 0, 1}}},
             true);
    const D2D1_POINT_2F mark{sv.left + s * width(sv), sv.top + (1 - v) * height(sv)};
    rt_->DrawEllipse({mark, 6, 6}, brush(hex(0xffffff)), 2);
    rt_->DrawEllipse({mark, 7.5f, 7.5f}, brush(hex(0x000000, 0.4f)), 1);
    const float hy = bar.top + h * height(bar);
    stroke({bar.left - 2, hy - 3, bar.right + 2, hy + 3}, hex(0xffffff), 3, 2);

    if (changed) color = hsvToRgb(h, s, v, color.a);

    // Hex: mostra a cor atual; ao escrever e carregar Enter aplica.
    std::wstring& edit = hexEdits_[hexId];
    if (focus_ != hexId) edit = toHex(color);
    const R field{r.left, r.top + 144, r.left + 120, r.top + 176};
    if (textField(hexId, field, edit, L"#rrggbb")) {
        std::string narrow;
        for (wchar_t ch : edit) narrow += ch < 0x80 ? static_cast<char>(ch) : '?';
        if (auto c = parseColor(narrow)) {
            color = *c;
            changed = true;
            sliderReleased_ = true;
            focus_ = 0;
        }
    }
    TextStyle ms;
    ms.kind = FontKind::Mono;
    ms.size = 11.5f;
    text({field.right + 12, field.top, r.right, field.bottom},
         std::format(L"H {:.0f}°  S {:.0f}%  V {:.0f}%", h * 360, s * 100, v * 100), theme_.faint, ms);
    if (changed) repaint_ = true;
    return changed;
}

float Ui::scrollArea(Id id, const R& viewport, float contentHeight) {
    float& off = scroll_[id];
    const float maxOff = std::max(0.0f, contentHeight - height(viewport));
    if (mouseInside(viewport) && in_->wheel != 0 && openDropdown_ == 0) {
        off -= in_->wheel * 56.0f;
        repaint_ = true;
    }
    off = std::clamp(off, 0.0f, maxOff);
    if (maxOff > 0) {
        const float h = height(viewport) * height(viewport) / contentHeight;
        const float y = viewport.top + (height(viewport) - h) * (off / maxOff);
        fill(rect(viewport.right - 5, y + 2, 3, h - 4), withAlpha(theme_.muted, 0.35f), 1.5f);
    }
    return off;
}

} // namespace ap::ui
