#pragma once

namespace dover::overlay {
bool InitializeInputHooks();
void ShutdownInputHooks();
void PollGamepadToggle();
void OverrideImGuiClipboardFunctions();
} // namespace dover::overlay
