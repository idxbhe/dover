#pragma once
#include <windows.h>
#include <cstdint>

struct ImFont;

#include <atomic>

namespace dover::overlay {

enum class GraphicsAPI {
    Unknown,
    DX9,
    DX11,
    DX12
};

struct OverlayState {
    std::atomic<bool> show_overlay{false};
    std::atomic<bool> in_overlay_frame{false};
    std::atomic<WNDPROC> original_wnd_proc{nullptr};
    std::atomic<HWND> game_hwnd{nullptr};
    std::atomic<GraphicsAPI> active_dx_version{GraphicsAPI::Unknown};
    std::atomic<uint32_t> swapchain_width{0};
    std::atomic<uint32_t> swapchain_height{0};
    std::atomic<uint32_t> client_width{0};
    std::atomic<uint32_t> client_height{0};
};

OverlayState& GetOverlayState();


LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
void RenderImGuiUI();

void InitializeOverlay();

} // namespace dover::overlay

