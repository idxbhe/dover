#pragma once
#include <atomic>

namespace dover::shared::ui {

/**
 * @brief A premium toggle checkbox widget with consistent styling.
 * @param label The label for the checkbox.
 * @param value_ptr Pointer to the boolean value to toggle.
 * @return True if the value was changed this frame.
 */
bool ToggleCheckbox(const char* label, bool* value_ptr);

/**
 * @brief Overload for std::atomic<bool> to support thread-safe settings.
 */
bool ToggleCheckbox(const char* label, std::atomic<bool>* value_ptr);

} // namespace dover::shared::ui
