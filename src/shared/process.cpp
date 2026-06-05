#include "shared/process.h"

#include "shared/log.h"

#include <string>

namespace dover::shared {

std::filesystem::path GetExecutableDirectory() {
  wchar_t buffer[MAX_PATH] = {};
  const DWORD size = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (size == 0 || size == MAX_PATH) {
    return {};
  }

  return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path GetOverlayDllPath() {
  const auto executable_directory = GetExecutableDirectory();
  if (executable_directory.empty()) {
    return {};
  }

#ifdef _WIN64
  return executable_directory / L"overlay64.dll";
#else
  return executable_directory / L"overlay32.dll";
#endif
}

std::wstring BuildCommandLine(int argc, wchar_t** argv) {
  std::wstring command_line;
  for (int i = 1; i < argc; ++i) {
    if (i > 1) {
      command_line.append(L" ");
    }
    command_line.append(L"\"").append(argv[i]).append(L"\"");
  }
  return command_line;
}

bool StartSuspendedProcess(const std::wstring& application_path,
                  const std::filesystem::path& working_directory,
                  std::wstring command_line,
                  PROCESS_INFORMATION& process_info) {
  STARTUPINFOW startup_info{};
  startup_info.cb = sizeof(startup_info);

  return CreateProcessW(application_path.c_str(), command_line.data(), nullptr, nullptr, FALSE,
                CREATE_SUSPENDED, nullptr,
                working_directory.empty() ? nullptr : working_directory.c_str(),
                &startup_info, &process_info) != FALSE;
}

bool InjectDll(HANDLE process, const std::filesystem::path& dll_path) {
  const std::wstring dll_string = dll_path.wstring();
  const size_t bytes = (dll_string.size() + 1) * sizeof(wchar_t);

  void* remote_buffer = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!remote_buffer) {
    DWORD err = GetLastError();
    LogError("VirtualAllocEx failed for DLL path. Error: %lu", err);
    return false;
  }

  const BOOL wrote = WriteProcessMemory(process, remote_buffer, dll_string.c_str(), bytes, nullptr);
  if (!wrote) {
    DWORD err = GetLastError();
    LogError("WriteProcessMemory failed for DLL path. Error: %lu", err);
    VirtualFreeEx(process, remote_buffer, 0, MEM_RELEASE);
    return false;
  }

  HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
  if (!kernel32) {
    LogError("GetModuleHandleW(kernel32.dll) failed.");
    VirtualFreeEx(process, remote_buffer, 0, MEM_RELEASE);
    return false;
  }

  auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel32, "LoadLibraryW"));
  if (!load_library) {
    LogError("GetProcAddress(LoadLibraryW) failed.");
    VirtualFreeEx(process, remote_buffer, 0, MEM_RELEASE);
    return false;
  }

  HANDLE remote_thread = CreateRemoteThread(process, nullptr, 0, load_library, remote_buffer, 0, nullptr);
  if (!remote_thread) {
    DWORD err = GetLastError();
    LogError("CreateRemoteThread failed. Error: %lu", err);
    VirtualFreeEx(process, remote_buffer, 0, MEM_RELEASE);
    return false;
  }

  WaitForSingleObject(remote_thread, INFINITE);
  DWORD exit_code = 0;
  GetExitCodeThread(remote_thread, &exit_code);
  CloseHandle(remote_thread);
  VirtualFreeEx(process, remote_buffer, 0, MEM_RELEASE);

  if (exit_code == 0) {
    LogError("LoadLibraryW returned null module handle.");
    return false;
  }

  return true;
}

} // namespace dover::shared
