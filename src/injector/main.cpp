#include "shared/ipc.h"
#include "shared/log.h"
#include "shared/process.h"

#include <filesystem>
#include <iostream>
#include <windows.h>
#include <shellapi.h>

namespace {
constexpr DWORD kOverlayReadyTimeoutMs = 10000;

std::filesystem::path ResolveWorkingDirectory(const std::wstring& application_path) {
  return std::filesystem::path(application_path).parent_path();
}
} // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    dover::shared::LogError("Injector requires target executable path.");
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
    DWORD err = GetLastError();
    if (err == ERROR_ELEVATION_REQUIRED) {
      dover::shared::LogError("Target game requires Administrator privileges. Restarting Injector as Administrator...");
      
      wchar_t injector_path[MAX_PATH];
      GetModuleFileNameW(nullptr, injector_path, MAX_PATH);

      SHELLEXECUTEINFOW sei = { sizeof(sei) };
      sei.lpVerb = L"runas";
      sei.lpFile = injector_path;
      sei.lpParameters = command_line.c_str();
      sei.nShow = SW_NORMAL;
      
      if (ShellExecuteExW(&sei)) {
        return 0;
      } else {
        dover::shared::LogError("Failed to elevate Injector privileges.");
      }
    } else {
      dover::shared::LogError("Failed to launch target process.");
    }
    return 1;
  }

  const auto overlay_path = dover::shared::GetOverlayDllPath();
  if (overlay_path.empty() || !std::filesystem::exists(overlay_path)) {
    dover::shared::LogError("Overlay DLL not found beside injector.");
    TerminateProcess(process_info.hProcess, 1);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return 1;
  }

  const DWORD pid = process_info.dwProcessId;
  HANDLE ready_event = dover::shared::CreateOverlayReadyEvent(pid);
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

  dover::shared::LogInfo("Process created; overlay injected.");
  ResumeThread(process_info.hThread);
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  return 0;
}
