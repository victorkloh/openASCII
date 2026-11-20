#pragma once
// Vigia uma pasta com ReadDirectoryChangesW (assíncrono). O evento entra diretamente no
// MsgWaitForMultipleObjectsEx do loop principal: sem polling, sem thread extra.

#include <windows.h>

#include <filesystem>
#include <vector>

#include <wil/resource.h>

namespace ap::wallpaper {

class DirectoryWatcher {
public:
    ~DirectoryWatcher() { stop(); }

    bool start(const std::filesystem::path& dir);
    void stop();
    bool active() const { return dir_.is_valid(); }
    HANDLE event() const { return event_.get(); }

    // Chamar quando event() fica sinalizado. Volta a armar a vigilância.
    // Devolve true se houve alguma alteração.
    bool consume();

private:
    bool arm();

    wil::unique_hfile dir_;
    wil::unique_event event_;
    OVERLAPPED overlapped_{};
    std::vector<std::byte> buffer_ = std::vector<std::byte>(16 * 1024);
};

} // namespace ap::wallpaper
