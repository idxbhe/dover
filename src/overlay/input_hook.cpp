#include "overlay/input_hook.h"
#include "overlay/overlay_ui.h" // For g_show_overlay, g_in_overlay_frame, HookedWndProc
#include "overlay/hook_utils.h"

#include <windows.h>
#include <cstring>
#include <imgui.h>

// Forward declaration of imgui wndproc handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace dover::overlay {

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

bool ProcessInputMessage(LPMSG lpMsg) {
  if (g_show_overlay && lpMsg) {
    if ((lpMsg->message >= WM_MOUSEFIRST && lpMsg->message <= WM_MOUSELAST) ||
        (lpMsg->message >= WM_KEYFIRST && lpMsg->message <= WM_KEYLAST) ||
        lpMsg->message == WM_INPUT) {
      bool is_toggle = (lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000));
      if (!is_toggle) {
        return true; // We want to block it from game
      }
    }
  }
  return false;
}

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
