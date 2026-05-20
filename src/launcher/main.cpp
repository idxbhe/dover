#include "shared/ipc.h"
#include "shared/config.h"
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
  const auto config_path = dover::shared::GetConfigPath();
  if (config_path.empty() || !std::filesystem::exists(config_path)) {
    return games;
  }

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
  const auto config_path = dover::shared::GetConfigPath();
  if (config_path.empty()) {
    return;
  }

  // Ensure directories exist
  std::filesystem::create_directories(config_path.parent_path());

  auto games = LoadSavedGames();
  for (const auto& game : games) {
    if (game.path == path) {
      return; // Already saved
    }
  }

  int count = static_cast<int>(games.size());
  wchar_t key[32] = {};
  wsprintfW(key, L"Game%d", count);
  WritePrivateProfileStringW(L"Games", key, path.c_str(), config_path.c_str());
  WritePrivateProfileStringW(L"Games", L"Count", std::to_wstring(count + 1).c_str(), config_path.c_str());
}

bool LaunchAndInject(const std::wstring& target_path, int argc, wchar_t** argv) {
  const auto overlay_path = dover::shared::GetOverlayDllPath();
  if (overlay_path.empty() || !std::filesystem::exists(overlay_path)) {
    dover::shared::LogError("Overlay DLL not found beside launcher.");
    return false;
  }

  std::wstring command_line;
  if (argc >= 2) {
    command_line = dover::shared::BuildCommandLine(argc, argv);
  } else {
    command_line = L"\"" + target_path + L"\"";
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
  const auto app_data = dover::shared::EnsureAppDataDir();
  if (app_data.empty()) {
    dover::shared::LogError("Failed to locate AppData.");
    return 1;
  }

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
