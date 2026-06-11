#include "shared/input_utils.h"
#include <windows.h>

namespace dover::shared {

std::atomic<bool> g_is_recording_keybind{false};
thread_local bool g_allow_xinput = false;
thread_local bool g_visualizer_xinput = false;
thread_local bool g_allow_input_queries = false;

std::atomic<KeyStateFunc> g_key_state_func = nullptr;

bool IsHardwareKeyPressed(int vKey) {
    auto func = g_key_state_func.load(std::memory_order_acquire);
    if (func) {
        return func(vKey);
    }
    return (GetAsyncKeyState(vKey) & 0x8000) != 0;
}

} // namespace dover::shared
