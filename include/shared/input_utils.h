#pragma once

#include <atomic>

namespace dover::shared {

extern std::atomic<bool> g_is_recording_keybind;
extern thread_local bool g_allow_xinput;
extern thread_local bool g_visualizer_xinput;
extern thread_local bool g_allow_input_queries;

using KeyStateFunc = bool(*)(int);
extern std::atomic<KeyStateFunc> g_key_state_func;

bool IsHardwareKeyPressed(int vKey);

} // namespace dover::shared
