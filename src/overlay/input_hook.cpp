#include "overlay/input_hook.h"
#include "shared/settings/app_config.h"
#include "overlay/overlay_ui.h"
#include "overlay/hook_utils.h"
#include "shared/input_mapper.h"
#include "shared/input_utils.h"
#include "shared/log.h"

#include <windows.h>
#include <cstring>
#include <atomic>
#include <string>
#include <thread>
#include <mutex>
#include <imgui.h>
#include <xinput.h>
#include <cmath>
#include <bit>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

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

// DirectInput8 Hook Types
typedef HRESULT(WINAPI* DInput8Acquire_t)(IDirectInputDevice8W* pThis);
typedef ULONG(WINAPI* DInput8Release_t)(IDirectInputDevice8W* pThis);
typedef HRESULT(WINAPI* DInput8GetDeviceState_t)(IDirectInputDevice8W* pThis, DWORD cbData, LPVOID lpvData);
typedef HRESULT(WINAPI* DInput8GetDeviceData_t)(IDirectInputDevice8W* pThis, DWORD cbObjectData, DIDEVICEOBJECTDATA* rgdod, LPDWORD pdwInOut, DWORD dwFlags);
typedef HRESULT(WINAPI* DInput8SetCooperativeLevel_t)(IDirectInputDevice8W* pThis, HWND hwnd, DWORD dwFlags);

static DInput8Acquire_t g_orig_dinput8_acquire = nullptr;
static DInput8Release_t g_orig_dinput8_release = nullptr;
static DInput8GetDeviceState_t g_orig_dinput8_getdevicestate = nullptr;
static DInput8GetDeviceData_t g_orig_dinput8_getdevicedata = nullptr;
static DInput8SetCooperativeLevel_t g_orig_dinput8_setcooperativelevel = nullptr;

static std::mutex g_dinput_mutex;
constexpr size_t kMaxUnacquiredDevices = 512;
static std::atomic<void*> g_unacquired_devices[kMaxUnacquiredDevices];
static std::atomic<size_t> g_unacquired_devices_count{0};
static std::atomic<uint32_t> g_dinput_session_gen{0};

struct DInputThreadCache {
    uint32_t gen = 0;
    void* devices[8] = {nullptr};
    size_t count = 0;
};

static void CheckAndUnacquireDevice(IDirectInputDevice8W* pThis) {
    static thread_local DInputThreadCache cache;
    uint32_t current_gen = g_dinput_session_gen.load(std::memory_order_acquire);

    if (cache.gen != current_gen) {
        cache.gen = current_gen;
        cache.count = 0;
    }

    // Tier 1: TLS-based check (fastest, no atomics/mutex)
    for (size_t i = 0; i < cache.count; ++i) {
        if (cache.devices[i] == pThis) return;
    }

    // Tier 2: Global lock-free search (Double-Checked Locking pattern)
    size_t current_count = g_unacquired_devices_count.load(std::memory_order_acquire);
    for (size_t i = 0; i < current_count; ++i) {
        if (g_unacquired_devices[i].load(std::memory_order_relaxed) == pThis) {
            if (cache.count < 8) cache.devices[cache.count++] = pThis;
            return;
        }
    }

    bool should_unacquire = false;
    {
        // Tier 3: Mutex protected addition (only hit once per device per session)
        std::lock_guard<std::mutex> lock(g_dinput_mutex);
        
        // Re-check after lock
        current_count = g_unacquired_devices_count.load(std::memory_order_relaxed);
        bool found = false;
        for (size_t i = 0; i < current_count; ++i) {
            if (g_unacquired_devices[i].load(std::memory_order_relaxed) == pThis) {
                found = true;
                break;
            }
        }

        if (!found) {
            if (current_count < kMaxUnacquiredDevices) {
                g_unacquired_devices[current_count].store(pThis, std::memory_order_relaxed);
                g_unacquired_devices_count.store(current_count + 1, std::memory_order_release);
                should_unacquire = true;
            } else {
                static std::atomic<bool> logged_warning{false};
                if (!logged_warning.exchange(true)) {
                    shared::LogError("dinput: Maximum unacquired devices limit reached (512). Complex peripheral setups may experience input leakage.");
                }
            }
        }
    }

    if (should_unacquire) {
        pThis->Unacquire();
    }

    if (cache.count < 8) {
        cache.devices[cache.count++] = pThis;
    }
}

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
using SetCursorFn = HCURSOR(WINAPI*)(HCURSOR);

static std::atomic<float> g_mouse_scale_x{1.0f};
static std::atomic<float> g_mouse_scale_y{1.0f};
static std::atomic<bool> g_mouse_scaling_enabled{false};

static void ScaleMouseCoordinates(long& x, long& y) {
    if (g_mouse_scaling_enabled.load(std::memory_order_relaxed)) {
        x = static_cast<long>(x * g_mouse_scale_x.load(std::memory_order_relaxed));
        y = static_cast<long>(y * g_mouse_scale_y.load(std::memory_order_relaxed));
    }
}

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
SetCursorFn g_original_set_cursor = nullptr;

static std::atomic<int> g_logical_cursor_count{0};
static std::atomic<int> g_overlay_cursor_forces{0};
static std::atomic<bool> g_cursor_initialized{false};
static std::atomic<bool> g_last_overlay_visible{false};
static std::atomic<ULONGLONG> g_overlay_closed_time{0};
static std::atomic<uint64_t> g_active_keys_mask[4]{};

static bool IsInputBlocked() {
    if (GetOverlayState().show_overlay.load(std::memory_order_relaxed)) {
        return true;
    }
    
    ULONGLONG closed_time = g_overlay_closed_time.load(std::memory_order_relaxed);
    if (closed_time > 0 && (GetTickCount64() - closed_time < 200)) {
        return true;
    }
    
    return false;
}

bool ProcessInputMessage(LPMSG lpMsg) {
    if (!lpMsg) return false;
    
    auto& cfg = shared::GetAppConfig();
    bool is_toggle = (lpMsg->wParam == (WPARAM)cfg.hotkey_toggle_main);
    if (cfg.hotkey_toggle_modifier != 0 && lpMsg->wParam == (WPARAM)cfg.hotkey_toggle_modifier) {
        is_toggle = true;
    }
    
    // Mask toggle keys from game messages to prevent spam or unintentional actions in games like Skyrim
    if (is_toggle) {
        if ((lpMsg->message >= WM_KEYFIRST && lpMsg->message <= WM_KEYLAST) || lpMsg->message == WM_INPUT) {
            return true;
        }
    }
    
    if (IsInputBlocked()) {
        if ((lpMsg->message >= WM_MOUSEFIRST && lpMsg->message <= WM_MOUSELAST) || 
            (lpMsg->message >= WM_KEYFIRST && lpMsg->message <= WM_KEYLAST) || 
            lpMsg->message == WM_INPUT) {
            return true;
        }
    }
    
    return false;
}

SHORT WINAPI HookedGetAsyncKeyState(int vKey) {
    auto& cfg = shared::GetAppConfig();
    bool is_toggle = (vKey == cfg.hotkey_toggle_main || (cfg.hotkey_toggle_modifier != 0 && vKey == cfg.hotkey_toggle_modifier));
    
    // Stealth: Mask toggle keys from the game engine at all times to prevent fallback-spam in Skyrim
    if (is_toggle && !shared::g_allow_input_queries) {
        return 0;
    }
    
    if (shared::g_allow_input_queries) {
        return g_original_get_async_key_state ? g_original_get_async_key_state(vKey) : 0;
    }
    
    if (IsInputBlocked()) {
        return 0;
    }
    
    return g_original_get_async_key_state ? g_original_get_async_key_state(vKey) : 0;
}

SHORT WINAPI HookedGetKeyState(int nVirtKey) {
    auto& cfg = shared::GetAppConfig();
    bool is_toggle = (nVirtKey == cfg.hotkey_toggle_main || (cfg.hotkey_toggle_modifier != 0 && nVirtKey == cfg.hotkey_toggle_modifier));
    
    if (is_toggle && !shared::g_allow_input_queries) {
        return 0;
    }
    
    if (shared::g_allow_input_queries) {
        return g_original_get_key_state ? g_original_get_key_state(nVirtKey) : 0;
    }
    
    if (IsInputBlocked()) {
        return 0;
    }
    
    return g_original_get_key_state ? g_original_get_key_state(nVirtKey) : 0;
}

BOOL WINAPI HookedGetKeyboardState(PBYTE lpKeyState) {
    if (shared::g_allow_input_queries) {
        return g_original_get_keyboard_state ? g_original_get_keyboard_state(lpKeyState) : FALSE;
    }
    
    if (IsInputBlocked() && lpKeyState) {
        std::memset(lpKeyState, 0, 256);
        return TRUE;
    }
    
    BOOL res = g_original_get_keyboard_state ? g_original_get_keyboard_state(lpKeyState) : FALSE;
    if (res && lpKeyState) {
        auto& cfg = shared::GetAppConfig();
        lpKeyState[cfg.hotkey_toggle_main] = 0;
        if (cfg.hotkey_toggle_modifier != 0) {
            lpKeyState[cfg.hotkey_toggle_modifier] = 0;
        }
    }
    return res;
}

BOOL WINAPI HookedClipCursor(const RECT* lpRect) {
    if (GetOverlayState().show_overlay.load(std::memory_order_relaxed)) {
        if (lpRect) {
            g_game_clip_rect = *lpRect;
            g_game_has_clip_rect = true;
        } else {
            g_game_has_clip_rect = false;
        }
        return TRUE; // Swallow clip request to keep cursor free while overlay is up
    }
    
    if (lpRect) {
        g_game_clip_rect = *lpRect;
        g_game_has_clip_rect = true;
    } else {
        g_game_has_clip_rect = false;
    }
    
    return g_original_clip_cursor ? g_original_clip_cursor(lpRect) : TRUE;
}

BOOL WINAPI HookedGetCursorPos(LPPOINT lpPoint) {
    if (GetOverlayState().show_overlay.load(std::memory_order_relaxed) && !GetOverlayState().in_overlay_frame.load(std::memory_order_relaxed)) {
        HWND hw = GetOverlayState().game_hwnd.load(std::memory_order_acquire);
        if (hw) {
            uint32_t cw = GetOverlayState().client_width.load(std::memory_order_relaxed);
            uint32_t ch = GetOverlayState().client_height.load(std::memory_order_relaxed);
            if (cw > 0 && ch > 0) {
                POINT center = { static_cast<LONG>(cw / 2), static_cast<LONG>(ch / 2) };
                ClientToScreen(hw, &center);
                if (lpPoint) {
                    *lpPoint = center;
                    return TRUE;
                }
            }
        }
    }
    
    if (!lpPoint) return FALSE;
    return g_original_get_cursor_pos ? g_original_get_cursor_pos(lpPoint) : FALSE;
}

BOOL WINAPI HookedScreenToClient(HWND hWnd, LPPOINT lpPoint) {
    BOOL res = g_original_screen_to_client ? g_original_screen_to_client(hWnd, lpPoint) : FALSE;
    if (res && (g_in_imgui_new_frame || GetOverlayState().in_overlay_frame.load(std::memory_order_relaxed)) && lpPoint) {
        ScaleMouseCoordinates(lpPoint->x, lpPoint->y);
    }
    return res;
}

BOOL WINAPI HookedSetCursorPos(int x, int y) {
    if (GetOverlayState().show_overlay.load(std::memory_order_relaxed) && !GetOverlayState().in_overlay_frame.load(std::memory_order_relaxed)) {
        return TRUE;
    }
    return g_original_set_cursor_pos ? g_original_set_cursor_pos(x, y) : FALSE;
}

HCURSOR WINAPI HookedSetCursor(HCURSOR hCursor) {
    if (GetOverlayState().show_overlay.load(std::memory_order_relaxed) && hCursor == nullptr) {
        return LoadCursor(nullptr, IDC_ARROW);
    }
    return g_original_set_cursor ? g_original_set_cursor(hCursor) : hCursor;
}

BOOL WINAPI HookedPeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    if (!g_original_peek_message_w) return FALSE;
    BOOL result = g_original_peek_message_w(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    if (result && ProcessInputMessage(lpMsg)) {
        if (wRemoveMsg & PM_REMOVE) {
            TranslateMessage(lpMsg);
            DispatchMessageW(lpMsg);
        }
        lpMsg->message = WM_NULL;
    }
    return result;
}

BOOL WINAPI HookedPeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    if (!g_original_peek_message_a) return FALSE;
    BOOL result = g_original_peek_message_a(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    if (result && ProcessInputMessage(lpMsg)) {
        if (wRemoveMsg & PM_REMOVE) {
            TranslateMessage(lpMsg);
            DispatchMessageA(lpMsg);
        }
        lpMsg->message = WM_NULL;
    }
    return result;
}

BOOL WINAPI HookedGetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    if (!g_original_get_message_w) return -1;
    BOOL result = g_original_get_message_w(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
    if (result > 0 && ProcessInputMessage(lpMsg)) {
        TranslateMessage(lpMsg);
        DispatchMessageW(lpMsg);
        lpMsg->message = WM_NULL;
    }
    return result;
}

BOOL WINAPI HookedGetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    if (!g_original_get_message_a) return -1;
    BOOL result = g_original_get_message_a(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
    if (result > 0 && ProcessInputMessage(lpMsg)) {
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
    std::atomic<bool> l_s{false};
    std::atomic<bool> r_s{false};
    std::atomic<bool> l_t{false};
    std::atomic<bool> r_t{false};
};
static LatchedAnalogState g_latched_analogs[4];

void ModifyXInputState(DWORD dwUserIndex, XINPUT_STATE* pState) {
    if (shared::g_visualizer_xinput || dwUserIndex >= 4) return;
    
    WORD hw_buttons = pState->Gamepad.wButtons;
    
    if (shared::g_allow_xinput) {
        if (GetOverlayState().show_overlay) {
            g_latched_buttons[dwUserIndex].fetch_or(hw_buttons);
            if (std::abs(pState->Gamepad.sThumbLX) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE || 
                std::abs(pState->Gamepad.sThumbLY) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) {
                g_latched_analogs[dwUserIndex].l_s = true;
            }
            if (std::abs(pState->Gamepad.sThumbRX) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE || 
                std::abs(pState->Gamepad.sThumbRY) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) {
                g_latched_analogs[dwUserIndex].r_s = true;
            }
            if (pState->Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
                g_latched_analogs[dwUserIndex].l_t = true;
            }
            if (pState->Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
                g_latched_analogs[dwUserIndex].r_t = true;
            }
        } else {
            // Mask everything from game while we have focus
            pState->Gamepad.wButtons = 0;
            pState->Gamepad.bLeftTrigger = 0;
            pState->Gamepad.bRightTrigger = 0;
            pState->Gamepad.sThumbLX = 0;
            pState->Gamepad.sThumbLY = 0;
            pState->Gamepad.sThumbRX = 0;
            pState->Gamepad.sThumbRY = 0;
        }
    } else {
        if (GetOverlayState().show_overlay) {
            // Overlay visible but not focused (XInput allow is false)
            pState->Gamepad.wButtons = 0;
            pState->Gamepad.bLeftTrigger = 0;
            pState->Gamepad.bRightTrigger = 0;
            pState->Gamepad.sThumbLX = 0;
            pState->Gamepad.sThumbLY = 0;
            pState->Gamepad.sThumbRX = 0;
            pState->Gamepad.sThumbRY = 0;
            g_latched_buttons[dwUserIndex].fetch_or(hw_buttons);
        } else {
            // Game has focus, but we need to prevent sticky inputs from when the overlay was up
            WORD prev_latched = g_latched_buttons[dwUserIndex].fetch_and(hw_buttons);
            WORD still_pressed = prev_latched & hw_buttons;
            pState->Gamepad.wButtons &= ~still_pressed;
            
            if (g_latched_analogs[dwUserIndex].l_s) {
                if (std::abs(pState->Gamepad.sThumbLX) <= XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE && 
                    std::abs(pState->Gamepad.sThumbLY) <= XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) {
                    g_latched_analogs[dwUserIndex].l_s = false;
                } else {
                    pState->Gamepad.sThumbLX = 0;
                    pState->Gamepad.sThumbLY = 0;
                }
            }
            if (g_latched_analogs[dwUserIndex].r_s) {
                if (std::abs(pState->Gamepad.sThumbRX) <= XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE && 
                    std::abs(pState->Gamepad.sThumbRY) <= XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) {
                    g_latched_analogs[dwUserIndex].r_s = false;
                } else {
                    pState->Gamepad.sThumbRX = 0;
                    pState->Gamepad.sThumbRY = 0;
                }
            }
            if (g_latched_analogs[dwUserIndex].l_t) {
                if (pState->Gamepad.bLeftTrigger <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
                    g_latched_analogs[dwUserIndex].l_t = false;
                } else {
                    pState->Gamepad.bLeftTrigger = 0;
                }
            }
            if (g_latched_analogs[dwUserIndex].r_t) {
                if (pState->Gamepad.bRightTrigger <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
                    g_latched_analogs[dwUserIndex].r_t = false;
                } else {
                    pState->Gamepad.bRightTrigger = 0;
                }
            }
            
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

HRESULT WINAPI HookedDInput8Acquire(IDirectInputDevice8W* pThis) {
    if (IsInputBlocked() && !shared::g_allow_input_queries) {
        return DI_OK;
    }
    return g_orig_dinput8_acquire ? g_orig_dinput8_acquire(pThis) : DI_OK;
}

ULONG WINAPI HookedDInput8Release(IDirectInputDevice8W* pThis) {
    {
        std::lock_guard<std::mutex> lock(g_dinput_mutex);
        size_t count = g_unacquired_devices_count.load(std::memory_order_relaxed);
        for (size_t i = 0; i < count; ++i) {
            if (g_unacquired_devices[i].load(std::memory_order_relaxed) == pThis) {
                g_unacquired_devices[i].store(g_unacquired_devices[count - 1].load(std::memory_order_relaxed), std::memory_order_relaxed);
                g_unacquired_devices_count.store(count - 1, std::memory_order_relaxed);
                break;
            }
        }
    }
    return g_orig_dinput8_release ? g_orig_dinput8_release(pThis) : 0;
}

HRESULT WINAPI HookedDInput8SetCooperativeLevel(IDirectInputDevice8W* pThis, HWND hwnd, DWORD dwFlags) {
    // Force non-exclusive to ensure we can always override/hook
    if (dwFlags & DISCL_EXCLUSIVE) {
        dwFlags &= ~DISCL_EXCLUSIVE;
        dwFlags |= DISCL_NONEXCLUSIVE;
    }
    return g_orig_dinput8_setcooperativelevel ? g_orig_dinput8_setcooperativelevel(pThis, hwnd, dwFlags) : DI_OK;
}

static BYTE GetDInputScanCode(int vk) {
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    switch (vk) {
        case VK_RCONTROL: sc = 0x9D; break;
        case VK_RMENU:    sc = 0xB8; break;
        case VK_INSERT:   sc = 0xD2; break;
        case VK_DELETE:   sc = 0xD3; break;
        case VK_HOME:     sc = 0xC7; break;
        case VK_END:      sc = 0xCF; break;
        case VK_PRIOR:    sc = 0xC9; break; // Page Up
        case VK_NEXT:     sc = 0xD1; break; // Page Down
        case VK_LEFT:     sc = 0xCB; break;
        case VK_RIGHT:    sc = 0xCD; break;
        case VK_UP:       sc = 0xC8; break;
        case VK_DOWN:     sc = 0xD0; break;
        case VK_DIVIDE:   sc = 0xB5; break;
    }
    return static_cast<BYTE>(sc & 0xFF);
}

HRESULT WINAPI HookedDInput8GetDeviceState(IDirectInputDevice8W* pThis, DWORD cbData, LPVOID lpvData) {
    if (IsInputBlocked() && !shared::g_allow_input_queries) {
        CheckAndUnacquireDevice(pThis);
        if (lpvData) std::memset(lpvData, 0, cbData);
        return DI_OK;
    }
    
    HRESULT hr = g_orig_dinput8_getdevicestate ? g_orig_dinput8_getdevicestate(pThis, cbData, lpvData) : DI_OK;
    
    if (SUCCEEDED(hr) && cbData == 256 && lpvData) {
        auto& cfg = shared::GetAppConfig();
        BYTE main_key_sc = GetDInputScanCode(cfg.hotkey_toggle_main);
        static_cast<BYTE*>(lpvData)[main_key_sc] = 0;
        
        if (cfg.hotkey_toggle_modifier != 0) {
            BYTE mod_key_sc = GetDInputScanCode(cfg.hotkey_toggle_modifier);
            static_cast<BYTE*>(lpvData)[mod_key_sc] = 0;
        }
    }
    return hr;
}

HRESULT WINAPI HookedDInput8GetDeviceData(IDirectInputDevice8W* pThis, DWORD cbObjectData, DIDEVICEOBJECTDATA* rgdod, LPDWORD pdwInOut, DWORD dwFlags) {
    if (IsInputBlocked() && !shared::g_allow_input_queries) {
        CheckAndUnacquireDevice(pThis);
        if (pdwInOut) *pdwInOut = 0;
        return DI_OK;
    }
    
    HRESULT hr = g_orig_dinput8_getdevicedata ? g_orig_dinput8_getdevicedata(pThis, cbObjectData, rgdod, pdwInOut, dwFlags) : DI_OK;
    
    if (SUCCEEDED(hr) && rgdod && pdwInOut) {
        auto& cfg = shared::GetAppConfig();
        BYTE main_key_sc = GetDInputScanCode(cfg.hotkey_toggle_main);
        BYTE mod_key_sc = (cfg.hotkey_toggle_modifier != 0) ? GetDInputScanCode(cfg.hotkey_toggle_modifier) : 0xFF;
        
        DWORD count = *pdwInOut;
        DWORD write_idx = 0;
        
        for (DWORD i = 0; i < count; ++i) {
            BYTE ofs = static_cast<BYTE>(rgdod[i].dwOfs);
            if (ofs != main_key_sc && ofs != mod_key_sc) {
                if (write_idx != i) {
                    rgdod[write_idx] = rgdod[i];
                }
                write_idx++;
            }
        }
        *pdwInOut = write_idx;
    }
    return hr;
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
    if (GetOverlayState().show_overlay.load(std::memory_order_relaxed)) {
        if (bShow) {
            return ++g_logical_cursor_count;
        } else {
            return --g_logical_cursor_count;
        }
    }
    
    if (g_original_show_cursor) {
        int ret = g_original_show_cursor(bShow);
        g_logical_cursor_count.store(ret, std::memory_order_relaxed);
        return ret;
    }
    
    return g_logical_cursor_count.load(std::memory_order_relaxed);
}

void UpdateOverlayVisibilityState() {
    bool vis = GetOverlayState().show_overlay.load(std::memory_order_relaxed);
    if (g_last_overlay_visible.exchange(vis, std::memory_order_relaxed) == vis) return;
    
    if (!g_original_show_cursor) return;

    if (vis) {
        if (g_original_clip_cursor) g_original_clip_cursor(nullptr);
        if (g_original_set_cursor) g_original_set_cursor(LoadCursor(nullptr, IDC_ARROW));
        
        // Force OS cursor to be visible for overlay interaction
        int current = g_original_show_cursor(TRUE);
        int forces = 1;
        while (current < 0) {
            current = g_original_show_cursor(TRUE);
            forces++;
        }
        g_overlay_cursor_forces.store(forces, std::memory_order_relaxed);
    } else {
        if (g_game_has_clip_rect && g_original_clip_cursor) {
            g_original_clip_cursor(&g_game_clip_rect);
        }
        
        // 1. Revert Dover's explicit forces
        int forces = g_overlay_cursor_forces.exchange(0, std::memory_order_relaxed);
        for (int i = 0; i < forces; ++i) {
            g_original_show_cursor(FALSE);
        }

        // 2. Brutal Reconciliation: Sync physical OS state with logical engine state
        int logical = g_logical_cursor_count.load(std::memory_order_relaxed);
        
        // Use a temp increment/decrement to find the current physical state 
        // and then drive it exactly to the logical state.
        int current = g_original_show_cursor(TRUE);
        while (current < logical) {
            current = g_original_show_cursor(TRUE);
        }
        while (current > logical) {
            current = g_original_show_cursor(FALSE);
        }

        // 3. Force-clear the cursor icon and poke the engine to re-evaluate its state
        // This is critical for games like Skyrim LE that use a mix of DirectInput and Win32 cursor management.
        if (g_original_set_cursor) g_original_set_cursor(nullptr);
        HWND hw = GetOverlayState().game_hwnd.load(std::memory_order_relaxed);
        if (hw) {
            ::PostMessageW(hw, WM_SETCURSOR, (WPARAM)hw, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        }
    }
}

static std::atomic<bool> g_sticky_reset_pending{false};

static void ResetStickyInputs() {
    HWND hw = GetOverlayState().game_hwnd;
    if (!hw) return;
    
    for (int i = 0; i < 4; ++i) {
        uint64_t mask = g_active_keys_mask[i].exchange(0, std::memory_order_acquire);
        while (mask) {
            int bit = std::countr_zero(mask);
            int vk = (i << 6) | bit;
            
            bool is_up = false;
            if (g_original_get_async_key_state) {
                is_up = (g_original_get_async_key_state(vk) & 0x8000) == 0;
            } else {
                is_up = (GetAsyncKeyState(vk) & 0x8000) == 0;
            }
            
            if (is_up) {
                if (vk == VK_LBUTTON) PostMessageW(hw, WM_LBUTTONUP, 0, 0);
                else if (vk == VK_RBUTTON) PostMessageW(hw, WM_RBUTTONUP, 0, 0);
                else if (vk == VK_MBUTTON) PostMessageW(hw, WM_MBUTTONUP, 0, 0);
                else if (vk == VK_XBUTTON1) PostMessageW(hw, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), 0);
                else if (vk == VK_XBUTTON2) PostMessageW(hw, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), 0);
                else {
                    LPARAM lp = static_cast<LPARAM>(0xC0000001U);
                    lp |= (static_cast<LPARAM>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)) << 16);
                    PostMessageW(hw, WM_KEYUP, vk, lp);
                }
            }
            
            mask &= ~(1ULL << bit);
        }
    }
}
static void UpdateClientDimensions(HWND hwnd) {
    RECT r = {};
    if (::GetClientRect(hwnd, &r)) {
        GetOverlayState().client_width.store(static_cast<uint32_t>(r.right - r.left), std::memory_order_relaxed);
        GetOverlayState().client_height.store(static_cast<uint32_t>(r.bottom - r.top), std::memory_order_relaxed);
        UpdateMouseScaling();
    }
}
} // namespace

void UpdateMouseScaling() {
    uint32_t cw = GetOverlayState().client_width.load(std::memory_order_relaxed);
    uint32_t ch = GetOverlayState().client_height.load(std::memory_order_relaxed);
    uint32_t sw = GetOverlayState().swapchain_width.load(std::memory_order_relaxed);
    uint32_t sh = GetOverlayState().swapchain_height.load(std::memory_order_relaxed);
    
    if (sw > 0 && sh > 0 && cw > 0 && ch > 0 && (cw != sw || ch != sh)) {
        g_mouse_scale_x.store(static_cast<float>(sw) / cw, std::memory_order_relaxed);
        g_mouse_scale_y.store(static_cast<float>(sh) / ch, std::memory_order_relaxed);
        g_mouse_scaling_enabled.store(true, std::memory_order_relaxed);
    } else {
        g_mouse_scaling_enabled.store(false, std::memory_order_relaxed);
    }
}

void TickInputCooldown() {
    if (!g_sticky_reset_pending.load(std::memory_order_relaxed)) return;
    
    ULONGLONG t = g_overlay_closed_time.load(std::memory_order_relaxed);
    if (t > 0 && (GetTickCount64() - t >= 200)) {
        ResetStickyInputs();
        g_sticky_reset_pending.store(false, std::memory_order_relaxed);
    }
}

void SetOverlayVisible(bool visible) {
    if (GetOverlayState().show_overlay == visible) return;
    
    if (!visible) {
        g_overlay_closed_time.store(GetTickCount64(), std::memory_order_relaxed);
        g_sticky_reset_pending.store(true, std::memory_order_relaxed);
        
        // Restore DirectInput devices
        void* devices_to_restore[kMaxUnacquiredDevices];
        size_t count_to_restore = 0;
        {
            std::lock_guard<std::mutex> lock(g_dinput_mutex);
            count_to_restore = g_unacquired_devices_count.load(std::memory_order_relaxed);
            if (count_to_restore > 0) {
                for (size_t i = 0; i < count_to_restore; ++i) {
                    devices_to_restore[i] = g_unacquired_devices[i].load(std::memory_order_relaxed);
                }
                g_unacquired_devices_count.store(0, std::memory_order_relaxed);
            }
        }
        for (size_t i = 0; i < count_to_restore; ++i) {
            static_cast<IDirectInputDevice8W*>(devices_to_restore[i])->Acquire();
        }
    } else {
        HWND hw = GetOverlayState().game_hwnd;
        if (hw) {
            ::ReleaseCapture();
            ::SetForegroundWindow(hw);
            ::SetActiveWindow(hw);
            UpdateClientDimensions(hw);
            
            // Snapshot current keyboard state to track keys that might need resetting later
            BYTE keys[256];
            if (::GetKeyboardState(keys)) {
                for (int i = 0; i < 256; ++i) {
                    if (keys[i] & 0x80) {
                        g_active_keys_mask[i >> 6].fetch_or(1ULL << (i & 63), std::memory_order_relaxed);
                    }
                }
            }
        }
        g_dinput_session_gen.fetch_add(1, std::memory_order_release);
    }
    
    GetOverlayState().show_overlay.store(visible, std::memory_order_release);
    
    HWND hw = GetOverlayState().game_hwnd.load(std::memory_order_acquire);
    if (hw) {
        if (GetCurrentThreadId() == GetWindowThreadProcessId(hw, nullptr)) {
            UpdateOverlayVisibilityState();
        } else {
            PostMessageW(hw, WM_USER + 0x1337, 0, 0);
        }
    }
}

LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto& state = GetOverlayState();
    bool show_overlay = state.show_overlay.load(std::memory_order_acquire);
    
    if (!state.game_hwnd.load(std::memory_order_relaxed)) {
        state.game_hwnd.store(hwnd, std::memory_order_release);
        UpdateClientDimensions(hwnd);
    }
    
    if (msg == WM_SIZE || msg == WM_WINDOWPOSCHANGED) {
        UpdateClientDimensions(hwnd);
    }
    
    if (!g_cursor_initialized.load(std::memory_order_relaxed)) {
        if (g_original_show_cursor) {
            int cur = g_original_show_cursor(TRUE);
            g_original_show_cursor(FALSE);
            g_logical_cursor_count.store(cur - 1, std::memory_order_relaxed);
            g_cursor_initialized.store(true, std::memory_order_relaxed);
        }
    }
    
    UpdateOverlayVisibilityState();
    
    if (msg == WM_USER + 0x1337) {
        return 0;
    }
    
    if (msg == WM_ACTIVATE || msg == WM_ACTIVATEAPP) {
        bool inactive = (msg == WM_ACTIVATE) ? (LOWORD(wparam) == WA_INACTIVE) : (wparam == FALSE);
        if (inactive && show_overlay) {
            SetOverlayVisible(false);
        }
    }
    
    if (msg == WM_SETCURSOR) {
        if (show_overlay) {
            if (LOWORD(lparam) == HTCLIENT) {
                shared::g_allow_input_queries = true;
                state.in_overlay_frame.store(true, std::memory_order_relaxed);
                bool processed = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
                state.in_overlay_frame.store(false, std::memory_order_relaxed);
                shared::g_allow_input_queries = false;
                
                if (processed) return TRUE;
                if (g_original_set_cursor) g_original_set_cursor(LoadCursor(nullptr, IDC_ARROW));
                return TRUE;
            }
        } else {
            // Stealth Cursor: If engine thinks cursor is hidden, prevent OS from showing it
            if (LOWORD(lparam) == HTCLIENT && g_logical_cursor_count.load(std::memory_order_relaxed) < 0) {
                if (g_original_set_cursor) g_original_set_cursor(nullptr);
                return TRUE;
            }
        }
    }
    
    // Track all keys and mouse buttons to prevent sticky states during overlay transitions
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN) {
        int vk = 0;
        if (msg == WM_LBUTTONDOWN) vk = VK_LBUTTON;
        else if (msg == WM_RBUTTONDOWN) vk = VK_RBUTTON;
        else if (msg == WM_MBUTTONDOWN) vk = VK_MBUTTON;
        else if (msg == WM_XBUTTONDOWN) vk = (GET_XBUTTON_WPARAM(wparam) == XBUTTON1) ? VK_XBUTTON1 : VK_XBUTTON2;
        else vk = static_cast<int>(wparam);

        if (vk > 0 && vk < 256) {
            g_active_keys_mask[vk >> 6].fetch_or(1ULL << (vk & 63), std::memory_order_relaxed);
        }
    } else if (msg == WM_KEYUP || msg == WM_SYSKEYUP || msg == WM_LBUTTONUP || msg == WM_RBUTTONUP || msg == WM_MBUTTONUP || msg == WM_XBUTTONUP) {
        int vk = 0;
        if (msg == WM_LBUTTONUP) vk = VK_LBUTTON;
        else if (msg == WM_RBUTTONUP) vk = VK_RBUTTON;
        else if (msg == WM_MBUTTONUP) vk = VK_MBUTTON;
        else if (msg == WM_XBUTTONUP) vk = (GET_XBUTTON_WPARAM(wparam) == XBUTTON1) ? VK_XBUTTON1 : VK_XBUTTON2;
        else vk = static_cast<int>(wparam);

        if (vk > 0 && vk < 256) {
            g_active_keys_mask[vk >> 6].fetch_and(~(1ULL << (vk & 63)), std::memory_order_relaxed);
        }
    }

    if (IsInputBlocked()) {
        if (show_overlay) {
            // WM_SETCURSOR handled above
            
            // Handle coordinate scaling if swapchain and window size mismatch
            if ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) && msg != WM_MOUSEWHEEL && msg != WM_MOUSEHWHEEL) {
                long x = static_cast<short>(LOWORD(lparam));
                long y = static_cast<short>(HIWORD(lparam));
                ScaleMouseCoordinates(x, y);
                lparam = MAKELPARAM(static_cast<short>(x), static_cast<short>(y));
            }
            
            shared::g_allow_input_queries = true;
            state.in_overlay_frame.store(true, std::memory_order_relaxed);
            bool processed = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
            state.in_overlay_frame.store(false, std::memory_order_relaxed);
            shared::g_allow_input_queries = false;
            
            if (processed) return TRUE;
        }
        
        // Block leaking input to game
        if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) return 1;
        if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) return 1;
        if (msg == WM_INPUT) return 1;
    }
    
    WNDPROC orig = state.original_wnd_proc.load(std::memory_order_acquire);
    if (!orig) return DefWindowProcW(hwnd, msg, wparam, lparam);
    return CallWindowProcW(orig, hwnd, msg, wparam, lparam);
}

bool InitializeInputHooks() {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (!u32) return false;
    
    auto HookU32 = [&](const char* name, void* dest, void** orig) {
        void* addr = GetProcAddress(u32, name);
        if (addr) {
            if (!CreateAndEnableHook(addr, dest, orig)) return false;
            
            // Special case: publish the original pointer to shared utils immediately for robust polling
            if (strcmp(name, "GetAsyncKeyState") == 0) {
                shared::g_key_state_func.store([](int vk) -> bool {
                    if (g_original_get_async_key_state) {
                        return (g_original_get_async_key_state(vk) & 0x8000) != 0;
                    }
                    return (GetAsyncKeyState(vk) & 0x8000) != 0;
                }, std::memory_order_release);
            }
        }
        return true;
    };
    
    bool success = true;
    success &= HookU32("GetAsyncKeyState", (void*)&HookedGetAsyncKeyState, (void**)&g_original_get_async_key_state);
    success &= HookU32("GetKeyState", (void*)&HookedGetKeyState, (void**)&g_original_get_key_state);
    success &= HookU32("GetKeyboardState", (void*)&HookedGetKeyboardState, (void**)&g_original_get_keyboard_state);
    success &= HookU32("ClipCursor", (void*)&HookedClipCursor, (void**)&g_original_clip_cursor);
    success &= HookU32("GetCursorPos", (void*)&HookedGetCursorPos, (void**)&g_original_get_cursor_pos);
    success &= HookU32("SetCursorPos", (void*)&HookedSetCursorPos, (void**)&g_original_set_cursor_pos);
    success &= HookU32("SetCursor", (void*)&HookedSetCursor, (void**)&g_original_set_cursor);
    success &= HookU32("PeekMessageW", (void*)&HookedPeekMessageW, (void**)&g_original_peek_message_w);
    success &= HookU32("PeekMessageA", (void*)&HookedPeekMessageA, (void**)&g_original_peek_message_a);
    success &= HookU32("GetMessageW", (void*)&HookedGetMessageW, (void**)&g_original_get_message_w);
    success &= HookU32("GetMessageA", (void*)&HookedGetMessageA, (void**)&g_original_get_message_a);
    success &= HookU32("ScreenToClient", (void*)&HookedScreenToClient, (void**)&g_original_screen_to_client);
    success &= HookU32("ShowCursor", (void*)&HookedShowCursor, (void**)&g_original_show_cursor);
    
    // DirectInput8 hooking
    HMODULE di8 = GetModuleHandleW(L"dinput8.dll");
    if (!di8) di8 = LoadLibraryW(L"dinput8.dll");
    if (di8) {
        using DI8CFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
        auto create_fn = (DI8CFn)GetProcAddress(di8, "DirectInput8Create");
        
        if (create_fn) {
            auto HookDI8 = [&](REFIID iid) {
                IDirectInput8W* di = nullptr;
                if (SUCCEEDED(create_fn(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, iid, (LPVOID*)&di, nullptr)) && di) {
                    IDirectInputDevice8W* dev = nullptr;
                    if (SUCCEEDED(di->CreateDevice(GUID_SysKeyboard, &dev, nullptr)) && dev) {
                        void** vtable = *reinterpret_cast<void***>(dev);
                        void* dummy_orig = nullptr;
                        
                        if (CreateAndEnableHook(vtable[2], (void*)&HookedDInput8Release, &dummy_orig) && dummy_orig) {
                            if (!g_orig_dinput8_release) g_orig_dinput8_release = (DInput8Release_t)dummy_orig;
                        }
                        if (CreateAndEnableHook(vtable[7], (void*)&HookedDInput8Acquire, &dummy_orig) && dummy_orig) {
                            if (!g_orig_dinput8_acquire) g_orig_dinput8_acquire = (DInput8Acquire_t)dummy_orig;
                        }
                        if (CreateAndEnableHook(vtable[9], (void*)&HookedDInput8GetDeviceState, &dummy_orig) && dummy_orig) {
                            if (!g_orig_dinput8_getdevicestate) g_orig_dinput8_getdevicestate = (DInput8GetDeviceState_t)dummy_orig;
                        }
                        if (CreateAndEnableHook(vtable[10], (void*)&HookedDInput8GetDeviceData, &dummy_orig) && dummy_orig) {
                            if (!g_orig_dinput8_getdevicedata) g_orig_dinput8_getdevicedata = (DInput8GetDeviceData_t)dummy_orig;
                        }
                        if (CreateAndEnableHook(vtable[13], (void*)&HookedDInput8SetCooperativeLevel, &dummy_orig) && dummy_orig) {
                            if (!g_orig_dinput8_setcooperativelevel) g_orig_dinput8_setcooperativelevel = (DInput8SetCooperativeLevel_t)dummy_orig;
                        }
                        dev->Release();
                    }
                    di->Release();
                }
            };
            HookDI8(IID_IDirectInput8W);
            HookDI8(IID_IDirectInput8A);
        }
    }
    
    // XInput Hooking
    auto HookXI = [](const wchar_t* dll, void* dest, void** orig) {
        HMODULE h = GetModuleHandleW(dll);
        if (!h) h = LoadLibraryW(dll);
        if (h) {
            void* addr = GetProcAddress(h, "XInputGetState");
            if (addr) CreateAndEnableHook(addr, dest, orig);
        }
    };
    HookXI(L"xinput1_4.dll", (void*)&HookedXInput14GetState, (void**)&g_orig_xinput14_getstate);
    HookXI(L"xinput1_3.dll", (void*)&HookedXInput13GetState, (void**)&g_orig_xinput13_getstate);
    
    return success;
}

void PollGamepadToggle() {
    if (!g_xinput_initialized) {
        const wchar_t* dlls[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
        for (auto dll : dlls) {
            g_hXInputDll = LoadLibraryW(dll);
            if (g_hXInputDll) {
                g_XInputGetStateEx = (XInputGetStateEx_t)GetProcAddress(g_hXInputDll, (LPCSTR)100);
                if (g_XInputGetStateEx) break;
                FreeLibrary(g_hXInputDll);
                g_hXInputDll = nullptr;
            }
        }
        g_xinput_initialized = true;
    }
    
    if (!g_XInputGetStateEx) return;
    
    XINPUT_STATE s = {};
    if (g_XInputGetStateEx(0, &s) == ERROR_SUCCESS) {
        bool guide_pressed = (s.Gamepad.wButtons & XINPUT_GAMEPAD_GUIDE) != 0;
        if (guide_pressed && !g_prev_guide_pressed) {
            SetOverlayVisible(!GetOverlayState().show_overlay);
        }
        g_prev_guide_pressed = guide_pressed;
    }
}

void PollKeyboardToggle() {
    static bool s_recording_last = false;
    static ULONGLONG s_suppress_until = 0;
    static bool s_toggle_latch = false;
    static ULONGLONG s_last_toggle_time = 0;
    
    bool recording = shared::g_is_recording_keybind.load(std::memory_order_relaxed);
    
    if (recording) {
        s_recording_last = true;
        return;
    }
    
    if (s_recording_last) {
        s_recording_last = false;
        s_suppress_until = GetTickCount64() + 500;
        s_toggle_latch = true;
    }
    
    if (GetTickCount64() < s_suppress_until) return;
    
    auto& cfg = shared::GetAppConfig();
    bool mod_pressed = cfg.hotkey_toggle_modifier == 0 || shared::IsHardwareKeyPressed(cfg.hotkey_toggle_modifier);
    bool main_pressed = shared::IsHardwareKeyPressed(cfg.hotkey_toggle_main);
    
    if (main_pressed && mod_pressed) {
        if (!s_toggle_latch) {
            s_toggle_latch = true;
            if (GetTickCount64() - s_last_toggle_time > 200) {
                SetOverlayVisible(!GetOverlayState().show_overlay);
                s_last_toggle_time = GetTickCount64();
            }
        }
    } else {
        s_toggle_latch = false;
    }
}

void ShutdownInputHooks() {
    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_async_key_state));
    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_key_state));
    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_keyboard_state));
    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_clip_cursor));
    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_cursor_pos));
    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_set_cursor_pos));
    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_set_cursor));
    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_peek_message_w));
    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_peek_message_a));
    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_message_w));
    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_message_a));
    if (g_original_screen_to_client) DisableAndRemoveHook(reinterpret_cast<void*>(g_original_screen_to_client));
    if (g_original_show_cursor) DisableAndRemoveHook(reinterpret_cast<void*>(g_original_show_cursor));
    
    shared::g_key_state_func.store(nullptr, std::memory_order_release);
    g_original_get_async_key_state = nullptr;
    g_original_get_key_state = nullptr;
    g_original_get_keyboard_state = nullptr;
    g_original_clip_cursor = nullptr;
    g_original_get_cursor_pos = nullptr;
    g_original_set_cursor_pos = nullptr;
    g_original_set_cursor = nullptr;
    g_original_peek_message_w = nullptr;
    g_original_peek_message_a = nullptr;
    g_original_get_message_w = nullptr;
    g_original_get_message_a = nullptr;
    g_original_screen_to_client = nullptr;
    g_original_show_cursor = nullptr;
    
    if (g_orig_xinput14_getstate) DisableAndRemoveHook(reinterpret_cast<void*>(g_orig_xinput14_getstate));
    if (g_orig_xinput13_getstate) DisableAndRemoveHook(reinterpret_cast<void*>(g_orig_xinput13_getstate));
    
    if (g_hXInputDll) {
        if (g_xinput_initialized) FreeLibrary(g_hXInputDll);
        g_hXInputDll = nullptr;
        g_XInputGetStateEx = nullptr;
        g_xinput_initialized = false;
    }
}

constexpr size_t kMaxClipboardSize = 256 * 1024;
static char g_clipboard_buffer[kMaxClipboardSize];

static const char* HookedGetClipboardText(ImGuiContext*) {
    g_clipboard_buffer[0] = '\0';
    if (!OpenClipboard(nullptr)) return nullptr;
    
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        wchar_t* wtext = (wchar_t*)GlobalLock(hData);
        if (wtext) {
            int wlen = (int)wcslen(wtext);
            int len = WideCharToMultiByte(CP_UTF8, 0, wtext, wlen, nullptr, 0, nullptr, nullptr);
            if (len > 0 && len < kMaxClipboardSize) {
                WideCharToMultiByte(CP_UTF8, 0, wtext, wlen, g_clipboard_buffer, len, nullptr, nullptr);
                g_clipboard_buffer[len] = '\0';
            }
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return g_clipboard_buffer[0] == '\0' ? nullptr : g_clipboard_buffer;
}

static void HookedSetClipboardText(ImGuiContext*, const char* text) {
    if (!text || text[0] == '\0') return;
    if (!OpenClipboard(nullptr)) return;
    
    EmptyClipboard();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (wlen > 0) {
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
        if (hMem) {
            wchar_t* dest = (wchar_t*)GlobalLock(hMem);
            if (dest) {
                MultiByteToWideChar(CP_UTF8, 0, text, -1, dest, wlen);
                GlobalUnlock(hMem);
                if (!SetClipboardData(CF_UNICODETEXT, hMem)) GlobalFree(hMem);
            } else {
                GlobalFree(hMem);
            }
        }
    }
    CloseClipboard();
}

void OverrideImGuiClipboardFunctions() {
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Platform_SetClipboardTextFn = HookedSetClipboardText;
    platform_io.Platform_GetClipboardTextFn = HookedGetClipboardText;
}

} // namespace dover::overlay
