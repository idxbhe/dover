#include "overlay/overlay_ui.h"
#include "overlay/hook_utils.h"

#include <windows.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <cstring>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace dover::overlay {

bool g_show_overlay = false;
WNDPROC g_original_wnd_proc = nullptr;
const char* g_active_dx_version = "Unknown API";

namespace {
using GetAsyncKeyStateFn = SHORT(WINAPI*)(int);
using GetKeyStateFn = SHORT(WINAPI*)(int);
using GetKeyboardStateFn = BOOL(WINAPI*)(PBYTE);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);

GetAsyncKeyStateFn g_original_get_async_key_state = nullptr;
GetKeyStateFn g_original_get_key_state = nullptr;
GetKeyboardStateFn g_original_get_keyboard_state = nullptr;
ClipCursorFn g_original_clip_cursor = nullptr;

SHORT WINAPI HookedGetAsyncKeyState(int vKey) {
  if (g_show_overlay) {
    if (vKey == VK_TAB || vKey == VK_SHIFT) {
      if (g_original_get_async_key_state) {
        return g_original_get_async_key_state(vKey);
      }
    }
    return 0;
  }
  if (g_original_get_async_key_state) {
    return g_original_get_async_key_state(vKey);
  }
  return 0;
}

SHORT WINAPI HookedGetKeyState(int nVirtKey) {
  if (g_show_overlay) {
    if (nVirtKey == VK_TAB || nVirtKey == VK_SHIFT) {
      if (g_original_get_key_state) {
        return g_original_get_key_state(nVirtKey);
      }
    }
    return 0;
  }
  if (g_original_get_key_state) {
    return g_original_get_key_state(nVirtKey);
  }
  return 0;
}

BOOL WINAPI HookedGetKeyboardState(PBYTE lpKeyState) {
  if (g_show_overlay && lpKeyState) {
    std::memset(lpKeyState, 0, 256);
    return TRUE;
  }
  if (g_original_get_keyboard_state) {
    return g_original_get_keyboard_state(lpKeyState);
  }
  return FALSE;
}

BOOL WINAPI HookedClipCursor(const RECT* lpRect) {
  if (g_show_overlay) {
    return TRUE;
  }
  if (g_original_clip_cursor) {
    return g_original_clip_cursor(lpRect);
  }
  return ClipCursor(lpRect);
}
} // namespace

LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  // Toggle overlay on Shift + Tab
  if (msg == WM_KEYDOWN && wparam == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000)) {
    g_show_overlay = !g_show_overlay;
    
    // Update ImGui cursor visibility state
    ImGuiIO& io = ImGui::GetIO();
    if (g_show_overlay) {
      io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    } else {
      io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }
    
    return 1; // Block input to game
  }

  if (g_show_overlay) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
      return true; // ImGui processed, block from game
    }

    // Block keyboard, mouse and raw input events from leaking into the game when overlay is active
    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) {
      return 1;
    }
    if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) {
      return 1;
    }
    if (msg == WM_INPUT) {
      return 1;
    }
  }

  return CallWindowProcW(g_original_wnd_proc, hwnd, msg, wparam, lparam);
}

void SetupImGuiTheme() {
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 8.0f;
  style.FrameRounding = 4.0f;
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.85f);
  style.Colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.12f, 0.16f, 0.90f);
  style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.22f, 0.95f);
  style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.35f, 0.70f);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.35f, 0.50f, 0.85f);
  style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.35f, 0.45f, 0.65f, 1.00f);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.10f, 0.14f, 0.60f);
}

void RenderImGuiUI() {
  // 1. Draw Pinned Info Window (transparent corner overlay)
  ImGui::SetNextWindowPos(ImVec2(12.0f, 10.0f), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.65f);
  ImGui::Begin("Info Window", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
               ImGuiWindowFlags_NoNav);

  SYSTEMTIME time{};
  GetLocalTime(&time);
  ImGui::TextColored(ImVec4(0.20f, 1.00f, 0.70f, 1.00f), "Time: %02u:%02u:%02u", time.wHour, time.wMinute, time.wSecond);
  ImGui::TextColored(ImVec4(1.00f, 1.00f, 1.00f, 1.00f), "FPS:  %.1f", ImGui::GetIO().Framerate);
  ImGui::TextColored(ImVec4(1.00f, 0.80f, 0.20f, 1.00f), "API:  %s", g_active_dx_version);
  ImGui::End();

  // 2. Draw Interactive Overlay Panel if visible
  if (g_show_overlay) {
    // Dim the underlying game frame for premium presentation
    ImVec2 display_size = ImGui::GetIO().DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), display_size, IM_COL32(0, 0, 0, 160));

    ImGui::SetNextWindowSize(ImVec2(550.0f, 350.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Dover Overlay Panel (Press Shift+Tab to Close)", &g_show_overlay, ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Welcome to Dover Steam Overlay alternative!");
    ImGui::TextDisabled("Take notes or configure settings while gaming.");
    ImGui::Spacing();
    
    if (ImGui::BeginTabBar("DoverTabs")) {
      if (ImGui::BeginTabItem("Notes")) {
        static char notes[2048] = "Write down your notes, strategies, or game codes here...";
        ImGui::InputTextMultiline("##notes", notes, sizeof(notes), ImVec2(-FLT_MIN, -FLT_MIN));
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Settings")) {
        ImGui::Text("Configurations:");
        ImGui::Separator();
        static bool vsync = true;
        ImGui::Checkbox("Enable VSync Simulation", &vsync);
        ImGui::Spacing();
        if (ImGui::Button("Close Overlay")) {
          g_show_overlay = false;
          ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        }
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    ImGui::End();
  }
}

bool InitializeInputHooks() {
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (!user32) {
    return false;
  }

  void* get_async_key_state_addr = GetProcAddress(user32, "GetAsyncKeyState");
  void* get_key_state_addr = GetProcAddress(user32, "GetKeyState");
  void* get_keyboard_state_addr = GetProcAddress(user32, "GetKeyboardState");
  void* clip_cursor_addr = GetProcAddress(user32, "ClipCursor");

  bool success = true;

  if (get_async_key_state_addr) {
    success &= CreateAndEnableHook(
        get_async_key_state_addr,
        reinterpret_cast<void*>(&HookedGetAsyncKeyState),
        reinterpret_cast<void**>(&g_original_get_async_key_state));
  }
  if (get_key_state_addr) {
    success &= CreateAndEnableHook(
        get_key_state_addr,
        reinterpret_cast<void*>(&HookedGetKeyState),
        reinterpret_cast<void**>(&g_original_get_key_state));
  }
  if (get_keyboard_state_addr) {
    success &= CreateAndEnableHook(
        get_keyboard_state_addr,
        reinterpret_cast<void*>(&HookedGetKeyboardState),
        reinterpret_cast<void**>(&g_original_get_keyboard_state));
  }
  if (clip_cursor_addr) {
    success &= CreateAndEnableHook(
        clip_cursor_addr,
        reinterpret_cast<void*>(&HookedClipCursor),
        reinterpret_cast<void**>(&g_original_clip_cursor));
  }

  return success;
}

void ShutdownInputHooks() {
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_async_key_state));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_key_state));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_keyboard_state));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_clip_cursor));

  g_original_get_async_key_state = nullptr;
  g_original_get_key_state = nullptr;
  g_original_get_keyboard_state = nullptr;
  g_original_clip_cursor = nullptr;
}

} // namespace dover::overlay
