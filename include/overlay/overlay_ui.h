#pragma once
#include <windows.h>
#include <cstdint>

struct ImFont;

#include <atomic>

namespace dover::overlay {

struct OverlayState {
    std::atomic<bool> show_overlay{false};
    std::atomic<bool> in_overlay_frame{false};
    WNDPROC original_wnd_proc = nullptr;
    const char* active_dx_version = "Unknown API";
    uint32_t swapchain_width = 0;
    uint32_t swapchain_height = 0;
};

OverlayState& GetOverlayState();


LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
void RenderImGuiUI();

void InitializeOverlay();

} // namespace dover::overlay

