#include "inputs/InputHub.h"

#include "core/Utf8.h"
#include "render/GlyphAtlas.h"
#include "util/Log.h"
#include "util/Win32.h"

#include <dwmapi.h>
#include <exdisp.h>
#include <servprov.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>

namespace ap::inputs {

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace {

constexpr size_t kMaxTextChars = 65536; // 256 KB na GPU no máximo
constexpr UINT kMaxObstacles = 256;

std::wstring toWide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(std::max(n, 0)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring expandEnv(const std::wstring& s) {
    wchar_t buf[2048];
    const DWORD n = ExpandEnvironmentStringsW(s.c_str(), buf, static_cast<DWORD>(std::size(buf)));
    return n > 0 && n <= std::size(buf) ? std::wstring(buf) : s;
}

// Caminho do wallpaper.toml: relativo à pasta do wallpaper, com %VARIAVEIS% expandidas.
fs::path resolvePath(const std::string& spec, const fs::path& dir) {
    fs::path p(expandEnv(toWide(spec)));
    return p.is_absolute() ? p : dir / p;
}

std::string readFile(const fs::path& p, size_t maxBytes) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::string s(maxBytes, '\0');
    in.read(s.data(), static_cast<std::streamsize>(maxBytes));
    s.resize(static_cast<size_t>(in.gcount()));
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3); // BOM
    }
    return s;
}

// Para a GPU só existe Latin-1 (o que o atlas tem sempre): o resto vira '?'. Tabs viram
// espaços e as mudanças de linha ficam como '\n' (o shader decide o que fazer com elas).
std::u32string toLatin1Text(std::string_view utf8) {
    std::u32string out;
    for (char32_t c : decodeUtf8(utf8)) {
        if (c == U'\r') continue;
        if (c == U'\t') {
            out += U"    ";
            continue;
        }
        if (c != U'\n' && (c < 0x20 || (c >= 0x7F && c < 0xA0))) continue;
        out += c <= 0xFF ? c : U'?';
        if (out.size() >= kMaxTextChars) break;
    }
    return out;
}

bool isCodeFile(const fs::path& p) {
    static const wchar_t* exts[] = {L".c",  L".cc", L".cpp", L".h",  L".hpp", L".hlsl", L".glsl", L".cs",
                                    L".py", L".js", L".ts",  L".rs", L".go",  L".java", L".lua",  L".kt",
                                    L".rb", L".php", L".swift", L".toml", L".json", L".md", L".txt"};
    const std::wstring e = p.extension().wstring();
    return std::any_of(std::begin(exts), std::end(exts), [&](const wchar_t* x) { return _wcsicmp(e.c_str(), x) == 0; });
}

// O código de uma pasta (até 4 níveis), ficheiro a ficheiro, até ao limite.
std::u32string readFolder(const fs::path& dir) {
    std::vector<fs::path> files;
    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        if (it.depth() > 4) {
            it.disable_recursion_pending();
            continue;
        }
        const fs::path& p = it->path();
        const std::wstring name = p.filename().wstring();
        if (it->is_directory(ec) && (name.starts_with(L".") || name == L"node_modules" || name == L"build" ||
                                      name == L"vcpkg_installed" || name == L"bin" || name == L"obj")) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && isCodeFile(p) && it->file_size(ec) < 512 * 1024) files.push_back(p);
        if (files.size() > 400) break;
    }
    std::sort(files.begin(), files.end());
    std::u32string out;
    for (const auto& f : files) {
        out += toLatin1Text(readFile(f, 64 * 1024));
        out += U"\n\n";
        if (out.size() >= kMaxTextChars) break;
    }
    if (out.size() > kMaxTextChars) out.resize(kMaxTextChars);
    return out;
}

// Retângulo visível de uma janela (sem as margens invisíveis de redimensionar do Windows 10/11).
bool windowRect(HWND h, RECT& r) {
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) return true;
    return GetWindowRect(h, &r) != FALSE;
}

struct WindowScan {
    std::vector<RECT>* out;
    HWND stopAt; // --windowed: só contam as janelas à frente da janela de desenvolvimento
};

BOOL CALLBACK collectWindow(HWND h, LPARAM lp) {
    const auto& scan = *reinterpret_cast<WindowScan*>(lp);
    auto& out = *scan.out;
    if (h == scan.stopAt || out.size() >= kMaxObstacles / 2) return FALSE; // EnumWindows vai de cima para baixo
    if (!IsWindowVisible(h) || IsIconic(h)) return TRUE;
    BOOL cloaked = FALSE; // janelas de outros ambientes de trabalho virtuais, apps UWP suspensas
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
    const LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TRANSPARENT) return TRUE; // sobreposições que deixam passar o rato (overlays)
    const std::wstring cls = win::className(h);
    if (cls == L"Progman" || cls == L"WorkerW" || cls == L"OpenASCIIController") return TRUE;
    RECT r{};
    if (!windowRect(h, r) || r.right - r.left < 32 || r.bottom - r.top < 16) return TRUE;
    out.push_back(r);
    return TRUE;
}

} // namespace

// ---------------------------------------------------------------------------
// Ícones do ambiente de trabalho
// ---------------------------------------------------------------------------

void DesktopIcons::start() {
    if (thread_.joinable()) return;
    stop_ = false;
    thread_ = std::thread(&DesktopIcons::run, this);
}

void DesktopIcons::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    std::lock_guard lock(mutex_);
    rects_.clear();
}

std::vector<RECT> DesktopIcons::rects() {
    std::lock_guard lock(mutex_);
    return rects_;
}

void DesktopIcons::run() {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (!stop_) {
        std::vector<RECT> found;
        // IShellWindows -> a janela do desktop -> IFolderView: posição e tamanho de cada ícone.
        ComPtr<IShellWindows> windows;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows)))) {
            VARIANT loc{}, empty{};
            loc.vt = VT_I4;
            loc.lVal = CSIDL_DESKTOP;
            long hwnd = 0;
            ComPtr<IDispatch> disp;
            ComPtr<IServiceProvider> sp;
            ComPtr<IShellBrowser> browser;
            ComPtr<IShellView> view;
            ComPtr<IFolderView> folder;
            ComPtr<IEnumIDList> items;
            HWND defView = nullptr;
            if (windows->FindWindowSW(&loc, &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &disp) == S_OK && disp &&
                SUCCEEDED(disp.As(&sp)) &&
                SUCCEEDED(sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))) &&
                SUCCEEDED(browser->QueryActiveShellView(&view)) && SUCCEEDED(view.As(&folder)) &&
                SUCCEEDED(view->GetWindow(&defView)) && SUCCEEDED(folder->Items(SVGIO_ALLVIEW, IID_PPV_ARGS(&items)))) {
                HWND list = FindWindowExW(defView, nullptr, L"SysListView32", nullptr);
                POINT spacing{};
                folder->GetSpacing(&spacing);
                PITEMID_CHILD pidl = nullptr;
                while (!stop_ && items->Next(1, &pidl, nullptr) == S_OK && found.size() < kMaxObstacles / 2) {
                    POINT pt{};
                    if (SUCCEEDED(folder->GetItemPosition(pidl, &pt))) {
                        ClientToScreen(list ? list : defView, &pt);
                        // O espaçamento inclui a margem entre ícones: o ícone ocupa ~80% dele.
                        const LONG w = spacing.x * 8 / 10, hgt = spacing.y * 8 / 10;
                        const LONG ox = (spacing.x - w) / 2;
                        found.push_back({pt.x + ox, pt.y, pt.x + ox + w, pt.y + hgt});
                    }
                    CoTaskMemFree(pidl);
                }
            }
        }
        {
            std::lock_guard lock(mutex_);
            rects_ = std::move(found);
        }
        for (int i = 0; i < 20 && !stop_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (SUCCEEDED(init)) CoUninitialize();
}

// ---------------------------------------------------------------------------
// InputHub
// ---------------------------------------------------------------------------

void InputHub::configure(const Manifest& m, const fs::path& dir, const InputSettings& s, IWICImagingFactory* wic) {
    manifest_ = m;
    configured_ = true;

    // Áudio, ícones, tempo: threads que só existem enquanto o wallpaper os usa.
    if (m.useAudio) audio_.start();
    else audio_.stop();
    if (m.useIcons) desktopIcons_.start();
    else desktopIcons_.stop();
    if (m.useWeather) weather_.start(s.weatherCity); // cidade vazia = parado, sem rede
    else weather_.stop();
    levels_ = {};
    weatherNow_ = {};
    windowsTime_ = systemTime_ = weatherTime_ = -10.0;

    // Imagem / vídeo / webcam: só reabre se mudou.
    std::wstring spec;
    webcamConsentNeeded_ = false;
    if (!m.source.empty()) {
        if (_stricmp(m.source.c_str(), "webcam") == 0) {
            if (s.webcamAllowed) spec = L"webcam";
            else webcamConsentNeeded_ = true;
        } else if (_stricmp(m.source.c_str(), "wallpaper") == 0) {
            // O wallpaper do Windows que o OpenASCII substituiu; se for uma cor lisa (sem
            // imagem), a imagem que vem com o próprio Windows.
            std::error_code ec;
            spec = s.windowsWallpaper;
            if (spec.empty() || !fs::exists(spec, ec)) spec = expandEnv(L"%WINDIR%\\Web\\Wallpaper\\Windows\\img0.jpg");
        } else {
            spec = resolvePath(m.source, dir).wstring();
        }
    }
    if (spec != sourceSpec_) {
        sourceSpec_ = spec;
        if (spec.empty()) {
            source_.close();
        } else if (!source_.open(spec, wic)) {
            log::warn(L"[inputs] source: não consegui abrir \"{}\"", spec);
            // Esquecer: a próxima configuração (recarregar, mudar ajustes) tenta outra vez,
            // em vez de ficar para sempre sem imagem por o ficheiro ter falhado uma vez.
            sourceSpec_.clear();
        }
    }

    loadText(m.text, dir, m.textInterval);
}

void InputHub::stop() {
    audio_.stop();
    desktopIcons_.stop();
    weather_.stop();
    source_.close();
    sourceSpec_.clear();
    configured_ = false;
}

void InputHub::setPaused(bool paused) { source_.setPaused(paused); }

void InputHub::loadText(const std::string& spec, const fs::path& dir, float interval) {
    std::u32string text;
    lines_.clear();
    lineIndex_ = -1;
    lineInterval_ = std::max(1.0f, interval);
    if (spec.rfind("file:", 0) == 0) {
        const fs::path p = resolvePath(spec.substr(5), dir);
        text = toLatin1Text(readFile(p, kMaxTextChars * 2));
        if (text.empty()) log::warn(L"[inputs] text: \"{}\" está vazio ou não existe", p.wstring());
    } else if (spec.rfind("folder:", 0) == 0) {
        const fs::path p = resolvePath(spec.substr(7), dir);
        text = readFolder(p);
        if (text.empty()) log::warn(L"[inputs] text: não encontrei código em \"{}\"", p.wstring());
    } else if (spec.rfind("lines:", 0) == 0) {
        const fs::path p = resolvePath(spec.substr(6), dir);
        std::istringstream in(readFile(p, kMaxTextChars * 2));
        std::string line;
        while (std::getline(in, line)) {
            std::u32string l = toLatin1Text(line);
            while (!l.empty() && l.back() == U' ') l.pop_back();
            if (!l.empty() && l.front() != U'#') lines_.push_back(std::move(l)); // '#' = comentário
        }
        if (lines_.empty()) log::warn(L"[inputs] text: \"{}\" não tem linhas", p.wstring());
    } else {
        text = toLatin1Text(spec);
    }
    if (lines_.empty()) {
        text_ = std::move(text);
        textInfo_[0] = textInfo_[1] = 0;
        textInfo_[2] = 1;
    }
    textInfo_[3] = lineInterval_;
    textDirty_ = true;
}

void InputHub::refreshWindows() {
    windows_.clear();
    WindowScan scan{&windows_, stopAt_};
    if (manifest_.useWindows) EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&scan));
    icons_ = manifest_.useIcons ? desktopIcons_.rects() : std::vector<RECT>{};
}

void InputHub::update(ID3D11Device* device, ID3D11DeviceContext* ctx, double t, const render::GlyphAtlas& atlas) {
    if (!configured_) return;
    if ((manifest_.useWindows || manifest_.useIcons) && (t - windowsTime_ > 0.05 || t < windowsTime_)) {
        windowsTime_ = t;
        refreshWindows();
    }
    if (audio_.running()) levels_ = audio_.levels();
    if (manifest_.useSystem && (t - systemTime_ >= 1.0 || t < systemTime_)) {
        systemTime_ = t;
        system_.update();
    }
    if (manifest_.useWeather && (t - weatherTime_ >= 5.0 || t < weatherTime_)) {
        weatherTime_ = t;
        weatherNow_ = weather_.now();
    }
    if (!sourceSpec_.empty()) source_.update(device, ctx, t);

    // "lines:": uma linha de cada vez, a rodar.
    if (!lines_.empty()) {
        const int idx = static_cast<int>(std::fmod(t / lineInterval_, static_cast<double>(lines_.size())));
        if (idx != lineIndex_) {
            lineIndex_ = idx;
            lineChanged_ = t;
            text_ = lines_[static_cast<size_t>(idx)];
            textDirty_ = true;
        }
        textInfo_[0] = static_cast<float>(t - lineChanged_);
        textInfo_[1] = static_cast<float>(idx);
        textInfo_[2] = static_cast<float>(lines_.size());
    }
    if (atlas.valid() && (textDirty_ || atlas.key() != textAtlasKey_ || !textSrv_)) uploadText(device, ctx, atlas);
}

void InputHub::uploadText(ID3D11Device* device, ID3D11DeviceContext* ctx, const render::GlyphAtlas& atlas) {
    // [0..255]: slot do atlas de cada caractere Latin-1 (controlo -> espaço); [256..]: o texto.
    std::vector<uint32_t> data(256 + std::max<size_t>(text_.size(), 1), 0);
    const uint32_t space = atlas.slot(U' ');
    for (char32_t c = 0; c < 256; ++c) {
        const bool printable = c >= 0x20 && !(c >= 0x7F && c < 0xA1);
        data[c] = printable ? atlas.slot(c) : space;
    }
    for (size_t i = 0; i < text_.size(); ++i) data[256 + i] = static_cast<uint32_t>(text_[i]);
    if (text_.empty()) data[256] = 32u;

    const UINT needed = static_cast<UINT>(data.size());
    if (!textBuffer_ || needed > textCapacity_) {
        textBuffer_.Reset();
        textSrv_.Reset();
        textCapacity_ = std::max<UINT>(needed, 1024);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = textCapacity_ * sizeof(uint32_t);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(uint32_t);
        if (FAILED(device->CreateBuffer(&bd, nullptr, &textBuffer_)) ||
            FAILED(device->CreateShaderResourceView(textBuffer_.Get(), nullptr, &textSrv_))) {
            textBuffer_.Reset();
            textSrv_.Reset();
            textCapacity_ = 0;
            return;
        }
    }
    const D3D11_BOX box{0, 0, 0, needed * static_cast<UINT>(sizeof(uint32_t)), 1, 1};
    ctx->UpdateSubresource(textBuffer_.Get(), 0, &box, data.data(), 0, 0);
    textAtlasKey_ = atlas.key();
    textDirty_ = false;
}

wallpaper::ShaderInputs InputHub::bind(ID3D11Device* device, ID3D11DeviceContext* ctx, const void* key,
                                       const CanvasLayout& c, wallpaper::FrameConstants& f) {
    wallpaper::ShaderInputs in;
    if (!sampler_) {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        device->CreateSamplerState(&sd, &sampler_);
    }
    in.sampler = sampler_.Get();
    in.text = textSrv_.Get();
    f.textLength = static_cast<uint32_t>(lines_.empty() && text_.empty() ? 0 : text_.size());
    std::copy(std::begin(textInfo_), std::end(textInfo_), f.textInfo);

    // Obstáculos: janelas (por ordem de cima para baixo) e depois ícones, em células deste canvas.
    if (manifest_.useWindows || manifest_.useIcons) {
        std::vector<int32_t> rects;
        uint32_t windows = 0;
        auto add = [&](const RECT& r) {
            const int x0 = static_cast<int>(std::floor(static_cast<double>(r.left - c.originPx.x) / c.cellW));
            const int y0 = static_cast<int>(std::floor(static_cast<double>(r.top - c.originPx.y) / c.cellH));
            const int x1 = static_cast<int>(std::ceil(static_cast<double>(r.right - c.originPx.x) / c.cellW));
            const int y1 = static_cast<int>(std::ceil(static_cast<double>(r.bottom - c.originPx.y) / c.cellH));
            if (x1 <= 0 || y1 <= 0 || x0 >= c.cols || y0 >= c.rows || rects.size() / 4 >= kMaxObstacles) return false;
            rects.insert(rects.end(), {std::max(x0, 0), y0, std::min(x1, c.cols), std::min(y1, c.rows)});
            return true;
        };
        for (const RECT& r : windows_) windows += add(r) ? 1u : 0u;
        for (const RECT& r : icons_) add(r);
        ObstacleBuffer& b = obstacles_[key];
        if (!b.buffer) {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = kMaxObstacles * 16;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bd.StructureByteStride = 16;
            if (FAILED(device->CreateBuffer(&bd, nullptr, &b.buffer)) ||
                FAILED(device->CreateShaderResourceView(b.buffer.Get(), nullptr, &b.srv))) {
                b = {};
            }
        }
        if (b.buffer && !rects.empty()) {
            const D3D11_BOX box{0, 0, 0, static_cast<UINT>(rects.size() * sizeof(int32_t)), 1, 1};
            ctx->UpdateSubresource(b.buffer.Get(), 0, &box, rects.data(), 0, 0);
        }
        in.obstacles = b.srv.Get();
        f.obstacleCount = b.buffer ? static_cast<uint32_t>(rects.size() / 4) : 0;
        f.windowCount = b.buffer ? windows : 0;
    }

    if (audio_.running()) {
        std::copy(levels_.spectrum.begin(), levels_.spectrum.end(), f.spectrum);
        f.audioLevels[0] = levels_.bass;
        f.audioLevels[1] = levels_.mid;
        f.audioLevels[2] = levels_.treble;
        f.audioLevels[3] = levels_.beat;
    }
    if (manifest_.useSystem) {
        f.systemStats[0] = system_.cpu();
        f.systemStats[1] = system_.ram();
    }
    if (manifest_.useWeather && weatherNow_.valid) {
        f.weather[0] = weatherNow_.rainMm;
        f.weather[1] = weatherNow_.cloud;
        f.weather[2] = weatherNow_.windMs;
        f.weather[3] = weatherNow_.temperature;
        f.weather2[0] = weatherNow_.isDay ? 1.0f : 0.0f;
        f.weather2[1] = weatherNow_.sunElevation;
        f.weather2[2] = 1.0f;
        f.weather2[3] = static_cast<float>(weatherNow_.code);
    }
    if (source_.srv()) {
        in.source = source_.srv();
        f.sourceW = static_cast<float>(source_.width());
        f.sourceH = static_cast<float>(source_.height());
    }
    return in;
}

void InputHub::releaseDeviceObjects() {
    obstacles_.clear();
    textBuffer_.Reset();
    textSrv_.Reset();
    textCapacity_ = 0;
    textAtlasKey_.clear();
    sampler_.Reset();
    source_.releaseDeviceObjects();
}

} // namespace ap::inputs
