#include "shared/storage.h"

#include <windows.h>
#include <shlobj.h>

namespace dover::shared {

namespace fs = std::filesystem;

std::filesystem::path GetDocumentsDir() {
    PWSTR raw = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &raw);
    if (FAILED(hr) || !raw) {
        if (raw) CoTaskMemFree(raw);
        return {};
    }
    fs::path result(raw);
    CoTaskMemFree(raw);
    return result;
}

std::filesystem::path GetDoverRootDir() {
    auto docs = GetDocumentsDir();
    if (docs.empty()) return {};
    return docs / L"Dover";
}

std::filesystem::path GetDoverDebugDir() {
    auto root = GetDoverRootDir();
    if (root.empty()) return {};
    return root / L"debug";
}

std::filesystem::path GetGameDir(const std::wstring& exe_name) {
    auto root = GetDoverRootDir();
    if (root.empty()) return {};
    return root / L"overlay" / L"games" / exe_name;
}

std::filesystem::path EnsureGameDir(const std::wstring& exe_name) {
    auto dir = GetGameDir(exe_name);
    if (dir.empty()) return {};
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return {};
    return dir;
}

} // namespace dover::shared
