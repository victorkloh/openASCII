#include "ui/UiWindow.h"

#include "util/Log.h"
#include "util/Win32.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace ap::ui {

namespace {

float lum(Rgba8 c) {
    auto ch = [](uint8_t v) {
        const float x = v / 255.0f;
        return x <= 0.03928f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
    };
    return 0.2126f * ch(c.r) + 0.7152f * ch(c.g) + 0.0722f * ch(c.b);
}

float saturation(Rgba8 c) {
    const int mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b});
    return mx == 0 ? 0.0f : (mx - mn) / static_cast<float>(mx);
}

} // namespace

Rgba8 accentFromPalette(const std::vector<Rgba8>& palette) {
    // A cor mais "viva" da paleta: saturada e não demasiado escura. Os fundos e os brancos ficam de fora.
    Rgba8 best{92, 200, 240, 255};
    float bestScore = -1;
    for (const Rgba8& c : palette) {
        const float l = lum(c), s = saturation(c);
        if (l < 0.04f || s < 0.25f) continue;
        const float score = s * 0.7f + std::min(l, 0.5f) * 0.6f;
        if (score > bestScore) {
            bestScore = score;
            best = c;
        }
    }
    return best;
}

bool systemUsesDarkTheme() {
    DWORD value = 1, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
        return true;
    }
    return value == 0;
}

UiWindow::~UiWindow() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
}

bool UiWindow::createWindow(HINSTANCE instance, const wchar_t* className, const wchar_t* title, DWORD style,
                            DWORD exStyle, int widthDip, int heightDip, UINT classStyle) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = classStyle;
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    wc.lpszClassName = className;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    // Tamanho em DIPs convertido para o DPI do monitor principal (a janela ajusta-se depois).
    const UINT dpi = GetDpiForSystem();
    const int w = MulDiv(widthDip, static_cast<int>(dpi), 96), h = MulDiv(heightDip, static_cast<int>(dpi), 96);
    hwnd_ = CreateWindowExW(exStyle, className, title, style, CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr,
                            instance, this);
    if (!hwnd_) return false;
    applyWindowTheme();
    return true;
}

void UiWindow::applyWindowTheme() {
    if (!hwnd_) return;
    const Tokens t = theme();
    const BOOL dark = t.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    // Barra de título da mesma cor do fundo: a janela parece uma peça só (Windows 11).
    const COLORREF caption = RGB(static_cast<int>(t.bg.r * 255), static_cast<int>(t.bg.g * 255),
                                 static_cast<int>(t.bg.b * 255));
    DwmSetWindowAttribute(hwnd_, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
    const COLORREF border = RGB(static_cast<int>(t.border.r * 255), static_cast<int>(t.border.g * 255),
                                static_cast<int>(t.border.b * 255));
    DwmSetWindowAttribute(hwnd_, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
    const int round = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd_, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &round, sizeof(round));
}

float UiWindow::dpiScale() const { return hwnd_ ? GetDpiForWindow(hwnd_) / 96.0f : 1.0f; }

void UiWindow::invalidate() const {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

D2D1_POINT_2F UiWindow::toDip(LPARAM lp) const {
    const float s = dpiScale();
    return {GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s};
}

namespace {
// Diagnóstico: quanto tempo a interface ocupa o thread de UI (que também desenha o wallpaper).
int g_paints = 0;
double g_paintMs = 0, g_paintMaxMs = 0;
LONGLONG qpcTicks() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return li.QuadPart;
}
double qpcMs(LONGLONG ticks) {
    static const double freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    return ticks * 1000.0 / freq;
}
} // namespace

constexpr double kMinPaintIntervalMs = 14.0; // ~70 pinturas/s no máximo
constexpr UINT_PTR kPaintTimerId = 0xA5C1;

UiPaintStats takeUiPaintStats() {
    UiPaintStats s{g_paints, g_paintMs, g_paintMaxMs};
    g_paints = 0;
    g_paintMs = g_paintMaxMs = 0;
    return s;
}

void UiWindow::paint() {
    const LONGLONG paintStart = qpcTicks();
    struct Measure {
        LONGLONG start;
        ~Measure() {
            const double ms = qpcMs(qpcTicks() - start);
            ++g_paints;
            g_paintMs += ms;
            g_paintMaxMs = std::max(g_paintMaxMs, ms);
        }
    } measure{paintStart};
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    const float dpi = static_cast<float>(GetDpiForWindow(hwnd_));
    if (!rt_) {
        const auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            dpi, dpi);
        // IMMEDIATELY: sem isto, cada EndDraw espera pelo vsync (até 16 ms) e, como este thread
        // também desenha o wallpaper, mexer o rato sobre a interface fazia o fundo perder FPS.
        const auto hwndProps = D2D1::HwndRenderTargetProperties(
            hwnd_, D2D1::SizeU(static_cast<UINT32>(rc.right), static_cast<UINT32>(rc.bottom)),
            D2D1_PRESENT_OPTIONS_IMMEDIATELY);
        if (FAILED(gfx_.d2d()->CreateHwndRenderTarget(props, hwndProps, &rt_))) return;
        rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        ui_.resetDeviceResources();
    }
    rt_->SetDpi(dpi, dpi);
    const Tokens t = theme();
    const bool hadEvent = input_.pressed || input_.released || input_.rightReleased || input_.wheel != 0 || !input_.text.empty() ||
                          input_.backspace || input_.enter || input_.escape;
    rt_->BeginDraw();
    rt_->Clear(t.bg);
    ui_.begin(rt_.Get(), input_, t);
    const D2D1_SIZE_F size = rt_->GetSize();
    build(ui_, size.width, size.height);
    ui_.end();
    const HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        rt_.Reset();
        ui_.resetDeviceResources();
        invalidate();
    }
    input_.clearTransient();
    // Previews ainda a descodificar noutro thread: voltar a pintar daqui a pouco.
    // Previews ainda a descodificar ou um wallpaper a compilar (o indicador anima): repintar daqui a pouco.
    const bool animating = !app_.loadingPath().empty();
    if ((gfx_.imagesPending() || animating) && !paintDeferred_) {
        paintDeferred_ = true;
        SetTimer(hwnd_, kPaintTimerId, animating ? 110 : 30, nullptr);
    }
    // Um clique pode mudar o estado depois de a parte que o mostra já estar desenhada:
    // uma repintura extra (sem input) acerta tudo. Só uma, para não entrar em ciclo.
    if ((hadEvent || ui_.repaintRequested()) && !followUp_) {
        followUp_ = true;
        invalidate();
    } else {
        followUp_ = false;
    }
}

HRESULT UiWindow::renderToPngImpl(const std::wstring& path, float widthDip, float heightDip, float scale) {
    const UINT w = static_cast<UINT>(widthDip * scale), h = static_cast<UINT>(heightDip * scale);
    ComPtr<IWICBitmap> bitmap;
    HRESULT hr = gfx_.wic()->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
    if (FAILED(hr)) return hr;
    ComPtr<ID2D1RenderTarget> rt;
    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f * scale, 96.0f * scale);
    if (FAILED(hr = gfx_.d2d()->CreateWicBitmapRenderTarget(bitmap.Get(), props, &rt))) return hr;
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    const Tokens t = theme();
    Input none;
    rt->BeginDraw();
    rt->Clear(t.bg);
    ui_.setSynchronousImages(true);
    ui_.begin(rt.Get(), none, t);
    build(ui_, widthDip, heightDip);
    ui_.end();
    ui_.setSynchronousImages(false);
    if (FAILED(hr = rt->EndDraw())) return hr;
    ui_.resetDeviceResources();

    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    if (SUCCEEDED(hr)) hr = gfx_.wic()->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = gfx_.wic()->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(w, h);
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr)) hr = frame->WriteSource(bitmap.Get(), nullptr);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    return hr;
}

LRESULT CALLBACK UiWindow::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        reinterpret_cast<UiWindow*>(cs->lpCreateParams)->hwnd_ = hwnd;
    }
    auto* self = reinterpret_cast<UiWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->onMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT UiWindow::onMessage(UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT result = 0;
    if (handleMessage(msg, wp, lp, result)) return result;

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd_, &ps);
        const double sinceLast = qpcMs(qpcTicks() - lastPaintQpc_);
        if (sinceLast < kMinPaintIntervalMs) {
            // Cedo demais: dar a janela por pintada agora e pintar quando o intervalo acabar.
            if (!paintDeferred_) {
                paintDeferred_ = true;
                SetTimer(hwnd_, kPaintTimerId, static_cast<UINT>(std::ceil(kMinPaintIntervalMs - sinceLast)), nullptr);
            }
        } else {
            paint();
            lastPaintQpc_ = qpcTicks();
        }
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_TIMER:
        if (wp != kPaintTimerId) break;
        KillTimer(hwnd_, kPaintTimerId);
        paintDeferred_ = false;
        invalidate();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCLBUTTONDOWN:
        if (wp == HTMINBUTTON || wp == HTMAXBUTTON || wp == HTCLOSE) {
            // O DefWindowProc seguiria o botão num loop que só lê mensagens do rato: nem timers
            // nem mensagens publicadas passam, e o fundo congelava ~100 ms por clique (medido).
            // Fazemos como o Windows, mas sem loop: a ação acontece ao largar no mesmo botão.
            captionPressed_ = static_cast<int>(wp);
            return 0;
        }
        [[fallthrough]];
    case WM_NCRBUTTONDOWN:
    case WM_SYSCOMMAND: {
        // Arrastar, redimensionar e o menu de sistema correm um loop modal do Windows DENTRO
        // do DefWindowProc (medido: 0,6 a 1,3 s a arrastar o launcher), e o nosso loop
        // principal fica parado. Enquanto durar, a app desenha o wallpaper por outro caminho.
        // Pode aninhar (o clique na barra de título gera um WM_SYSCOMMAND SC_MOVE): a app
        // conta as entradas.
        // Fechar pelo X ou Alt+F4 passa por SC_CLOSE; um WM_CLOSE sem isto vem de fora (taskkill).
        const bool sysClose = msg == WM_SYSCOMMAND && (wp & 0xFFF0) == SC_CLOSE;
        if (sysClose) closingByUser_ = true;
        app_.setModalLoop(true);
        const LRESULT r = DefWindowProcW(hwnd_, msg, wp, lp);
        app_.setModalLoop(false);
        if (sysClose) closingByUser_ = false;
        return r;
    }
    case WM_NCLBUTTONUP:
        if (captionPressed_ != 0) {
            const int pressed = std::exchange(captionPressed_, 0);
            if (static_cast<int>(wp) == pressed) {
                const WPARAM cmd = pressed == HTMINBUTTON ? SC_MINIMIZE
                                   : pressed == HTCLOSE   ? SC_CLOSE
                                   : IsZoomed(hwnd_)      ? SC_RESTORE
                                                          : SC_MAXIMIZE;
                PostMessageW(hwnd_, WM_SYSCOMMAND, cmd, 0);
            }
            return 0;
        }
        break;
    case WM_SIZE:
        if (rt_) rt_->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
        invalidate();
        return 0;
    case WM_DPICHANGED: {
        // Janela de topo: o Windows sugere o novo retângulo para o DPI do outro monitor.
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        invalidate();
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (lp && std::wstring_view(reinterpret_cast<const wchar_t*>(lp)) == L"ImmersiveColorSet") {
            applyWindowTheme(); // o utilizador mudou entre tema claro e escuro
            invalidate();
        }
        break;
    case WM_MOUSEMOVE:
        input_.mouse = toDip(lp);
        if (!trackingMouse_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);
            trackingMouse_ = true;
        }
        invalidate();
        return 0;
    case WM_MOUSELEAVE:
        trackingMouse_ = false;
        if (!input_.down) input_.mouse = {-1e6f, -1e6f};
        invalidate();
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SetCapture(hwnd_);
        input_.mouse = toDip(lp);
        input_.down = true;
        input_.pressed = true;
        invalidate();
        return 0;
    case WM_LBUTTONUP:
        captionPressed_ = 0; // carregou num botão da barra de título e largou dentro da janela
        ReleaseCapture();
        input_.mouse = toDip(lp);
        input_.down = false;
        input_.released = true;
        invalidate();
        return 0;
    case WM_RBUTTONUP:
        input_.mouse = toDip(lp);
        input_.rightReleased = true;
        invalidate();
        return 0;
    case WM_MOUSEWHEEL:
        input_.wheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
        invalidate();
        return 0;
    case WM_CHAR:
        if (wp >= 0x20) {
            input_.text += static_cast<wchar_t>(wp);
            invalidate();
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_BACK) input_.backspace = true;
        else if (wp == VK_RETURN) input_.enter = true;
        else if (wp == VK_ESCAPE) input_.escape = true;
        else break;
        invalidate();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

} // namespace ap::ui
