#include "shared/log.h"
#include "shared/storage.h"

#include <windows.h>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <iostream>

namespace dover::shared {

namespace {
void LogWithTag(const char* tag, const char* message) {
  if (!tag || !message) {
    return;
  }

  char buffer[1024] = {};
  wsprintfA(buffer, "[%s] %s\n", tag, message);
  OutputDebugStringA(buffer);

  // Print to console if stdout is available
  HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  if (stdout_handle && stdout_handle != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(stdout_handle, buffer, static_cast<DWORD>(strlen(buffer)), &written, nullptr);
  }

  // Write to Documents/Dover/debug/dover.log
  std::filesystem::path debug_dir = GetDoverDebugDir();
  if (!debug_dir.empty()) {
    std::filesystem::create_directories(debug_dir);
    std::filesystem::path log_file = debug_dir / L"dover.log";
    std::ofstream ofs(log_file, std::ios::app);
    if (ofs.is_open()) {
      auto now = std::chrono::system_clock::now();
      auto time_t_now = std::chrono::system_clock::to_time_t(now);
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()) % 1000;
      struct tm time_info;
      localtime_s(&time_info, &time_t_now);
      ofs << "[" << std::put_time(&time_info, "%H:%M:%S") << "." 
          << std::setfill('0') << std::setw(3) << ms.count() << "] "
          << "[" << tag << "] " << message << "\n";
    }
  }
}
} // namespace

void LogInfo(const char* message) {
  LogWithTag("INFO", message);
}

void LogError(const char* message) {
  LogWithTag("ERROR", message);
}

} // namespace dover::shared
