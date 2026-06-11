#pragma once

namespace dover::overlay {
extern thread_local bool g_in_imgui_new_frame;
bool InitializeInputHooks();
void ShutdownInputHooks();
void PollGamepadToggle();
void PollKeyboardToggle();
void OverrideImGuiClipboardFunctions();
void SetOverlayVisible(bool visible);
void TickInputCooldown();
void UpdateMouseScaling();
} // namespace dover::overlay

