# ImGui InputText External Mutation & State Overwrite Bug

## Problem Overview
When attempting to build formatting toolbars or custom Copy/Cut/Paste context menus for an `ImGui::InputTextMultiline` widget, developers often try to mutate the underlying `char*` text buffer directly from the UI logic (e.g., inside the button click handler). 

The symptom of this approach is highly erratic behavior:
- Formatting buttons (like Bold or Italic) cause the text to briefly flicker or fail to apply entirely.
- The typing cursor "jumps" to random or unexpected positions.
- "Cut" operations copy the text to the clipboard but leave the original text behind.
- "Paste" operations do absolutely nothing.

## Root Cause Analysis
This erratic behavior is caused by a fundamental conflict between external state manipulation and ImGui's internal `stb_textedit` state.

1. **The Internal Buffer Priority:** When an `InputText` widget is active (focused), ImGui uses its own internal text buffer (`state->TextA`) as the absolute source of truth. It tracks the cursor, selection bounds, and undo/redo history natively.
2. **The Overwrite Cycle:** If you mutate the user-provided `char*` buffer externally (e.g., `strcat`, `memmove`) while the widget is active, ImGui will completely ignore your changes. At the end of the frame, ImGui writes its internal `state->TextA` *back* into your user buffer, effectively erasing everything you just did.
3. **Cursor Desynchronization:** If your external mutation logic also manually calculates and updates global cursor/selection variables, these new coordinates will be applied to the *old* text (because ImGui reverted the string), resulting in the cursor "jumping" out of bounds or to incorrect characters.

## The Solution: The Callback Pipeline
To safely mutate text inside an active `InputText` widget, you **must not** modify the buffer externally. Instead, all text mutations must be executed from *inside* the `ImGuiInputTextCallback`.

### Implementation Strategy

1. **Create a Formatting Enum/State:**
Instead of modifying the string when a button is clicked, set an intention (a pending format action) inside a global or shared state struct.

```cpp
enum PendingFormat {
    FORMAT_NONE, FORMAT_BOLD, FORMAT_CUT, FORMAT_PASTE
};

struct FormatterState {
    PendingFormat pending_format = FORMAT_NONE;
    int focus_editor_restore_frames = 0;
};
```

2. **Trigger Intentions via UI:**
```cpp
if (ImGui::Button("Bold")) {
    GetFormatterState().pending_format = FORMAT_BOLD;
    // (Ensure you also forcefully restore focus as described in the Focus Loss bug documentation)
    GetFormatterState().focus_editor_restore_frames = 1; 
}
```

3. **Execute Mutations Natively Inside the Callback:**
Pass a callback to your `InputTextMultiline`. Inside the callback, use ImGui's native `data->DeleteChars()` and `data->InsertChars()` methods. 
These methods safely update the internal `stb_textedit` buffer, update cursor positions, and automatically flag `data->BufDirty = true` to inform ImGui that the text was structurally changed.

```cpp
int FormatCallback(ImGuiInputTextCallbackData* data) {
    if (GetFormatterState().pending_format != FORMAT_NONE) {
        switch (GetFormatterState().pending_format) {
            case FORMAT_PASTE: {
                const char* clip = ImGui::GetClipboardText();
                if (clip && clip[0] != '\0') {
                    // Delete selected text natively
                    if (data->HasSelection()) {
                        data->DeleteChars(data->SelectionStart, data->SelectionEnd - data->SelectionStart);
                    }
                    // Insert new text natively
                    data->InsertChars(data->CursorPos, clip);
                }
                break;
            }
            // ... Handle BOLD, CUT, etc.
        }
        GetFormatterState().pending_format = FORMAT_NONE; // Clear intention
    }
    return 0;
}
```

## Key Takeaways
- **Never mutate the string buffer externally** if the `InputText` widget is (or will immediately become) active.
- Treat `InputText` as a black box: only interact with its data through the `ImGuiInputTextCallbackData` struct passed to its callback.
- Always use `data->DeleteChars()` and `data->InsertChars()` for safe string mutations; this ensures seamless integration with ImGui's undo/redo system and cursor management.
