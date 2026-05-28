#pragma once

namespace dover::overlay {
bool InitializeInputHooks();
void ShutdownInputHooks();
void PollGamepadToggle();
bool IsHardwareKeyPressed(int vKey);
extern thread_local bool g_allow_xinput;
extern thread_local bool g_allow_input_queries;
void OverrideImGuiClipboardFunctions();
} // namespace dover::overlay
