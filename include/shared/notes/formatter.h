#pragma once
#include "imgui.h"
#include <string>

namespace dover::shared::notes {

enum PendingFormat {
  FORMAT_NONE, FORMAT_BOLD, FORMAT_ITALIC, FORMAT_STRIKETHROUGH, FORMAT_UNDERLINE,
  FORMAT_CODE, FORMAT_CODE_BLOCK, FORMAT_H1, FORMAT_H2, FORMAT_H3, FORMAT_H4, FORMAT_H5,
  FORMAT_LIST_BULLET, FORMAT_LIST_NUMBER, FORMAT_INDENT, FORMAT_OUTDENT,
  FORMAT_CUT, FORMAT_COPY, FORMAT_PASTE
};

struct FormatterState {
  PendingFormat pending_format = FORMAT_NONE;
  int saved_selection_start = 0;
  int saved_selection_end = 0;
  int saved_cursor_pos = 0;
  bool has_saved_state = false;
  int focus_editor_restore_frames = 0;
  bool text_was_formatted_this_frame = false;
};

// Access to the global formatter state
FormatterState& GetFormatterState();

// Helper to set the global editor state for wrapping inside the callback
void SetFormatterContext(float wrap_width, ImFont* editor_font, float font_size);

// Removed ApplyToolbarFormat

// The callback given to ImGui::InputTextMultiline
int FormatCallback(ImGuiInputTextCallbackData* data);

// Directly wraps the global buffer when the editor is NOT focused
void WrapGlobalBuffer(char* edit_buffer, size_t buffer_size, float wrap_width, ImFont* font, float font_size);

} // namespace dover::shared::notes
