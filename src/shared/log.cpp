#include "shared/log.h"
#include "shared/storage.h"

#include <windows.h>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <iostream>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <mutex>
#include <filesystem>

namespace dover::shared {

namespace {
bool g_debug_mode = false;
std::filesystem::path g_log_file_path;
std::ofstream g_log_stream;
std::mutex g_log_mutex;

void LogWithTag(const char* tag, const char* format, va_list args) {
    if (!tag || !format) return;

    char msg_buffer[4096] = {};
    vsnprintf(msg_buffer, sizeof(msg_buffer) - 1, format, args);

    char out_buffer[4096] = {};
    wsprintfA(out_buffer, "[%s] %s\n", tag, msg_buffer);
    OutputDebugStringA(out_buffer);

    HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdout_handle && stdout_handle != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(stdout_handle, out_buffer, static_cast<DWORD>(strlen(out_buffer)), &written, nullptr);
    }

    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log_stream.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        struct tm time_info;
        localtime_s(&time_info, &time_t_now);
        g_log_stream << "[" << std::put_time(&time_info, "%H:%M:%S") << "."
            << std::setfill('0') << std::setw(3) << ms.count() << "] "
            << "[" << tag << "] " << msg_buffer << "\n";
        g_log_stream.flush();
    }
}
} // namespace

void InitializeLogging() {
    std::filesystem::path debug_dir = GetDoverDebugDir();
    if (!debug_dir.empty()) {
        std::filesystem::create_directories(debug_dir);
        
        if (std::filesystem::exists(debug_dir / L"dover_debug.flag")) {
            g_debug_mode = true;
        }

        char exe_path[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
        std::string exe_name = std::filesystem::path(exe_path).filename().string();
        DWORD pid = GetCurrentProcessId();

        char filename[256];
        snprintf(filename, sizeof(filename), "dover_%s_%lu.log", exe_name.c_str(), pid);
        g_log_file_path = debug_dir / filename;
        
        {
            std::lock_guard<std::mutex> lock(g_log_mutex);
            if (g_log_stream.is_open()) {
                g_log_stream.close();
            }
            g_log_stream.open(g_log_file_path, std::ios::app);
        }
        
        LogInfo("Logging initialized. Target: %s, PID: %lu, Debug Mode: %s", exe_name.c_str(), pid, g_debug_mode ? "ON" : "OFF");
    }
}

void LogInfo(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogWithTag("INFO", format, args);
    va_end(args);
}

void LogWarning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogWithTag("WARNING", format, args);
    va_end(args);
}

void LogError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogWithTag("ERROR", format, args);
    va_end(args);
}

void LogDebug(const char* format, ...) {
    if (!g_debug_mode) return;
    va_list args;
    va_start(args, format);
    LogWithTag("DEBUG", format, args);
    va_end(args);
}

} // namespace dover::shared
