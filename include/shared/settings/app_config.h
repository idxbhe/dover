#pragma once
#include <cstdint>
#include <windows.h>

namespace dover::shared {

struct GamepadMapping {
    uint8_t vk_code;
    bool modifier_ctrl;
    bool modifier_shift;
    bool modifier_alt;
};

struct AppConfig {
    float overlay_bg_alpha = 0.63f;
    float global_window_alpha = 0.95f;
    bool show_fps = true;
    bool show_clock = true;
    bool show_api = false;
    int hotkey_toggle_main = VK_TAB;
    int hotkey_toggle_modifier = VK_SHIFT;
    
    GamepadMapping gamepad_to_vk_map[18] = {};
    
    bool show_gamepad_hud = false;
    int gamepad_hud_position = 2;
    float gamepad_hud_scale = 1.0f;
};

AppConfig& GetAppConfig();

} // namespace dover::shared
