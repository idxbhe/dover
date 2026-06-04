#include "shared/input_utils.h"
#include <windows.h>

namespace dover::shared {

thread_local bool g_allow_xinput = false;
thread_local bool g_visualizer_xinput = false;
thread_local bool g_allow_input_queries = false;

KeyStateFunc g_key_state_func = nullptr;

bool IsHardwareKeyPressed(int vKey) {
    if (g_key_state_func) {
        return g_key_state_func(vKey);
    }
    return (GetAsyncKeyState(vKey) & 0x8000) != 0;
}

} // namespace dover::shared
