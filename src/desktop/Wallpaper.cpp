#include "desktop/Wallpaper.h"

#include <windows.h>

namespace ap::desktop {

std::wstring currentWallpaper() {
    wchar_t buf[2048] = {};
    if (!SystemParametersInfoW(SPI_GETDESKWALLPAPER, static_cast<UINT>(std::size(buf)), buf, 0)) {
        return {};
    }
    return buf;
}

void restoreWallpaper(const std::wstring& path) {
    // SPIF_SENDCHANGE avisa o Explorer (que repinta), mas sem SPIF_UPDATEINIFILE não
    // reescrevemos o registo: não queremos estragar um slideshow ou o Spotlight.
    std::wstring copy = path; // a API pede um ponteiro não-const
    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, copy.data(), SPIF_SENDCHANGE);
}

} // namespace ap::desktop
