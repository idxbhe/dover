#include "shared/telemetry.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace dover::shared::telemetry {

static TelemetryData s_data;

void Update() {
    static bool s_init = false;
    static LARGE_INTEGER s_freq = {};
    static LARGE_INTEGER s_last_fps_qpc = {};
    static double s_last_sys_time = 0.0;
    static int s_frame_count = 0;

    // CPU baseline state
    static FILETIME pre_idle_time{}, pre_kernel_time{}, pre_user_time{};

    if (!s_init) {
        QueryPerformanceFrequency(&s_freq);
        QueryPerformanceCounter(&s_last_fps_qpc);
        s_last_sys_time = static_cast<double>(GetTickCount64()) / 1000.0;
        
        // Establish CPU baseline immediately to prevent startup spike
        GetSystemTimes(&pre_idle_time, &pre_kernel_time, &pre_user_time);
        
        s_init = true;
    }

    // --- 1. Every Frame: Increment counter ---
    s_frame_count++;

    LARGE_INTEGER now_qpc;
    QueryPerformanceCounter(&now_qpc);
    double current_time = static_cast<double>(GetTickCount64()) / 1000.0;

    // --- 2. FPS Calculation (Update every 0.5s for responsiveness) ---
    double fps_elapsed = static_cast<double>(now_qpc.QuadPart - s_last_fps_qpc.QuadPart) / static_cast<double>(s_freq.QuadPart);
    if (fps_elapsed >= 0.5) {
        s_data.fps = static_cast<float>(static_cast<double>(s_frame_count) / fps_elapsed);
        s_frame_count = 0;
        s_last_fps_qpc = now_qpc;
    }

    // --- 3. System Telemetry (Update every 1.0s to minimize overhead) ---
    if (current_time - s_last_sys_time >= 1.0) {
        s_last_sys_time = current_time;

        // A. CPU Usage Calculation (Global System)
        FILETIME idle_time, kernel_time, user_time;
        if (GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
            auto FileTimeToQuad = [](const FILETIME& ft) -> uint64_t {
                return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
            };

            uint64_t idle   = FileTimeToQuad(idle_time);
            uint64_t kernel = FileTimeToQuad(kernel_time);
            uint64_t user   = FileTimeToQuad(user_time);

            uint64_t pre_idle   = FileTimeToQuad(pre_idle_time);
            uint64_t pre_kernel = FileTimeToQuad(pre_kernel_time);
            uint64_t pre_user   = FileTimeToQuad(pre_user_time);

            uint64_t idle_delta   = idle - pre_idle;
            uint64_t kernel_delta = kernel - pre_kernel;
            uint64_t user_delta   = user - pre_user;
            uint64_t total_delta  = kernel_delta + user_delta;

            if (total_delta > 0) {
                // System CPU usage = 1.0 - (idle_delta / total_delta)
                s_data.cpu_usage = 100.0f * (1.0f - static_cast<float>(idle_delta) / static_cast<float>(total_delta));
                if (s_data.cpu_usage < 0.0f) s_data.cpu_usage = 0.0f;
                if (s_data.cpu_usage > 100.0f) s_data.cpu_usage = 100.0f;
            }

            pre_idle_time   = idle_time;
            pre_kernel_time = kernel_time;
            pre_user_time   = user_time;
        }

        // B. RAM Usage Calculation
        MEMORYSTATUSEX mem_status{};
        mem_status.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&mem_status)) {
            s_data.ram_total_gb = static_cast<double>(mem_status.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
            s_data.ram_used_gb  = s_data.ram_total_gb - (static_cast<double>(mem_status.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0));
        }
    }
}

const TelemetryData& GetData() {
    return s_data;
}

} // namespace dover::shared::telemetry
