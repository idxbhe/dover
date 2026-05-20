#include "shared/ipc.h"
#include "shared/config.h"
#include "shared/log.h"
#include "shared/process.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {
constexpr DWORD kOverlayReadyTimeoutMs = 10000;
}

std::filesystem::path ResolveWorkingDirectory(const std::wstring& application_path) {
  return std::filesystem::path(application_path).parent_path();
}

int wmain(int argc, wchar_t** argv) {
  const auto app_data = dover::shared::EnsureAppDataDir();
  if (app_data.empty()) {
    dover::shared::LogError("Failed to locate AppData.");
    return 1;
  }

  if (argc < 2) {
    dover::shared::LogError("Launcher requires target executable path.");
    std::wcout << L"Usage: dover_launcher.exe <game.exe> [args...]\n";
    return 1;
  }

  const auto overlay_path = dover::shared::GetOverlayDllPath();
  if (overlay_path.empty() || !std::filesystem::exists(overlay_path)) {
    dover::shared::LogError("Overlay DLL not found beside launcher.");
    return 1;
  }

  std::wstring command_line = dover::shared::BuildCommandLine(argc, argv);
  if (command_line.empty()) {
    dover::shared::LogError("Failed to build command line.");
    return 1;
  }

  PROCESS_INFORMATION process_info{};
  const auto working_directory = ResolveWorkingDirectory(argv[1]);
  if (!dover::shared::StartSuspendedProcess(argv[1], working_directory, command_line, process_info)) {
    dover::shared::LogError("Failed to launch target process.");
    return 1;
  }

  HANDLE ready_event = dover::shared::CreateOverlayReadyEvent(process_info.dwProcessId);
  if (!ready_event) {
    dover::shared::LogError("Failed to create overlay ready event.");
  }

  if (!dover::shared::InjectDll(process_info.hProcess, overlay_path)) {
    dover::shared::LogError("DLL injection failed.");
    TerminateProcess(process_info.hProcess, 1);
    if (ready_event) {
      CloseHandle(ready_event);
    }
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return 1;
  }

  if (ready_event) {
    WaitForSingleObject(ready_event, kOverlayReadyTimeoutMs);
    CloseHandle(ready_event);
  }

  ResumeThread(process_info.hThread);
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);

  dover::shared::LogInfo("Launcher injected overlay and resumed target.");
  std::wcout << L"AppData: " << app_data.wstring() << L"\n";
  return 0;
}
