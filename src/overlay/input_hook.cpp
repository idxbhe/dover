#include "overlay/input_hook.h"
#include "shared/settings/app_config.h"
#include "overlay/overlay_ui.h" // For GetOverlayState().show_overlay, GetOverlayState().in_overlay_frame, HookedWndProc
#include "overlay/hook_utils.h"
#include "shared/input_mapper.h"

#include <windows.h>
#include <cstring>
#include <atomic>
#include <string>
#include <thread>
#include <imgui.h>
#include <xinput.h>
#include <cmath>

#include "shared/input_utils.h"
#include "shared/input_mapper.h"
#include "shared/log.h"
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
using ShowCursorFn = int(WINAPI*)(BOOL);

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
ShowCursorFn g_original_show_cursor = nullptr;

static std::atomic<int> g_game_show_cursor_count{0};
static std::atomic<bool> g_cursor_initialized{false};
static std::atomic<HWND> g_game_hwnd{nullptr};
static bool g_last_overlay_visible = false;
static std::atomic<ULONGLONG> g_overlay_closed_time{0};

static bool IsInputBlocked() {
  if (GetOverlayState().show_overlay) {
    return true;
  }
  ULONGLONG closed_time = g_overlay_closed_time.load();
  if (closed_time > 0 && (GetTickCount64() - closed_time < 200)) {
    return true;
  }
  return false;
}

bool ProcessInputMessage(LPMSG lpMsg) {
  if (IsInputBlocked() && lpMsg) {
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
  if (IsInputBlocked() && !shared::g_allow_input_queries) {
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
  if (IsInputBlocked() && !shared::g_allow_input_queries) {
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
  if (IsInputBlocked() && !shared::g_allow_input_queries && lpKeyState) {
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
  if (!lpPoint) {
    return FALSE;
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

static std::atomic<WORD> g_latched_buttons[4]{};

struct LatchedAnalogState {
    std::atomic<bool> left_stick_latched{false};
    std::atomic<bool> right_stick_latched{false};
    std::atomic<bool> left_trigger_latched{false};
    std::atomic<bool> right_trigger_latched{false};
};
static LatchedAnalogState g_latched_analogs[4];

void ModifyXInputState(DWORD dwUserIndex, XINPUT_STATE* pState) {
  if (shared::g_visualizer_xinput) return;
  if (dwUserIndex >= 4) return;

  WORD hw_buttons = pState->Gamepad.wButtons;

  if (shared::g_allow_xinput) {
    // Caller is ImGui:
    if (GetOverlayState().show_overlay) {
      // Latch buttons pressed while overlay is open
      g_latched_buttons[dwUserIndex].fetch_or(hw_buttons);

      if (std::abs(pState->Gamepad.sThumbLX) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE || std::abs(pState->Gamepad.sThumbLY) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) {
          g_latched_analogs[dwUserIndex].left_stick_latched = true;
      }
      if (std::abs(pState->Gamepad.sThumbRX) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE || std::abs(pState->Gamepad.sThumbRY) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) {
          g_latched_analogs[dwUserIndex].right_stick_latched = true;
      }
      if (pState->Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
          g_latched_analogs[dwUserIndex].left_trigger_latched = true;
      }
      if (pState->Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
          g_latched_analogs[dwUserIndex].right_trigger_latched = true;
      }
    } else {
      // Zero inputs if overlay is hidden
      pState->Gamepad.wButtons = 0;
      pState->Gamepad.bLeftTrigger = 0;
      pState->Gamepad.bRightTrigger = 0;
      pState->Gamepad.sThumbLX = 0;
      pState->Gamepad.sThumbLY = 0;
      pState->Gamepad.sThumbRX = 0;
      pState->Gamepad.sThumbRY = 0;
    }
  } else {
    // Caller is Game:
    if (GetOverlayState().show_overlay) {
      // Zero all inputs if overlay is showing
      pState->Gamepad.wButtons = 0;
      pState->Gamepad.bLeftTrigger = 0;
      pState->Gamepad.bRightTrigger = 0;
      pState->Gamepad.sThumbLX = 0;
      pState->Gamepad.sThumbLY = 0;
      pState->Gamepad.sThumbRX = 0;
      pState->Gamepad.sThumbRY = 0;
      // Also latch buttons so they don't leak on close
      g_latched_buttons[dwUserIndex].fetch_or(hw_buttons);

      if (std::abs(pState->Gamepad.sThumbLX) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE || std::abs(pState->Gamepad.sThumbLY) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) {
          g_latched_analogs[dwUserIndex].left_stick_latched = true;
      }
      if (std::abs(pState->Gamepad.sThumbRX) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE || std::abs(pState->Gamepad.sThumbRY) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) {
          g_latched_analogs[dwUserIndex].right_stick_latched = true;
      }
      if (pState->Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
          g_latched_analogs[dwUserIndex].left_trigger_latched = true;
      }
      if (pState->Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
          g_latched_analogs[dwUserIndex].right_trigger_latched = true;
      }
    } else {
      // Overlay is closed:
      // Update latched buttons: if a button is no longer physically pressed, unlatch it
      WORD prev_latched = g_latched_buttons[dwUserIndex].fetch_and(hw_buttons);
      WORD still_pressed = prev_latched & hw_buttons;

      // Block any buttons that are still latched
      pState->Gamepad.wButtons &= ~still_pressed;

      // Unlatch and block analog sticks/triggers if they haven't returned to zero
      if (g_latched_analogs[dwUserIndex].left_stick_latched) {
          if (std::abs(pState->Gamepad.sThumbLX) <= XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE && std::abs(pState->Gamepad.sThumbLY) <= XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) {
              g_latched_analogs[dwUserIndex].left_stick_latched = false;
          } else {
              pState->Gamepad.sThumbLX = 0;
              pState->Gamepad.sThumbLY = 0;
          }
      }
      
      if (g_latched_analogs[dwUserIndex].right_stick_latched) {
          if (std::abs(pState->Gamepad.sThumbRX) <= XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE && std::abs(pState->Gamepad.sThumbRY) <= XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) {
              g_latched_analogs[dwUserIndex].right_stick_latched = false;
          } else {
              pState->Gamepad.sThumbRX = 0;
              pState->Gamepad.sThumbRY = 0;
          }
      }

      if (g_latched_analogs[dwUserIndex].left_trigger_latched) {
          if (pState->Gamepad.bLeftTrigger <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
              g_latched_analogs[dwUserIndex].left_trigger_latched = false;
          } else {
              pState->Gamepad.bLeftTrigger = 0;
          }
      }

      if (g_latched_analogs[dwUserIndex].right_trigger_latched) {
          if (pState->Gamepad.bRightTrigger <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
              g_latched_analogs[dwUserIndex].right_trigger_latched = false;
          } else {
              pState->Gamepad.bRightTrigger = 0;
          }
      }

      // If in input block cooldown, zero triggers/sticks/buttons
      if (IsInputBlocked()) {
        pState->Gamepad.bLeftTrigger = 0;
        pState->Gamepad.bRightTrigger = 0;
        pState->Gamepad.sThumbLX = 0;
        pState->Gamepad.sThumbLY = 0;
        pState->Gamepad.sThumbRX = 0;
        pState->Gamepad.sThumbRY = 0;
        pState->Gamepad.wButtons = 0;
      }
    }
  }
}

DWORD WINAPI HookedXInput14GetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
  DWORD result = g_orig_xinput14_getstate ? g_orig_xinput14_getstate(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;
  if (result == ERROR_SUCCESS) {
      if (!shared::g_allow_xinput && !GetOverlayState().show_overlay) {
          shared::input_mapper::ProcessGamepadRemapping(pState, dwUserIndex);
      }
      ModifyXInputState(dwUserIndex, pState);
  }
  return result;
}

DWORD WINAPI HookedXInput13GetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
  DWORD result = g_orig_xinput13_getstate ? g_orig_xinput13_getstate(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;
  if (result == ERROR_SUCCESS) {
      if (!shared::g_allow_xinput && !GetOverlayState().show_overlay) {
          shared::input_mapper::ProcessGamepadRemapping(pState, dwUserIndex);
      }
      ModifyXInputState(dwUserIndex, pState);
  }
  return result;
}

int WINAPI HookedShowCursor(BOOL bShow) {
  if (GetOverlayState().show_overlay) {
    if (bShow) {
      int new_count = ++g_game_show_cursor_count;
      if (new_count > 0) {
        if (g_original_show_cursor) {
          g_original_show_cursor(TRUE);
        } else {
          ShowCursor(TRUE);
        }
      }
      return new_count;
    } else {
      int new_count = --g_game_show_cursor_count;
      if (new_count >= 0) {
        if (g_original_show_cursor) {
          g_original_show_cursor(FALSE);
        } else {
          ShowCursor(FALSE);
        }
      }
      return new_count;
    }
  }

  int ret = 0;
  if (g_original_show_cursor) {
    ret = g_original_show_cursor(bShow);
  } else {
    ret = ShowCursor(bShow);
  }
  g_game_show_cursor_count.store(ret);
  return ret;
}

void UpdateOverlayVisibilityState() {
  bool current_visible = GetOverlayState().show_overlay;
  if (current_visible == g_last_overlay_visible) return;
  g_last_overlay_visible = current_visible;
  
  if (current_visible) {
    if (g_original_clip_cursor) {
      g_original_clip_cursor(nullptr);
    } else {
      ClipCursor(nullptr);
    }
    
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    
    int current_count = g_game_show_cursor_count.load();
    if (current_count < 0) {
      int needed_shows = -current_count;
      for (int i = 0; i < needed_shows; ++i) {
        if (g_original_show_cursor) {
          g_original_show_cursor(TRUE);
        } else {
          ShowCursor(TRUE);
        }
      }
    }
  } else {
    if (g_game_has_clip_rect) {
      if (g_original_clip_cursor) {
        g_original_clip_cursor(&g_game_clip_rect);
      } else {
        ClipCursor(&g_game_clip_rect);
      }
    }
    
    int current_count = g_game_show_cursor_count.load();
    if (current_count < 0) {
      int needed_hides = -current_count;
      for (int i = 0; i < needed_hides; ++i) {
        if (g_original_show_cursor) {
          g_original_show_cursor(FALSE);
        } else {
          ShowCursor(FALSE);
        }
      }
    }
  }
}

static std::atomic<bool> g_sticky_reset_pending{false};

static void ResetStickyInputs() {
  if (!g_game_hwnd) return;

  // 1. Release mouse buttons if they are physically up
  if (g_original_get_async_key_state) {
    if ((g_original_get_async_key_state(VK_LBUTTON) & 0x8000) == 0) PostMessageW(g_game_hwnd, WM_LBUTTONUP, 0, 0);
    if ((g_original_get_async_key_state(VK_RBUTTON) & 0x8000) == 0) PostMessageW(g_game_hwnd, WM_RBUTTONUP, 0, 0);
    if ((g_original_get_async_key_state(VK_MBUTTON) & 0x8000) == 0) PostMessageW(g_game_hwnd, WM_MBUTTONUP, 0, 0);
    if ((g_original_get_async_key_state(VK_XBUTTON1) & 0x8000) == 0) PostMessageW(g_game_hwnd, WM_XBUTTONUP, MAKEWPARAM(0, 1), 0);
    if ((g_original_get_async_key_state(VK_XBUTTON2) & 0x8000) == 0) PostMessageW(g_game_hwnd, WM_XBUTTONUP, MAKEWPARAM(0, 2), 0);
  } else {
    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) PostMessageW(g_game_hwnd, WM_LBUTTONUP, 0, 0);
    if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) == 0) PostMessageW(g_game_hwnd, WM_RBUTTONUP, 0, 0);
    if ((GetAsyncKeyState(VK_MBUTTON) & 0x8000) == 0) PostMessageW(g_game_hwnd, WM_MBUTTONUP, 0, 0);
    if ((GetAsyncKeyState(VK_XBUTTON1) & 0x8000) == 0) PostMessageW(g_game_hwnd, WM_XBUTTONUP, MAKEWPARAM(0, 1), 0);
    if ((GetAsyncKeyState(VK_XBUTTON2) & 0x8000) == 0) PostMessageW(g_game_hwnd, WM_XBUTTONUP, MAKEWPARAM(0, 2), 0);
  }

  // 2. Release keyboard keys if they are physically up
  for (int vk = 1; vk < 256; ++vk) {
    if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2) continue;

    bool is_up = false;
    if (g_original_get_async_key_state) {
      is_up = (g_original_get_async_key_state(vk) & 0x8000) == 0;
    } else {
      is_up = (GetAsyncKeyState(vk) & 0x8000) == 0;
    }

    if (is_up) {
      LPARAM lparam = static_cast<LPARAM>(0xC0000001U);
      UINT scan_code = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
      lparam |= (static_cast<LPARAM>(scan_code) << 16);
      PostMessageW(g_game_hwnd, WM_KEYUP, vk, lparam);
    }
  }
}

} // namespace

void TickInputCooldown() {
  if (!g_sticky_reset_pending.load(std::memory_order_relaxed)) return;

  ULONGLONG closed_time = g_overlay_closed_time.load(std::memory_order_relaxed);
  if (closed_time > 0 && (GetTickCount64() - closed_time >= 200)) {
    ResetStickyInputs();
    g_sticky_reset_pending.store(false);
  }
}

void SetOverlayVisible(bool visible) {
  if (GetOverlayState().show_overlay == visible) return;
  if (!visible) {
    g_overlay_closed_time.store(GetTickCount64());
    g_sticky_reset_pending.store(true);
  }
  GetOverlayState().show_overlay = visible;
  if (g_game_hwnd) {
    if (GetCurrentThreadId() == GetWindowThreadProcessId(g_game_hwnd, nullptr)) {
      UpdateOverlayVisibilityState();
    } else {
      PostMessageW(g_game_hwnd, WM_USER + 0x1337, 0, 0);
    }
  }
}

LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (!g_game_hwnd) {
    g_game_hwnd = hwnd;
  }

  if (!g_cursor_initialized.load(std::memory_order_relaxed)) {
    int cur = ShowCursor(TRUE);
    ShowCursor(FALSE);
    g_game_show_cursor_count.store(cur - 1);
    g_cursor_initialized.store(true, std::memory_order_relaxed);
  }

  UpdateOverlayVisibilityState();

  if (msg == WM_USER + 0x1337) {
    return 0;
  }

  if (msg == WM_ACTIVATE || msg == WM_ACTIVATEAPP) {
    bool is_inactive = false;
    if (msg == WM_ACTIVATE) {
      is_inactive = (LOWORD(wparam) == WA_INACTIVE);
    } else {
      is_inactive = (wparam == FALSE);
    }

    if (is_inactive && GetOverlayState().show_overlay) {
      SetOverlayVisible(false);
    }
  }

  auto& cfg = shared::GetAppConfig();
  bool modifier_pressed = cfg.hotkey_toggle_modifier == 0 || (GetKeyState(cfg.hotkey_toggle_modifier) & 0x8000);
  
  if (msg == WM_KEYDOWN && wparam == (WPARAM)cfg.hotkey_toggle_main && modifier_pressed) {
    SetOverlayVisible(!GetOverlayState().show_overlay);
    return 1; // Block input to game
  }

  if (IsInputBlocked()) {
    if (GetOverlayState().show_overlay) {
      if (msg == WM_SETCURSOR) {
        if (LOWORD(lparam) == HTCLIENT) {
          shared::g_allow_input_queries = true;
          bool imgui_processed = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
          shared::g_allow_input_queries = false;
          if (imgui_processed) {
            return TRUE;
          }
          SetCursor(LoadCursor(nullptr, IDC_ARROW));
          return TRUE;
        }
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
    }

    // Block keyboard, mouse and raw input events from leaking into the game when overlay is active or in cooldown
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
  void* show_cursor_addr = GetProcAddress(user32, "ShowCursor");

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
  if (show_cursor_addr) {
    success &= CreateAndEnableHook(
        show_cursor_addr,
        reinterpret_cast<void*>(&HookedShowCursor),
        reinterpret_cast<void**>(&g_original_show_cursor));
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
      SetOverlayVisible(!GetOverlayState().show_overlay);
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
  if (g_original_show_cursor) {
    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_show_cursor));
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
  g_original_show_cursor = nullptr;

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

constexpr size_t kMaxClipboardSize = 256 * 1024; // 256KB buffer
static char g_clipboard_buffer[kMaxClipboardSize];

static const char* HookedGetClipboardText(void* /*user_data*/) {
  g_clipboard_buffer[0] = '\0';
  
  // Zero-retry, fail-fast: clipboard is on the render thread.
  // Avoid passing a window HWND as it can cause Windows to coordinate with the busy game thread,
  // causing 1-3s freezes. OpenClipboard(nullptr) is safe and doesn't block.
  if (!OpenClipboard(nullptr)) {
    return nullptr;
  }
  
  HANDLE hData = GetClipboardData(CF_UNICODETEXT);
  if (hData) {
      wchar_t* wtext = (wchar_t*)GlobalLock(hData);
      if (wtext) {
          int wlen = (int)wcslen(wtext);
          int len = WideCharToMultiByte(CP_UTF8, 0, wtext, wlen, nullptr, 0, nullptr, nullptr);
          if (len > 0 && len < kMaxClipboardSize) {
              WideCharToMultiByte(CP_UTF8, 0, wtext, wlen, g_clipboard_buffer, len, nullptr, nullptr);
              g_clipboard_buffer[len] = '\0';
          } else if (len >= kMaxClipboardSize) {
              dover::shared::LogError("Clipboard text too large (%d bytes). Max is %zu.", len, kMaxClipboardSize - 1);
          }
          GlobalUnlock(hData);
      }
  }
  CloseClipboard();
  return g_clipboard_buffer[0] == '\0' ? nullptr : g_clipboard_buffer;
}

static void HookedSetClipboardText(void* /*user_data*/, const char* text) {
  if (!text || text[0] == '\0') return;

  // Run clipboard write synchronously and fail-fast to prevent thread creation overhead and
  // potential clipboard lock contention with background threads.
  if (!OpenClipboard(nullptr)) {
    return;
  }

  EmptyClipboard();
  int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
  if (wlen > 0) {
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
    if (hMem) {
      wchar_t* dest = (wchar_t*)GlobalLock(hMem);
      if (dest) {
        MultiByteToWideChar(CP_UTF8, 0, text, -1, dest, wlen);
        GlobalUnlock(hMem);
        if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
          GlobalFree(hMem);
        }
      } else {
        GlobalFree(hMem);
      }
    }
  }
  CloseClipboard();
}

void OverrideImGuiClipboardFunctions() {
  ImGuiIO& io = ImGui::GetIO();
  io.SetClipboardTextFn = HookedSetClipboardText;
  io.GetClipboardTextFn = HookedGetClipboardText;
}

} // namespace dover::overlay
