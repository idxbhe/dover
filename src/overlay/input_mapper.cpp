#include "overlay/input_mapper.h"
#include "overlay/overlay_ui.h"

namespace dover::overlay::input_mapper {

static DWORD g_prev_virtual_buttons[XUSER_MAX_COUNT] = {0};

void ProcessGamepadRemapping(XINPUT_STATE* state, DWORD user_index) {
    if (user_index >= XUSER_MAX_COUNT) return;

    const auto& config = GetOverlayConfig();
    
    // Combine standard wButtons with trigger states into a virtual 32-bit map
    // Bits 0-15 = wButtons
    // Bit 16 = Left Trigger (>30)
    // Bit 17 = Right Trigger (>30)
    DWORD current_buttons = state->Gamepad.wButtons;
    if (state->Gamepad.bLeftTrigger > 30) current_buttons |= (1 << 16);
    if (state->Gamepad.bRightTrigger > 30) current_buttons |= (1 << 17);
    
    DWORD prev_buttons = g_prev_virtual_buttons[user_index];

    // Detect edges
    DWORD changed = current_buttons ^ prev_buttons;
    if (!changed) {
        // Even if no state changed, we must still zero out any currently held mapped buttons
        // so the game doesn't process them.
        for (int i = 0; i < 18; ++i) {
            if (config.gamepad_to_vk_map[i].vk_code != 0) {
                if (i < 16) state->Gamepad.wButtons &= ~(1 << i);
                else if (i == 16) state->Gamepad.bLeftTrigger = 0;
                else if (i == 17) state->Gamepad.bRightTrigger = 0;
            }
        }
        return;
    }

    DWORD pressed = changed & current_buttons;
    DWORD released = changed & ~current_buttons;

    INPUT inputs[128] = {};
    int input_count = 0;

    for (int i = 0; i < 18; ++i) {
        const auto& map = config.gamepad_to_vk_map[i];
        uint8_t vk_code = map.vk_code;
        if (vk_code == 0) continue;

        DWORD mask = (1 << i);

        if (pressed & mask) {
            if (map.modifier_ctrl) { inputs[input_count].type = INPUT_KEYBOARD; inputs[input_count].ki.wVk = VK_CONTROL; inputs[input_count].ki.dwFlags = 0; input_count++; }
            if (map.modifier_shift) { inputs[input_count].type = INPUT_KEYBOARD; inputs[input_count].ki.wVk = VK_SHIFT; inputs[input_count].ki.dwFlags = 0; input_count++; }
            if (map.modifier_alt) { inputs[input_count].type = INPUT_KEYBOARD; inputs[input_count].ki.wVk = VK_MENU; inputs[input_count].ki.dwFlags = 0; input_count++; }

            inputs[input_count].type = INPUT_KEYBOARD;
            inputs[input_count].ki.wVk = vk_code;
            inputs[input_count].ki.dwFlags = 0; // Key down
            input_count++;
        } else if (released & mask) {
            inputs[input_count].type = INPUT_KEYBOARD;
            inputs[input_count].ki.wVk = vk_code;
            inputs[input_count].ki.dwFlags = KEYEVENTF_KEYUP;
            input_count++;

            if (map.modifier_alt) { inputs[input_count].type = INPUT_KEYBOARD; inputs[input_count].ki.wVk = VK_MENU; inputs[input_count].ki.dwFlags = KEYEVENTF_KEYUP; input_count++; }
            if (map.modifier_shift) { inputs[input_count].type = INPUT_KEYBOARD; inputs[input_count].ki.wVk = VK_SHIFT; inputs[input_count].ki.dwFlags = KEYEVENTF_KEYUP; input_count++; }
            if (map.modifier_ctrl) { inputs[input_count].type = INPUT_KEYBOARD; inputs[input_count].ki.wVk = VK_CONTROL; inputs[input_count].ki.dwFlags = KEYEVENTF_KEYUP; input_count++; }
        }

        // Zero out the mapped button so the game doesn't see the original input
        if (i < 16) {
            state->Gamepad.wButtons &= ~(1 << i);
        } else if (i == 16) {
            state->Gamepad.bLeftTrigger = 0;
        } else if (i == 17) {
            state->Gamepad.bRightTrigger = 0;
        }
    }

    if (input_count > 0) {
        SendInput(input_count, inputs, sizeof(INPUT));
    }

    g_prev_virtual_buttons[user_index] = current_buttons;
}

} // namespace dover::overlay::input_mapper
