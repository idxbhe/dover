#pragma once

#include <filesystem>

namespace dover::shared {

std::filesystem::path GetAppDataDir();
std::filesystem::path EnsureAppDataDir();
std::filesystem::path GetConfigPath();

} // namespace dover::shared
