#include "shared/config.h"

#include <windows.h>
#include <shlobj.h>

namespace dover::shared {

std::filesystem::path GetAppDataDir() {
  PWSTR raw_path = nullptr;
  const HRESULT result = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw_path);
  if (FAILED(result) || !raw_path) {
    return {};
  }

  std::filesystem::path app_data(raw_path);
  CoTaskMemFree(raw_path);
  return app_data / L"Dover";
}

std::filesystem::path EnsureAppDataDir() {
  const auto dir = GetAppDataDir();
  if (!dir.empty()) {
    std::filesystem::create_directories(dir);
  }
  return dir;
}

std::filesystem::path GetConfigPath() {
  return GetAppDataDir() / L"config.ini";
}

} // namespace dover::shared
