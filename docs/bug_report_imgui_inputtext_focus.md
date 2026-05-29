# ImGui InputText Focus Loss & ActiveId Stealing

## Problem Overview
When developing complex UIs using Dear ImGui, placing `ImGui::Button` or `ImGui::MenuItem` elements alongside an `ImGui::InputText` or `ImGui::InputTextMultiline` widget can lead to severe UX issues. 

Specifically, clicking a toolbar button or context menu item causes the editor to instantly lose its focus. The blinking cursor disappears, and if the user clicks back into the text area to regain focus, their previous text selection is completely forgotten.

## Root Cause Analysis
ImGui operates using an Immediate Mode GUI paradigm, heavily relying on the concept of an `ActiveId` to track which widget the user is currently interacting with.
1. **ActiveId Stealing:** When you click an `ImGui::Button` or `ImGui::MenuItem`, ImGui instantly assigns the global `g.ActiveId` to that clicked element.
2. **State Destruction:** As soon as the `ActiveId` changes away from the `InputTextMultiline` widget, ImGui considers the text input "deactivated." Consequently, it destroys the internal `stb_textedit` state (which holds the exact cursor position, scroll state, and selection bounds).
3. **SetKeyboardFocusHere Limitations:** Using `ImGui::SetKeyboardFocusHere(0)` queues a focus request for the *next* frame. While this gives focus back, it does not prevent the 1-frame deactivation gap. When the `InputText` is reactivated in the next frame, it initializes as a "new" interaction, resetting the cursor to the end of the text and wiping out the previous selection.

## The Solution
To fix this, we must unconditionally force ImGui to return the `ActiveId` to the `InputTextMultiline` widget *in the exact same frame* after the button click, preventing the internal state destruction.

Since the public API (`SetKeyboardFocusHere`) is insufficient for this aggressive state retention, we utilize ImGui's internal API (`imgui_internal.h`).

### Implementation
```cpp
#include <imgui.h>
#include <imgui_internal.h> // Required for advanced focus manipulation

// Inside the render loop, before calling ImGui::InputTextMultiline:
if (window->m_force_focus_frames > 0) {
    ImGuiID editor_id = ImGui::GetID("##ed"); // The ID of the InputText widget
    
    // Forcefully bring the parent window to the front
    ImGui::FocusWindow(ImGui::GetCurrentWindow());
    
    // Forcefully reclaim the ActiveId for the InputText widget immediately
    ImGui::SetActiveID(editor_id, ImGui::GetCurrentWindow());
    
    // Also queue standard keyboard focus as a fallback
    ImGui::SetKeyboardFocusHere(0);
    
    window->m_force_focus_frames--;
}

// ... Call ImGui::InputTextMultiline("##ed", ...)
```

## Key Takeaways
- Never rely solely on `SetKeyboardFocusHere(0)` if you are trying to maintain uninterrupted typing state while interacting with adjacent UI elements.
- `ImGui::SetActiveID` is a powerful internal tool to completely override ImGui's default focus stealing behavior, guaranteeing that `stb_textedit` remains intact and the cursor never stops blinking.
- Always remember to decrement your focus-forcing flags or variables to avoid permanently locking the user into the text editor.
