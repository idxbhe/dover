#include "shared/ipc.h"
#include "shared/storage.h"
#include "shared/log.h"
#include "shared/process.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr DWORD kOverlayReadyTimeoutMs = 10000;

std::filesystem::path ResolveWorkingDirectory(const std::wstring& application_path) {
  return std::filesystem::path(application_path).parent_path();
}

struct GameConfig {
  std::wstring path;
  std::wstring name;
};

std::vector<GameConfig> LoadSavedGames() {
  std::vector<GameConfig> games;
  auto root = dover::shared::GetDoverRootDir();
  if (root.empty()) return games;
  const auto config_path = root / L"launcher" / L"games.ini";
  if (!std::filesystem::exists(config_path)) return games;

  int count = GetPrivateProfileIntW(L"Games", L"Count", 0, config_path.c_str());
  for (int i = 0; i < count; ++i) {
    wchar_t key[32] = {};
    wsprintfW(key, L"Game%d", i);
    wchar_t path_buf[MAX_PATH] = {};
    GetPrivateProfileStringW(L"Games", key, L"", path_buf, MAX_PATH, config_path.c_str());

    if (path_buf[0] != L'\0') {
      std::wstring path_str(path_buf);
      std::filesystem::path p(path_str);
      games.push_back({path_str, p.filename().wstring()});
    }
  }
  return games;
}

void SaveGamePath(const std::wstring& path) {
  auto root = dover::shared::GetDoverRootDir();
  if (root.empty()) return;
  auto config_path = root / L"launcher" / L"games.ini";
  std::filesystem::create_directories(config_path.parent_path());

  auto games = LoadSavedGames();
  for (const auto& game : games) {
    if (game.path == path) return; // Already saved
  }

  int count = static_cast<int>(games.size());
  wchar_t key[32] = {};
  wsprintfW(key, L"Game%d", count);
  WritePrivateProfileStringW(L"Games", key, path.c_str(), config_path.c_str());
  WritePrivateProfileStringW(L"Games", L"Count", std::to_wstring(count + 1).c_str(), config_path.c_str());
}

bool LaunchAndInject(const std::wstring& target_path, int argc, wchar_t** argv) {
  // Detect target process bitness
  DWORD binary_type = 0;
  bool is_64bit = true;
  if (GetBinaryTypeW(target_path.c_str(), &binary_type)) {
    is_64bit = (binary_type == SCS_64BIT_BINARY);
  }

  std::wstring command_line;
  if (argc >= 2) {
    command_line = dover::shared::BuildCommandLine(argc, argv);
  } else {
    command_line = L"\"" + target_path + L"\"";
  }

  if (!is_64bit) {
    const auto executable_dir = dover::shared::GetExecutableDirectory();
    const auto injector32_path = executable_dir / L"dover_injector32.exe";

    if (!std::filesystem::exists(injector32_path)) {
      dover::shared::LogError("32-bit helper injector (dover_injector32.exe) not found beside launcher.");
      return false;
    }

    std::wstring full_command_line = L"\"" + injector32_path.wstring() + L"\" " + command_line;

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    dover::shared::LogInfo("32-bit target game detected. Spawning dover_injector32.exe...");
    if (CreateProcessW(nullptr, full_command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
      return true;
    }

    dover::shared::LogError("Failed to launch 32-bit helper injector.");
    return false;
  }

  const auto overlay_path = dover::shared::GetOverlayDllPath();
  if (overlay_path.empty() || !std::filesystem::exists(overlay_path)) {
    dover::shared::LogError("64-bit Overlay DLL not found beside launcher.");
    return false;
  }

  PROCESS_INFORMATION process_info{};
  const auto working_directory = ResolveWorkingDirectory(target_path);
  if (!dover::shared::StartSuspendedProcess(target_path, working_directory, command_line, process_info)) {
    dover::shared::LogError("Failed to launch target process.");
    return false;
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
    return false;
  }

  if (ready_event) {
    WaitForSingleObject(ready_event, kOverlayReadyTimeoutMs);
    CloseHandle(ready_event);
  }

  ResumeThread(process_info.hThread);
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);

  dover::shared::LogInfo("Launcher injected overlay and resumed target.");
  return true;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
  auto dover_root = dover::shared::GetDoverRootDir();
  if (dover_root.empty()) {
    dover::shared::LogError("Failed to locate Documents/Dover directory.");
    return 1;
  }
  std::filesystem::create_directories(dover_root / L"launcher");

  std::wstring target_path;
  if (argc >= 2) {
    target_path = argv[1];
    SaveGamePath(target_path);
    if (LaunchAndInject(target_path, argc, argv)) {
      return 0;
    }
    return 1;
  }

  // Interactive CLI Menu
  while (true) {
    auto games = LoadSavedGames();
    std::wcout << L"\n========================================\n";
    std::wcout << L"            DOVER LAUNCHER             \n";
    std::wcout << L"========================================\n";
    std::wcout << L"Saved games for testing:\n";
    for (size_t i = 0; i < games.size(); ++i) {
      std::wcout << (i + 1) << L". " << games[i].name << L" (" << games[i].path << L")\n";
    }
    std::wcout << (games.size() + 1) << L". Add a new game path\n";
    std::wcout << (games.size() + 2) << L". Exit\n";
    std::wcout << L"----------------------------------------\n";
    std::wcout << L"Select option (1-" << (games.size() + 2) << L"): ";

    std::wstring input;
    std::getline(std::wcin, input);
    if (input.empty()) continue;

    try {
      int choice = std::stoi(input);
      if (choice >= 1 && choice <= static_cast<int>(games.size())) {
        target_path = games[choice - 1].path;
        std::wcout << L"Launching " << games[choice - 1].name << L"...\n";
        LaunchAndInject(target_path, 0, nullptr);
        break;
      } else if (choice == static_cast<int>(games.size()) + 1) {
        std::wcout << L"Enter absolute path to game executable: ";
        std::wstring new_path;
        std::getline(std::wcin, new_path);
        if (!new_path.empty()) {
          // Remove surrounding quotes if any
          if (new_path.front() == L'"' && new_path.back() == L'"') {
            new_path = new_path.substr(1, new_path.length() - 2);
          }
          if (std::filesystem::exists(new_path)) {
            SaveGamePath(new_path);
            std::wcout << L"Game path saved successfully!\n";
          } else {
            std::wcout << L"Error: File does not exist.\n";
          }
        }
      } else if (choice == static_cast<int>(games.size()) + 2) {
        std::wcout << L"Exiting launcher.\n";
        break;
      } else {
        std::wcout << L"Invalid choice. Try again.\n";
      }
    } catch (...) {
      std::wcout << L"Invalid input. Please enter a number.\n";
    }
  }

  return 0;
}
