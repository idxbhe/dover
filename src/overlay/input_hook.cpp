#include "overlay/input_hook.h"
#include "shared/settings/app_config.h"
#include "overlay/overlay_ui.h" // For GetOverlayState().show_overlay, GetOverlayState().in_overlay_frame, HookedWndProc
#include "overlay/hook_utils.h"
#include "shared/input_mapper.h"

#include <windows.h>
#include <cstring>
#include <string>
#include <imgui.h>
#include <xinput.h>

#include "shared/input_utils.h"
#include "shared/input_mapper.h"

// Forward declaration of imgui wndproc handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace dover::overlay {

thread_local bool g_in_imgui_new_frame = false;

// XInput Guide Button
#define XINPUT_GAMEPAD_GUIDE 0x0400
typedef DWORD(WINAPI* XInputGetStateEx_t)(DWORD dwUserIndex, XINPUT_STATE* pState);

static XInputGetStateEx_t g_XInputGetStateEx = nullptr;
static HMODULE g_hXInputDll = nullptr;
static bool g_xinput_initialized = false;
static bool g_prev_guide_pressed = false;

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
using ScreenToClientFn = BOOL(WINAPI*)(HWND, LPPOINT);

// Cached cursor clip rect from the game, restored when overlay closes
static RECT g_game_clip_rect = {};
static bool g_game_has_clip_rect = false;

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
ScreenToClientFn g_original_screen_to_client = nullptr;

bool ProcessInputMessage(LPMSG lpMsg) {
  if (GetOverlayState().show_overlay && lpMsg) {
    if ((lpMsg->message >= WM_MOUSEFIRST && lpMsg->message <= WM_MOUSELAST) ||
        (lpMsg->message >= WM_KEYFIRST && lpMsg->message <= WM_KEYLAST) ||
        lpMsg->message == WM_INPUT) {
      auto& cfg = shared::GetAppConfig();
      bool modifier_pressed = cfg.hotkey_toggle_modifier == 0 || (GetKeyState(cfg.hotkey_toggle_modifier) & 0x8000);
      bool is_toggle = (lpMsg->message == WM_KEYDOWN && lpMsg->wParam == (WPARAM)cfg.hotkey_toggle_main && modifier_pressed);
      if (!is_toggle) {
        return true; // We want to block it from game
      }
    }
  }
  return false;
}

SHORT WINAPI HookedGetAsyncKeyState(int vKey) {
  if (GetOverlayState().show_overlay && !shared::g_allow_input_queries) {
    auto& cfg = shared::GetAppConfig();
    if (vKey == cfg.hotkey_toggle_main || (cfg.hotkey_toggle_modifier != 0 && vKey == cfg.hotkey_toggle_modifier)) {
        if (shared::g_allow_input_queries) {
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
  if (GetOverlayState().show_overlay && !shared::g_allow_input_queries) {
    auto& cfg = shared::GetAppConfig();
    if (nVirtKey == cfg.hotkey_toggle_main || (cfg.hotkey_toggle_modifier != 0 && nVirtKey == cfg.hotkey_toggle_modifier)) {
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
  if (GetOverlayState().show_overlay && !shared::g_allow_input_queries && lpKeyState) {
    std::memset(lpKeyState, 0, 256);
    return TRUE;
  }
  if (g_original_get_keyboard_state) {
    return g_original_get_keyboard_state(lpKeyState);
  }
  return FALSE;
}

BOOL WINAPI HookedClipCursor(const RECT* lpRect) {
  if (GetOverlayState().show_overlay) {
    // Cache the game's requested clip rect for restoration when overlay closes
    if (lpRect) {
      g_game_clip_rect = *lpRect;
      g_game_has_clip_rect = true;
    } else {
      g_game_has_clip_rect = false;
    }
    return TRUE;
  }
  // Cache even when overlay is hidden so we always have the latest
  if (lpRect) {
    g_game_clip_rect = *lpRect;
    g_game_has_clip_rect = true;
  } else {
    g_game_has_clip_rect = false;
  }
  if (g_original_clip_cursor) {
    return g_original_clip_cursor(lpRect);
  }
  return ClipCursor(lpRect);
}

BOOL WINAPI HookedGetCursorPos(LPPOINT lpPoint) {
  if (GetOverlayState().show_overlay && !GetOverlayState().in_overlay_frame) {
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

BOOL WINAPI HookedScreenToClient(HWND hWnd, LPPOINT lpPoint) {
  BOOL res = g_original_screen_to_client ? g_original_screen_to_client(hWnd, lpPoint) : ScreenToClient(hWnd, lpPoint);
  if (res && g_in_imgui_new_frame && lpPoint) {
    uint32_t swap_w = GetOverlayState().swapchain_width;
    uint32_t swap_h = GetOverlayState().swapchain_height;
    if (swap_w > 0 && swap_h > 0) {
      RECT rect;
      if (GetClientRect(hWnd, &rect)) {
        float client_w = static_cast<float>(rect.right - rect.left);
        float client_h = static_cast<float>(rect.bottom - rect.top);
        if (client_w > 0 && client_h > 0 && (client_w != swap_w || client_h != swap_h)) {
          lpPoint->x = static_cast<LONG>(lpPoint->x * (static_cast<float>(swap_w) / client_w));
          lpPoint->y = static_cast<LONG>(lpPoint->y * (static_cast<float>(swap_h) / client_h));
        }
      }
    }
  }
  return res;
}

BOOL WINAPI HookedSetCursorPos(int x, int y) {
  if (GetOverlayState().show_overlay && !GetOverlayState().in_overlay_frame) {
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

  if (result && ProcessInputMessage(lpMsg)) {
    TranslateMessage(lpMsg);
    DispatchMessageW(lpMsg);
    lpMsg->message = WM_NULL;
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

  if (result && ProcessInputMessage(lpMsg)) {
    TranslateMessage(lpMsg);
    DispatchMessageA(lpMsg);
    lpMsg->message = WM_NULL;
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

  if (result && ProcessInputMessage(lpMsg)) {
    TranslateMessage(lpMsg);
    DispatchMessageW(lpMsg);
    lpMsg->message = WM_NULL;
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

  if (result && ProcessInputMessage(lpMsg)) {
    TranslateMessage(lpMsg);
    DispatchMessageA(lpMsg);
    lpMsg->message = WM_NULL;
  }
  return result;
}

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
XInputGetStateFn g_orig_xinput14_getstate = nullptr;
XInputGetStateFn g_orig_xinput13_getstate = nullptr;

void ModifyXInputState(XINPUT_STATE* pState) {
  bool should_zero = false;
  if (shared::g_allow_xinput) {
    // Caller is ImGui: Zero inputs if overlay is hidden
    if (!GetOverlayState().show_overlay) should_zero = true;
  } else {
    // Caller is Game: Zero inputs if overlay is showing
    if (GetOverlayState().show_overlay) should_zero = true;
  }

  if (should_zero) {
    pState->Gamepad.wButtons = 0;
    pState->Gamepad.bLeftTrigger = 0;
    pState->Gamepad.bRightTrigger = 0;
    pState->Gamepad.sThumbLX = 0;
    pState->Gamepad.sThumbLY = 0;
    pState->Gamepad.sThumbRX = 0;
    pState->Gamepad.sThumbRY = 0;
  }
}

DWORD WINAPI HookedXInput14GetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
  DWORD result = g_orig_xinput14_getstate ? g_orig_xinput14_getstate(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;
  if (result == ERROR_SUCCESS) {
      if (!shared::g_allow_xinput && !GetOverlayState().show_overlay) {
          shared::input_mapper::ProcessGamepadRemapping(pState, dwUserIndex);
      }
      ModifyXInputState(pState);
  }
  return result;
}

DWORD WINAPI HookedXInput13GetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
  DWORD result = g_orig_xinput13_getstate ? g_orig_xinput13_getstate(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;
  if (result == ERROR_SUCCESS) {
      if (!shared::g_allow_xinput && !GetOverlayState().show_overlay) {
          shared::input_mapper::ProcessGamepadRemapping(pState, dwUserIndex);
      }
      ModifyXInputState(pState);
  }
  return result;
}

} // namespace

LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_ACTIVATE || msg == WM_ACTIVATEAPP) {
    bool is_inactive = false;
    if (msg == WM_ACTIVATE) {
      is_inactive = (LOWORD(wparam) == WA_INACTIVE);
    } else {
      is_inactive = (wparam == FALSE);
    }

    if (is_inactive && GetOverlayState().show_overlay) {
      GetOverlayState().show_overlay = false;
      
      // Restore game's cursor clip rect
      if (g_game_has_clip_rect) {
        if (g_original_clip_cursor) {
          g_original_clip_cursor(&g_game_clip_rect);
        } else {
          ClipCursor(&g_game_clip_rect);
        }
      } else {
        if (g_original_clip_cursor) {
          g_original_clip_cursor(nullptr);
        } else {
          ClipCursor(nullptr);
        }
      }
      
      ImGuiIO& io = ImGui::GetIO();
      io.ClearInputKeys();
      io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
      io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard);
    }
  }

  auto& cfg = shared::GetAppConfig();
  bool modifier_pressed = cfg.hotkey_toggle_modifier == 0 || (GetKeyState(cfg.hotkey_toggle_modifier) & 0x8000);
  
  if (msg == WM_KEYDOWN && wparam == (WPARAM)cfg.hotkey_toggle_main && modifier_pressed) {
    GetOverlayState().show_overlay = !GetOverlayState().show_overlay;
    
    ImGuiIO& io = ImGui::GetIO();
    if (GetOverlayState().show_overlay) {
      // Opening overlay: enable cursor, nav, and release game cursor clip
      io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
      io.ConfigFlags |= (ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard);
      
      // Release game's cursor confinement for free mouse movement
      if (g_original_clip_cursor) {
        g_original_clip_cursor(nullptr);
      } else {
        ClipCursor(nullptr);
      }
      
      // Force OS cursor visible (games that hide it via SetCursor(NULL))
      SetCursor(LoadCursor(nullptr, IDC_ARROW));
    } else {
      // Closing overlay: clear input state and restore game clip rect
      io.ClearInputKeys();
      io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
      io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard);
      
      // Restore game's cursor confinement
      if (g_game_has_clip_rect) {
        if (g_original_clip_cursor) {
          g_original_clip_cursor(&g_game_clip_rect);
        } else {
          ClipCursor(&g_game_clip_rect);
        }
      }
    }
    
    return 1; // Block input to game
  }

  if (GetOverlayState().show_overlay) {
    // Permanently block the game from hiding the cursor while overlay is active.
    // Games typically call SetCursor(NULL) inside WM_SETCURSOR on every mouse move,
    // overriding any one-shot SetCursor call. Intercepting here is the correct defense.
    if (msg == WM_SETCURSOR) {
      SetCursor(LoadCursor(nullptr, IDC_ARROW));
      return TRUE;
    }

    // Scale coordinates in client-coordinate mouse messages when window size and swapchain size mismatch
    if ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) && msg != WM_MOUSEWHEEL && msg != WM_MOUSEHWHEEL) {
      short x = static_cast<short>(LOWORD(lparam));
      short y = static_cast<short>(HIWORD(lparam));
      uint32_t swap_w = GetOverlayState().swapchain_width;
      uint32_t swap_h = GetOverlayState().swapchain_height;
      if (swap_w > 0 && swap_h > 0) {
        RECT rect;
        if (GetClientRect(hwnd, &rect)) {
          float client_w = static_cast<float>(rect.right - rect.left);
          float client_h = static_cast<float>(rect.bottom - rect.top);
          if (client_w > 0 && client_h > 0 && (client_w != swap_w || client_h != swap_h)) {
            short scaled_x = static_cast<short>(x * (static_cast<float>(swap_w) / client_w));
            short scaled_y = static_cast<short>(y * (static_cast<float>(swap_h) / client_h));
            lparam = MAKELPARAM(scaled_x, scaled_y);
          }
        }
      }
    }

    shared::g_allow_input_queries = true;
    bool imgui_processed = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
    shared::g_allow_input_queries = false;
    
    if (imgui_processed) {
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

  return CallWindowProcW(GetOverlayState().original_wnd_proc, hwnd, msg, wparam, lparam);
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
  void* screen_to_client_addr = GetProcAddress(user32, "ScreenToClient");

  bool success = true;

  if (get_async_key_state_addr) {
    success &= CreateAndEnableHook(
        get_async_key_state_addr,
        reinterpret_cast<void*>(&HookedGetAsyncKeyState),
        reinterpret_cast<void**>(&g_original_get_async_key_state));
    if (success) {
      shared::g_key_state_func = [](int vKey) -> bool {
          if (g_original_get_async_key_state) {
              return (g_original_get_async_key_state(vKey) & 0x8000) != 0;
          }
          return (GetAsyncKeyState(vKey) & 0x8000) != 0;
      };
    }
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
  if (screen_to_client_addr) {
    success &= CreateAndEnableHook(
        screen_to_client_addr,
        reinterpret_cast<void*>(&HookedScreenToClient),
        reinterpret_cast<void**>(&g_original_screen_to_client));
  }

  // Hook XInputGetState proactively if loaded by game or by us
  HMODULE xinput14 = GetModuleHandleW(L"xinput1_4.dll");
  if (!xinput14) xinput14 = LoadLibraryW(L"xinput1_4.dll"); // Force load to intercept future calls and ImGui
  if (xinput14) {
    void* addr = GetProcAddress(xinput14, "XInputGetState");
    if (addr) {
      CreateAndEnableHook(addr, reinterpret_cast<void*>(&HookedXInput14GetState), reinterpret_cast<void**>(&g_orig_xinput14_getstate));
    }
  }

  HMODULE xinput13 = GetModuleHandleW(L"xinput1_3.dll");
  if (xinput13) {
    void* addr = GetProcAddress(xinput13, "XInputGetState");
    if (addr) {
      CreateAndEnableHook(addr, reinterpret_cast<void*>(&HookedXInput13GetState), reinterpret_cast<void**>(&g_orig_xinput13_getstate));
    }
  }

  return success;
}

void PollGamepadToggle() {
  if (!g_xinput_initialized) {
    const wchar_t* dlls[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
    for (auto dll : dlls) {
      g_hXInputDll = LoadLibraryW(dll);
      if (g_hXInputDll) {
        g_XInputGetStateEx = (XInputGetStateEx_t)GetProcAddress(g_hXInputDll, (LPCSTR)100);
        if (g_XInputGetStateEx) {
          break;
        }
        FreeLibrary(g_hXInputDll);
        g_hXInputDll = nullptr;
      }
    }
    g_xinput_initialized = true;
  }

  if (!g_XInputGetStateEx) return;

  XINPUT_STATE state = {};
  if (g_XInputGetStateEx(0, &state) == ERROR_SUCCESS) {
    bool guide_pressed = (state.Gamepad.wButtons & XINPUT_GAMEPAD_GUIDE) != 0;
    
    if (guide_pressed && !g_prev_guide_pressed) {
      GetOverlayState().show_overlay = !GetOverlayState().show_overlay;
      
      ImGuiIO& io = ImGui::GetIO();
      if (GetOverlayState().show_overlay) {
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
        io.ConfigFlags |= (ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard);
        
        // Release game's cursor confinement
        if (g_original_clip_cursor) {
          g_original_clip_cursor(nullptr);
        } else {
          ClipCursor(nullptr);
        }
        
        // Force OS cursor visible
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
      } else {
        io.ClearInputKeys();
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard);
        
        // Restore game's cursor confinement
        if (g_game_has_clip_rect) {
          if (g_original_clip_cursor) {
            g_original_clip_cursor(&g_game_clip_rect);
          } else {
            ClipCursor(&g_game_clip_rect);
          }
        }
      }
    }
    g_prev_guide_pressed = guide_pressed;
  }
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
  if (g_original_screen_to_client) {
    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_screen_to_client));
  }

  shared::g_key_state_func = nullptr;
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
  g_original_screen_to_client = nullptr;

  if (g_orig_xinput14_getstate) {
    DisableAndRemoveHook(reinterpret_cast<void*>(g_orig_xinput14_getstate));
    g_orig_xinput14_getstate = nullptr;
  }
  if (g_orig_xinput13_getstate) {
    DisableAndRemoveHook(reinterpret_cast<void*>(g_orig_xinput13_getstate));
    g_orig_xinput13_getstate = nullptr;
  }

  if (g_hXInputDll) {
    FreeLibrary(g_hXInputDll);
    g_hXInputDll = nullptr;
    g_XInputGetStateEx = nullptr;
    g_xinput_initialized = false;
  }
}

static std::string g_clipboard_buffer;

static const char* HookedGetClipboardText(void* /*user_data*/) {
  g_clipboard_buffer.clear();
  HWND hwnd = GetForegroundWindow();
  
  // Zero-retry, fail-fast: clipboard is on the render thread.
  // Sleep() here — even Sleep(1) — can block up to ~15ms (OS timer granularity),
  // tanking FPS. If clipboard is locked by another process, drop this frame silently.
  // ImGui will retry naturally next frame while Ctrl+C/V is still held.
  if (!OpenClipboard(hwnd) && !OpenClipboard(nullptr)) {
    return nullptr;
  }
  
  HANDLE hData = GetClipboardData(CF_UNICODETEXT);
  if (hData) {
      wchar_t* wtext = (wchar_t*)GlobalLock(hData);
      if (wtext) {
          int len = WideCharToMultiByte(CP_UTF8, 0, wtext, -1, nullptr, 0, nullptr, nullptr);
          if (len > 0) {
              g_clipboard_buffer.resize(len - 1);
              WideCharToMultiByte(CP_UTF8, 0, wtext, -1, &g_clipboard_buffer[0], len, nullptr, nullptr);
          }
          GlobalUnlock(hData);
      }
  }
  CloseClipboard();
  return g_clipboard_buffer.empty() ? nullptr : g_clipboard_buffer.c_str();
}

static void HookedSetClipboardText(void* /*user_data*/, const char* text) {
  HWND hwnd = GetForegroundWindow();
  
  // Zero-retry, fail-fast: same rationale as GetClipboardText.
  if (!OpenClipboard(hwnd) && !OpenClipboard(nullptr)) {
    return;
  }
  
  EmptyClipboard();
  int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
  HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (wlen + 1) * sizeof(wchar_t));
  if (hMem) {
      wchar_t* dest = (wchar_t*)GlobalLock(hMem);
      MultiByteToWideChar(CP_UTF8, 0, text, -1, dest, wlen);
      dest[wlen] = L'\0';
      GlobalUnlock(hMem);
      SetClipboardData(CF_UNICODETEXT, hMem);
  }
  CloseClipboard();
}

void OverrideImGuiClipboardFunctions() {
  ImGuiIO& io = ImGui::GetIO();
  io.SetClipboardTextFn = HookedSetClipboardText;
  io.GetClipboardTextFn = HookedGetClipboardText;
}

} // namespace dover::overlay
