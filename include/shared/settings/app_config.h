#pragma once
#include <cstdint>
#include <windows.h>
#include <atomic>
#include <filesystem>

namespace dover::shared {

struct GamepadMapping {
    uint8_t vk_code;
    bool modifier_ctrl;
    bool modifier_shift;
    bool modifier_alt;
};

enum class InjectionMethod : int { PureVTable = 0, InlineMinHook = 1 };

struct AppConfigPOD {
    float overlay_bg_alpha = 0.63f;
    float global_window_alpha = 0.95f;
    bool show_fps = true;
    bool show_clock = true;
    bool show_api = false;
    bool show_cpu = false;
    bool show_ram = false;
    int hotkey_toggle_main = VK_TAB;
    int hotkey_toggle_modifier = VK_SHIFT;
    GamepadMapping gamepad_to_vk_map[18] = {};
    bool show_gamepad_hud = false;
    int gamepad_hud_position = 2;
    float gamepad_hud_scale = 1.0f;
    InjectionMethod injection_method = InjectionMethod::PureVTable;
};

struct AppConfig {
    std::atomic<float> overlay_bg_alpha{0.63f};
    std::atomic<float> global_window_alpha{0.95f};
    std::atomic<bool> show_fps{true};
    std::atomic<bool> show_clock{true};
    std::atomic<bool> show_api{false};
    std::atomic<bool> show_cpu{false};
    std::atomic<bool> show_ram{false};
    std::atomic<int> hotkey_toggle_main{VK_TAB};
    std::atomic<int> hotkey_toggle_modifier{VK_SHIFT};
    
    std::atomic<GamepadMapping> gamepad_to_vk_map[18];
    
    std::atomic<bool> show_gamepad_hud{false};
    std::atomic<int> gamepad_hud_position{2};
    std::atomic<float> gamepad_hud_scale{1.0f};
    std::atomic<InjectionMethod> injection_method{InjectionMethod::PureVTable};

    AppConfig() {
        for (int i = 0; i < 18; ++i) {
            gamepad_to_vk_map[i].store({0, false, false, false});
        }
    }
    
    // Non-copyable/movable due to atomics
    AppConfig(const AppConfig&) = delete;
    AppConfig& operator=(const AppConfig&) = delete;
};

AppConfig& GetAppConfig();

void LoadAppConfigFromIni(const std::filesystem::path& path);
void SaveAppConfigToIni(const std::filesystem::path& path, const AppConfigPOD* snap_pod = nullptr);

} // namespace dover::shared
