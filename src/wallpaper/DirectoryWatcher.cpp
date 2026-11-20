#include "wallpaper/DirectoryWatcher.h"

#include "util/Log.h"

namespace ap::wallpaper {

bool DirectoryWatcher::start(const std::filesystem::path& dir) {
    stop();
    dir_.reset(CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr));
    if (!dir_.is_valid()) {
        log::warn(L"Hot reload: não consegui vigiar {} (erro {})", dir.wstring(), GetLastError());
        return false;
    }
    event_.create(wil::EventOptions::ManualReset);
    if (!arm()) {
        stop();
        return false;
    }
    log::debug(L"Hot reload: a vigiar {}", dir.wstring());
    return true;
}

void DirectoryWatcher::stop() {
    if (dir_.is_valid()) {
        CancelIoEx(dir_.get(), &overlapped_);
        DWORD ignored = 0;
        GetOverlappedResult(dir_.get(), &overlapped_, &ignored, TRUE); // esperar o cancelamento
    }
    dir_.reset();
    event_.reset();
}

bool DirectoryWatcher::arm() {
    overlapped_ = {};
    overlapped_.hEvent = event_.get();
    event_.ResetEvent();
    const BOOL ok = ReadDirectoryChangesW(dir_.get(), buffer_.data(), static_cast<DWORD>(buffer_.size()), TRUE,
                                          FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME |
                                              FILE_NOTIFY_CHANGE_SIZE,
                                          nullptr, &overlapped_, nullptr);
    if (!ok) log::warn(L"ReadDirectoryChangesW falhou (erro {})", GetLastError());
    return ok != FALSE;
}

bool DirectoryWatcher::consume() {
    if (!dir_.is_valid()) return false;
    DWORD bytes = 0;
    const bool changed = GetOverlappedResult(dir_.get(), &overlapped_, &bytes, FALSE) != FALSE;
    // bytes == 0 quer dizer que o buffer transbordou: houve alterações, só não sabemos quais.
    arm();
    return changed;
}

} // namespace ap::wallpaper
