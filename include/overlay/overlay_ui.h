#pragma once
#include <windows.h>
#include <cstdint>

struct ImFont;

namespace dover::overlay {

struct OverlayState {
    bool show_overlay = false;
    bool in_overlay_frame = false;
    WNDPROC original_wnd_proc = nullptr;
    const char* active_dx_version = "Unknown API";
};

struct GamepadMapping {
    uint8_t vk_code;
    bool modifier_ctrl;
    bool modifier_shift;
    bool modifier_alt;
};

struct OverlayConfig {
    float overlay_bg_alpha = 0.63f;
    float global_window_alpha = 0.95f;
    bool show_fps = true;
    bool show_clock = true;
    bool show_api = false;
    int hotkey_toggle_main = VK_TAB;
    int hotkey_toggle_modifier = VK_SHIFT;
    
    // Gamepad to Keyboard mapping (16 bits for wButtons + 2 for triggers)
    GamepadMapping gamepad_to_vk_map[18] = {};
    
    // Gamepad HUD settings
    bool show_gamepad_hud = false;
    int gamepad_hud_position = 2; // 0=TopLeft, 1=TopCenter, 2=TopRight, 3=BottomLeft, 4=BottomCenter, 5=BottomRight
    float gamepad_hud_scale = 1.0f;
};

OverlayState& GetOverlayState();
OverlayConfig& GetOverlayConfig();


LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
void RenderImGuiUI();

void InitializeOverlay();

} // namespace dover::overlay
