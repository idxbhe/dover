#pragma once
#include <windows.h>

struct ImFont;

namespace dover::overlay {

struct OverlayState {
    bool show_overlay = false;
    bool in_overlay_frame = false;
    WNDPROC original_wnd_proc = nullptr;
    const char* active_dx_version = "Unknown API";
};

struct OverlayConfig {
    float overlay_bg_alpha = 0.63f;
    float global_window_alpha = 0.95f;
    bool show_fps = true;
    bool show_clock = true;
    bool show_api = false;
};

OverlayState& GetOverlayState();
OverlayConfig& GetOverlayConfig();


LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
void RenderImGuiUI();



} // namespace dover::overlay
