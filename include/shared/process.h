#pragma once

#include <filesystem>
#include <string>

#include <windows.h>

namespace dover::shared {

std::filesystem::path GetExecutableDirectory();
std::filesystem::path GetOverlayDllPath();
std::wstring BuildCommandLine(int argc, wchar_t** argv);
bool StartSuspendedProcess(const std::wstring& application_path,
                           const std::filesystem::path& working_directory,
                           std::wstring command_line,
                           PROCESS_INFORMATION& process_info);
bool InjectDll(HANDLE process, const std::filesystem::path& dll_path);

} // namespace dover::shared
