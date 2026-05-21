#include "overlay/overlay_ui.h"
#include "overlay/hook_utils.h"
#include "overlay/notes_manager.h"
#include "overlay/notes_ui.h"

#include <windows.h>
#include <psapi.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <cstring>
#include <string>

#pragma comment(lib, "psapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace dover::overlay {

bool g_show_overlay = false;
bool g_in_overlay_frame = false;
WNDPROC g_original_wnd_proc = nullptr;
const char* g_active_dx_version = "Unknown API";

namespace {
using GetAsyncKeyStateFn = SHORT(WINAPI*)(int);
using GetKeyStateFn = SHORT(WINAPI*)(int);
using GetKeyboardStateFn = BOOL(WINAPI*)(PBYTE);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
using GetCursorPosFn = BOOL(WINAPI*)(LPPOINT);
using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using PeekMessageWFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using PeekMessageAFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using GetMessageWFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);
using GetMessageAFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);

GetAsyncKeyStateFn g_original_get_async_key_state = nullptr;
GetKeyStateFn g_original_get_key_state = nullptr;
GetKeyboardStateFn g_original_get_keyboard_state = nullptr;
ClipCursorFn g_original_clip_cursor = nullptr;
GetCursorPosFn g_original_get_cursor_pos = nullptr;
SetCursorPosFn g_original_set_cursor_pos = nullptr;
PeekMessageWFn g_original_peek_message_w = nullptr;
PeekMessageAFn g_original_peek_message_a = nullptr;
GetMessageWFn g_original_get_message_w = nullptr;
GetMessageAFn g_original_get_message_a = nullptr;

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

BOOL WINAPI HookedGetCursorPos(LPPOINT lpPoint) {
  if (g_show_overlay && !g_in_overlay_frame) {
    HWND active_wnd = GetActiveWindow();
    if (active_wnd) {
      RECT rect = {};
      if (GetClientRect(active_wnd, &rect)) {
        POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
        ClientToScreen(active_wnd, &center);
        if (lpPoint) {
          *lpPoint = center;
          return TRUE;
        }
      }
    }
  }
  if (g_original_get_cursor_pos) {
    return g_original_get_cursor_pos(lpPoint);
  }
  return GetCursorPos(lpPoint);
}

BOOL WINAPI HookedSetCursorPos(int x, int y) {
  if (g_show_overlay && !g_in_overlay_frame) {
    return TRUE;
  }
  if (g_original_set_cursor_pos) {
    return g_original_set_cursor_pos(x, y);
  }
  return SetCursorPos(x, y);
}

BOOL WINAPI HookedPeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
  BOOL result = FALSE;
  if (g_original_peek_message_w) {
    result = g_original_peek_message_w(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
  } else {
    result = PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
  }

  if (result && g_show_overlay && lpMsg) {
    if ((lpMsg->message >= WM_MOUSEFIRST && lpMsg->message <= WM_MOUSELAST) ||
        (lpMsg->message >= WM_KEYFIRST && lpMsg->message <= WM_KEYLAST) ||
        lpMsg->message == WM_INPUT) {
      bool is_toggle = (lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000));
      if (!is_toggle) {
        TranslateMessage(lpMsg);
        DispatchMessageW(lpMsg);
        lpMsg->message = WM_NULL;
      }
    }
  }
  return result;
}

BOOL WINAPI HookedPeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
  BOOL result = FALSE;
  if (g_original_peek_message_a) {
    result = g_original_peek_message_a(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
  } else {
    result = PeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
  }

  if (result && g_show_overlay && lpMsg) {
    if ((lpMsg->message >= WM_MOUSEFIRST && lpMsg->message <= WM_MOUSELAST) ||
        (lpMsg->message >= WM_KEYFIRST && lpMsg->message <= WM_KEYLAST) ||
        lpMsg->message == WM_INPUT) {
      bool is_toggle = (lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000));
      if (!is_toggle) {
        TranslateMessage(lpMsg);
        DispatchMessageA(lpMsg);
        lpMsg->message = WM_NULL;
      }
    }
  }
  return result;
}

BOOL WINAPI HookedGetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
  BOOL result = FALSE;
  if (g_original_get_message_w) {
    result = g_original_get_message_w(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
  } else {
    result = GetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
  }

  if (result && g_show_overlay && lpMsg) {
    if ((lpMsg->message >= WM_MOUSEFIRST && lpMsg->message <= WM_MOUSELAST) ||
        (lpMsg->message >= WM_KEYFIRST && lpMsg->message <= WM_KEYLAST) ||
        lpMsg->message == WM_INPUT) {
      bool is_toggle = (lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000));
      if (!is_toggle) {
        TranslateMessage(lpMsg);
        DispatchMessageW(lpMsg);
        lpMsg->message = WM_NULL;
      }
    }
  }
  return result;
}

BOOL WINAPI HookedGetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
  BOOL result = FALSE;
  if (g_original_get_message_a) {
    result = g_original_get_message_a(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
  } else {
    result = GetMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
  }

  if (result && g_show_overlay && lpMsg) {
    if ((lpMsg->message >= WM_MOUSEFIRST && lpMsg->message <= WM_MOUSELAST) ||
        (lpMsg->message >= WM_KEYFIRST && lpMsg->message <= WM_KEYLAST) ||
        lpMsg->message == WM_INPUT) {
      bool is_toggle = (lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000));
      if (!is_toggle) {
        TranslateMessage(lpMsg);
        DispatchMessageA(lpMsg);
        lpMsg->message = WM_NULL;
      }
    }
  }
  return result;
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

static std::string g_ini_path_utf8;

void SetupImGuiTheme() {
  // Setup robust global INI file path in user LOCALAPPDATA directory
  wchar_t local_app_data[MAX_PATH];
  if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH)) {
    std::wstring dover_dir = std::wstring(local_app_data) + L"\\dover";
    CreateDirectoryW(dover_dir.c_str(), NULL);
    std::wstring ini_path = dover_dir + L"\\imgui.ini";
    
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, ini_path.c_str(), -1, NULL, 0, NULL, NULL);
    g_ini_path_utf8.resize(size_needed);
    WideCharToMultiByte(CP_UTF8, 0, ini_path.c_str(), -1, &g_ini_path_utf8[0], size_needed, NULL, NULL);
    
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = g_ini_path_utf8.c_str();

    // Determine game name from the current process executable
    char exe_name[MAX_PATH] = {};
    GetModuleBaseNameA(GetCurrentProcess(), nullptr, exe_name, MAX_PATH);
    InitializeNotesManager(std::wstring(local_app_data), std::string(exe_name));
    InitializeNotesUI();
  }

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

  // 2. Draw Steam-style Interactive Navigation and floating windows if visible
  if (g_show_overlay) {
    // Dim the underlying game frame for premium presentation
    ImVec2 display_size = ImGui::GetIO().DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), display_size, IM_COL32(0, 0, 0, 160));

    static bool show_notes = true;
    static bool show_settings = false;

    // A. Top Navigation Bar (Fixed persistent toolbar at the top)
    const float bar_height = 42.0f;
    const float bar_padding_x = 14.0f;
    const float button_width = 92.0f;
    const float button_height = 24.0f;
    const float button_spacing = 8.0f;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(display_size.x, bar_height), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    
    ImGui::Begin("Top Navigation Bar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                 
    ImGui::PopStyleVar(2);

    const float button_group_width = (button_width * 3.0f) + (button_spacing * 2.0f);
    const float brand_y = (bar_height - ImGui::GetTextLineHeight()) * 0.5f;
    const float button_y = (bar_height - button_height) * 0.5f;

    ImGui::SetCursorPos(ImVec2(bar_padding_x, brand_y));
    ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.00f, 1.00f), "DOVER OVERLAY");

    const float center_start_x = (display_size.x - button_group_width) * 0.5f;
    const float close_button_x = display_size.x - bar_padding_x - button_width;

    ImGui::SetCursorPos(ImVec2(center_start_x, button_y));
    if (show_notes) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.45f, 0.65f, 1.00f));
    }
    if (ImGui::Button("Notes", ImVec2(button_width, button_height))) {
      show_notes = !show_notes;
    }
    if (show_notes) {
      ImGui::PopStyleColor();
    }

    ImGui::SameLine(0.0f, button_spacing);
    if (show_settings) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.45f, 0.65f, 1.00f));
    }
    if (ImGui::Button("Settings", ImVec2(button_width, button_height))) {
      show_settings = !show_settings;
    }
    if (show_settings) {
      ImGui::PopStyleColor();
    }

    ImGui::SameLine(0.0f, button_spacing);
    ImGui::SetCursorPos(ImVec2(close_button_x, button_y));
    if (ImGui::Button("Close", ImVec2(button_width, button_height))) {
      g_show_overlay = false;
      ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }

    ImGui::End();

    // B. Floating Feature Jendela (Modular, bebas geser, mengingat posisi via dover/imgui.ini)
    
    // Notes Jendela (modular — handled by notes_ui module)
    if (show_notes) {
      RenderNotesWindow(&show_notes);
    }

    // Settings Jendela
    if (show_settings) {
      ImGui::SetNextWindowSize(ImVec2(320.0f, 200.0f), ImGuiCond_FirstUseEver);
      if (ImGui::Begin("Settings", &show_settings, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("Configurations:");
        ImGui::Separator();
        static bool vsync = true;
        ImGui::Checkbox("Enable VSync Simulation", &vsync);
        ImGui::Spacing();
        ImGui::TextDisabled("Future game-specific saving:");
        ImGui::TextDisabled("dover/imgui_<game>.ini");
      }
      ImGui::End();
    }
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
  void* get_cursor_pos_addr = GetProcAddress(user32, "GetCursorPos");
  void* set_cursor_pos_addr = GetProcAddress(user32, "SetCursorPos");
  void* peek_message_w_addr = GetProcAddress(user32, "PeekMessageW");
  void* peek_message_a_addr = GetProcAddress(user32, "PeekMessageA");
  void* get_message_w_addr = GetProcAddress(user32, "GetMessageW");
  void* get_message_a_addr = GetProcAddress(user32, "GetMessageA");

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
  if (get_cursor_pos_addr) {
    success &= CreateAndEnableHook(
        get_cursor_pos_addr,
        reinterpret_cast<void*>(&HookedGetCursorPos),
        reinterpret_cast<void**>(&g_original_get_cursor_pos));
  }
  if (set_cursor_pos_addr) {
    success &= CreateAndEnableHook(
        set_cursor_pos_addr,
        reinterpret_cast<void*>(&HookedSetCursorPos),
        reinterpret_cast<void**>(&g_original_set_cursor_pos));
  }
  if (peek_message_w_addr) {
    success &= CreateAndEnableHook(
        peek_message_w_addr,
        reinterpret_cast<void*>(&HookedPeekMessageW),
        reinterpret_cast<void**>(&g_original_peek_message_w));
  }
  if (peek_message_a_addr) {
    success &= CreateAndEnableHook(
        peek_message_a_addr,
        reinterpret_cast<void*>(&HookedPeekMessageA),
        reinterpret_cast<void**>(&g_original_peek_message_a));
  }
  if (get_message_w_addr) {
    success &= CreateAndEnableHook(
        get_message_w_addr,
        reinterpret_cast<void*>(&HookedGetMessageW),
        reinterpret_cast<void**>(&g_original_get_message_w));
  }
  if (get_message_a_addr) {
    success &= CreateAndEnableHook(
        get_message_a_addr,
        reinterpret_cast<void*>(&HookedGetMessageA),
        reinterpret_cast<void**>(&g_original_get_message_a));
  }

  return success;
}

void ShutdownInputHooks() {
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_async_key_state));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_key_state));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_keyboard_state));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_clip_cursor));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_cursor_pos));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_set_cursor_pos));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_peek_message_w));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_peek_message_a));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_message_w));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_message_a));

  g_original_get_async_key_state = nullptr;
  g_original_get_key_state = nullptr;
  g_original_get_keyboard_state = nullptr;
  g_original_clip_cursor = nullptr;
  g_original_get_cursor_pos = nullptr;
  g_original_set_cursor_pos = nullptr;
  g_original_peek_message_w = nullptr;
  g_original_peek_message_a = nullptr;
  g_original_get_message_w = nullptr;
  g_original_get_message_a = nullptr;
}

} // namespace dover::overlay
