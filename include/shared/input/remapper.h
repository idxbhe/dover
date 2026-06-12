#pragma once

#include <windows.h>
#include <xinput.h>

namespace dover::shared::input::remapper {

// Intercepts XInput state, generates simulated key events, and zeros out mapped gamepad buttons.
void ProcessGamepadRemapping(XINPUT_STATE* state, DWORD user_index);

} // namespace dover::shared::input::remapper
