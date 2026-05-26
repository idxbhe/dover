#pragma once

#include <filesystem>
#include <string>

namespace dover::shared {

// Returns Documents/ via SHGetKnownFolderPath(FOLDERID_Documents).
// Never returns a hardcoded path. Returns empty on failure.
std::filesystem::path GetDocumentsDir();

// Returns Documents/Dover/
std::filesystem::path GetDoverRootDir();

// Returns Documents/Dover/debug/
std::filesystem::path GetDoverDebugDir();

// Returns Documents/Dover/overlay/games/<exe_name>/
// exe_name should be the raw executable filename (e.g. L"eldenring.exe")
std::filesystem::path GetGameDir(const std::wstring& exe_name);

// Same as GetGameDir but also calls create_directories. Returns empty on failure.
std::filesystem::path EnsureGameDir(const std::wstring& exe_name);

} // namespace dover::shared
