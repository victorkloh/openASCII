#include "inputs/LiveData.h"

#include "util/Log.h"
#include "util/Win32.h"

#include <windows.h>
#include <winhttp.h>

#include <cmath>
#include <format>
#include <string_view>
#include <vector>

#include <wil/resource.h>

namespace ap::inputs {

namespace {

uint64_t ft(const FILETIME& f) { return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }

// GET https://<host><path> e devolve o corpo (UTF-8). Vazio se falhar.
std::string httpGet(const wchar_t* host, const std::wstring& path) {
    using hinternet = wil::unique_any<HINTERNET, decltype(&WinHttpCloseHandle), WinHttpCloseHandle>;
    hinternet session(WinHttpOpen(L"AsciiPaper/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) return {};
    WinHttpSetTimeouts(session.get(), 5000, 5000, 8000, 8000);
    hinternet connect(WinHttpConnect(session.get(), host, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connect) return {};
    hinternet request(WinHttpOpenRequest(connect.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request || !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        return {};
    }
    std::string body;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(request.get(), &avail) && avail > 0) {
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), chunk.data(), avail, &read) || read == 0) break;
        body.append(chunk.data(), read);
        if (body.size() > 512 * 1024) break;
    }
    return body;
}

// Leitura mínima de JSON: o número a seguir a "key": (primeira ocorrência depois de `after`).
bool jsonNumber(std::string_view json, std::string_view key, double& out, std::string_view after = {}) {
    size_t from = 0;
    if (!after.empty()) {
        from = json.find(after);
        if (from == std::string_view::npos) return false;
    }
    const std::string needle = "\"" + std::string(key) + "\":";
    const size_t p = json.find(needle, from);
    if (p == std::string_view::npos) return false;
    const char* s = json.data() + p + needle.size();
    char* end = nullptr;
    out = std::strtod(s, &end);
    return end != s;
}

std::wstring urlEncode(const std::wstring& s) {
    const std::string u = win::toUtf8(s);
    std::wstring out;
    for (unsigned char c : u) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') out += static_cast<wchar_t>(c);
        else out += std::format(L"%{:02X}", c);
    }
    return out;
}

} // namespace

void SystemStats::update() {
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const uint64_t i = ft(idle), k = ft(kernel), u = ft(user);
        if (lastKernel_ != 0) {
            const uint64_t di = i - lastIdle_, dk = k - lastKernel_, du = u - lastUser_;
            const uint64_t total = dk + du; // o kernel inclui o idle
            if (total > 0) cpu_ = static_cast<float>(1.0 - static_cast<double>(di) / static_cast<double>(total));
        }
        lastIdle_ = i;
        lastKernel_ = k;
        lastUser_ = u;
    }
    MEMORYSTATUSEX ms{sizeof(ms)};
    if (GlobalMemoryStatusEx(&ms)) ram_ = ms.dwMemoryLoad / 100.0f;
}

float solarElevation(double lat, double lon, std::chrono::system_clock::time_point when) {
    const double days = std::chrono::duration<double>(when.time_since_epoch()).count() / 86400.0 - 10957.5; // desde J2000
    const double pi = 3.14159265358979;
    const double g = std::fmod(357.529 + 0.98560028 * days, 360.0) * pi / 180;
    const double q = std::fmod(280.459 + 0.98564736 * days, 360.0);
    const double L = (q + 1.915 * std::sin(g) + 0.020 * std::sin(2 * g)) * pi / 180;
    const double e = (23.439 - 0.00000036 * days) * pi / 180;
    const double decl = std::asin(std::sin(e) * std::sin(L));
    const double ra = std::atan2(std::cos(e) * std::sin(L), std::cos(L));
    const double gmst = std::fmod(18.697374558 + 24.06570982441908 * days, 24.0);
    const double ha = (gmst * 15 + lon) * pi / 180 - ra;
    const double latr = lat * pi / 180;
    const double el = std::asin(std::sin(latr) * std::sin(decl) + std::cos(latr) * std::cos(decl) * std::cos(ha));
    return static_cast<float>(el * 180 / pi);
}

void Weather::start(const std::wstring& city) {
    if (city.empty()) {
        stop();
        return;
    }
    if (thread_.joinable() && city == city_) return;
    stop();
    city_ = city;
    stop_ = false;
    thread_ = std::thread(&Weather::run, this, city);
}

void Weather::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

WeatherNow Weather::now() {
    std::lock_guard lock(mutex_);
    if (data_.valid) data_.sunElevation = solarElevation(data_.latitude, data_.longitude, std::chrono::system_clock::now());
    return data_;
}

void Weather::run(std::wstring city) {
    // 1) Cidade -> coordenadas (uma vez).
    double lat = 0, lon = 0;
    const std::string geo = httpGet(L"geocoding-api.open-meteo.com",
                                    L"/v1/search?count=1&language=pt&format=json&name=" + urlEncode(city));
    if (!jsonNumber(geo, "latitude", lat) || !jsonNumber(geo, "longitude", lon)) {
        log::warn(L"Tempo real: cidade \"{}\" não encontrada", city);
        return;
    }
    log::info(L"Tempo real: {} ({:.2f}, {:.2f})", city, lat, lon);
    // 2) Tempo atual a cada 15 minutos.
    while (!stop_) {
        const std::wstring path = std::format(
            L"/v1/forecast?latitude={:.4f}&longitude={:.4f}&current=temperature_2m,precipitation,cloud_cover,"
            L"wind_speed_10m,is_day,weather_code&wind_speed_unit=ms",
            lat, lon);
        const std::string body = httpGet(L"api.open-meteo.com", path);
        double t = 0, p = 0, cc = 0, ws = 0, day = 1, code = 0;
        if (jsonNumber(body, "temperature_2m", t, "\"current\":") && jsonNumber(body, "precipitation", p, "\"current\":") &&
            jsonNumber(body, "cloud_cover", cc, "\"current\":") && jsonNumber(body, "wind_speed_10m", ws, "\"current\":")) {
            jsonNumber(body, "is_day", day, "\"current\":");
            jsonNumber(body, "weather_code", code, "\"current\":");
            std::lock_guard lock(mutex_);
            data_.valid = true;
            data_.temperature = static_cast<float>(t);
            data_.rainMm = static_cast<float>(p);
            data_.cloud = static_cast<float>(cc / 100.0);
            data_.windMs = static_cast<float>(ws);
            data_.isDay = day > 0.5;
            data_.code = static_cast<int>(code);
            data_.latitude = lat;
            data_.longitude = lon;
            log::info(L"Tempo real: {:.1f} °C, chuva {:.1f} mm, nuvens {:.0f}%, vento {:.1f} m/s, código {}", t, p, cc, ws,
                      static_cast<int>(code));
        } else {
            log::warn(L"Tempo real: resposta inválida do Open-Meteo");
        }
        for (int i = 0; i < 15 * 60 * 10 && !stop_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace ap::inputs
