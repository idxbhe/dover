#pragma once

namespace dover::shared {

extern thread_local bool g_allow_xinput;
extern thread_local bool g_visualizer_xinput;
extern thread_local bool g_allow_input_queries;

using KeyStateFunc = bool(*)(int);
extern KeyStateFunc g_key_state_func;

bool IsHardwareKeyPressed(int vKey);

} // namespace dover::shared
