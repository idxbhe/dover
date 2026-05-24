#include "overlay/notes/formatter.h"
#include <windows.h>
#include <vector>

namespace dover::overlay::notes {

namespace {
  FormatterState g_formatter_state;
  float g_wrap_width = 400.0f;
  ImFont* g_editor_font = nullptr;
}

FormatterState& GetFormatterState() {
  return g_formatter_state;
}

void SetFormatterContext(float wrap_width, ImFont* editor_font) {
  g_wrap_width = wrap_width;
  g_editor_font = editor_font;
}

static void WrapSelection(ImGuiInputTextCallbackData* data,
                          const char* prefix, const char* suffix) {
  int plen = static_cast<int>(strlen(prefix));
  int slen = static_cast<int>(strlen(suffix));
  int sel_s = data->SelectionStart;
  int sel_e = data->SelectionEnd;
  if (sel_s > sel_e) { int t = sel_s; sel_s = sel_e; sel_e = t; }

  if (sel_s == sel_e) {
    data->InsertChars(sel_s, suffix, suffix + slen);
    data->InsertChars(sel_s, prefix, prefix + plen);
    data->CursorPos     = sel_s + plen;
    data->SelectionStart = data->SelectionEnd = data->CursorPos;
  } else {
    data->InsertChars(sel_e, suffix, suffix + slen);
    data->InsertChars(sel_s, prefix, prefix + plen);
    data->CursorPos      = sel_e + plen + slen;
    data->SelectionStart = sel_s + plen;
    data->SelectionEnd   = sel_e + plen;
  }
}

static void ProcessWordWrap(ImGuiInputTextCallbackData* data) {
  if (data->BufTextLen == 0 || !g_editor_font) return;

  // Build clean string and mapping from wrapped index to clean index
  std::string clean_str;
  std::vector<int> map_W_to_C(data->BufTextLen + 1, 0);
  int c_idx = 0;
  for (int i = 0; i < data->BufTextLen; ) {
    if (i + 1 < data->BufTextLen && data->Buf[i] == ' ' && data->Buf[i+1] == '\n') {
      map_W_to_C[i] = c_idx;
      map_W_to_C[i+1] = c_idx;
      clean_str += ' ';
      c_idx++;
      i += 2;
    } else if (i + 1 < data->BufTextLen && data->Buf[i] == '-' && data->Buf[i+1] == '\n') {
      map_W_to_C[i] = c_idx;
      map_W_to_C[i+1] = c_idx;
      i += 2;
    } else {
      map_W_to_C[i] = c_idx;
      clean_str += data->Buf[i];
      c_idx++;
      i++;
    }
  }
  map_W_to_C[data->BufTextLen] = c_idx;

  // Performance optimization check:
  static std::string s_last_clean_str;
  static float s_last_wrap_width = 0.0f;
  static ImFont* s_last_font = nullptr;

  if (clean_str == s_last_clean_str &&
      g_wrap_width == s_last_wrap_width &&
      g_editor_font == s_last_font) {
    return;
  }

  s_last_clean_str = clean_str;
  s_last_wrap_width = g_wrap_width;
  s_last_font = g_editor_font;

  // Wrap clean string to build wrapped string and mapping from clean index to new wrapped index
  std::string wrapped_str;
  std::vector<int> map_C_to_Wnew(clean_str.length() + 1, 0);

  int clean_len = static_cast<int>(clean_str.length());
  int last_wrap_c_idx = 0;
  int last_space_c_idx = -1;

  for (int i = 0; i <= clean_len; i++) {
    if (i == clean_len || clean_str[i] == '\n') {
      for (int j = last_wrap_c_idx; j < i; j++) {
        map_C_to_Wnew[j] = static_cast<int>(wrapped_str.length());
        wrapped_str += clean_str[j];
      }
      if (i < clean_len) {
        map_C_to_Wnew[i] = static_cast<int>(wrapped_str.length());
        wrapped_str += '\n';
      }
      last_wrap_c_idx = i + 1;
      last_space_c_idx = -1;
      continue;
    }

    if (clean_str[i] == ' ') {
      last_space_c_idx = i;
    }

    float width = g_editor_font->CalcTextSizeA(g_editor_font->FontSize, FLT_MAX, 0.0f,
                                               &clean_str[last_wrap_c_idx],
                                               &clean_str[i] + 1).x;

    if (width > g_wrap_width) {
      if (last_space_c_idx != -1 && last_space_c_idx > last_wrap_c_idx) {
        for (int j = last_wrap_c_idx; j <= last_space_c_idx; j++) {
          map_C_to_Wnew[j] = static_cast<int>(wrapped_str.length());
          wrapped_str += clean_str[j];
        }
        wrapped_str += "\n";

        last_wrap_c_idx = last_space_c_idx + 1;
        i = last_wrap_c_idx - 1;
        last_space_c_idx = -1;
      } else if (i > last_wrap_c_idx) {
        for (int j = last_wrap_c_idx; j < i; j++) {
          map_C_to_Wnew[j] = static_cast<int>(wrapped_str.length());
          wrapped_str += clean_str[j];
        }
        wrapped_str += "-\n";

        last_wrap_c_idx = i;
        i = last_wrap_c_idx - 1;
        last_space_c_idx = -1;
      }
    }
  }
  map_C_to_Wnew[clean_len] = static_cast<int>(wrapped_str.length());

  auto MapOldToNew = [&](int old_pos) -> int {
    if (old_pos < 0) return 0;
    if (old_pos > data->BufTextLen) old_pos = data->BufTextLen;
    int clean_pos = map_W_to_C[old_pos];
    return map_C_to_Wnew[clean_pos];
  };

  int new_cursor = MapOldToNew(data->CursorPos);
  int new_sel_start = MapOldToNew(data->SelectionStart);
  int new_sel_end = MapOldToNew(data->SelectionEnd);

  if (wrapped_str != data->Buf) {
    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, wrapped_str.c_str(), wrapped_str.c_str() + wrapped_str.length());
    data->CursorPos = new_cursor;
    data->SelectionStart = new_sel_start;
    data->SelectionEnd = new_sel_end;
  }
}

int FormatCallback(ImGuiInputTextCallbackData* data) {
  if (g_formatter_state.focus_editor_restore_frames > 0 && g_formatter_state.has_saved_state) {
    data->SelectionStart          = g_formatter_state.saved_selection_start;
    data->SelectionEnd            = g_formatter_state.saved_selection_end;
    data->CursorPos               = g_formatter_state.saved_cursor_pos;
    g_formatter_state.focus_editor_restore_frames--; 
  }

  if (g_formatter_state.pending_format != FORMAT_NONE) {
    switch (g_formatter_state.pending_format) {
      case FORMAT_BOLD: WrapSelection(data, "**", "**"); break;
      case FORMAT_ITALIC: WrapSelection(data, "*", "*"); break;
      case FORMAT_STRIKETHROUGH: WrapSelection(data, "~~", "~~"); break;
      case FORMAT_UNDERLINE: WrapSelection(data, "<u>", "</u>"); break;
      case FORMAT_CODE: WrapSelection(data, "`", "`"); break;
      case FORMAT_CODE_BLOCK: WrapSelection(data, "\n```\n", "\n```\n"); break;
      case FORMAT_H1: WrapSelection(data, "# ", ""); break;
      case FORMAT_H2: WrapSelection(data, "## ", ""); break;
      case FORMAT_H3: WrapSelection(data, "### ", ""); break;
      case FORMAT_H4: WrapSelection(data, "#### ", ""); break;
      case FORMAT_H5: WrapSelection(data, "##### ", ""); break;
      case FORMAT_LIST_BULLET: WrapSelection(data, "- ", ""); break;
      case FORMAT_LIST_NUMBER: WrapSelection(data, "1. ", ""); break;
      case FORMAT_INDENT: WrapSelection(data, "\t", ""); break;
      default: break;
    }
    g_formatter_state.pending_format = FORMAT_NONE;
  } else {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (ctrl) {
      if (ImGui::IsKeyPressed(ImGuiKey_B, false) || ImGui::IsKeyPressed((ImGuiKey)'B', false)) {
        WrapSelection(data, "**", "**");
      } else if (ImGui::IsKeyPressed(ImGuiKey_I, false) || ImGui::IsKeyPressed((ImGuiKey)'I', false)) {
        WrapSelection(data, "*", "*");
      } else if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false) || ImGui::IsKeyPressed((ImGuiKey)192, false)) {
        WrapSelection(data, "`", "`");
      } else if (shift && (ImGui::IsKeyPressed(ImGuiKey_X, false) || ImGui::IsKeyPressed((ImGuiKey)'X', false))) {
        WrapSelection(data, "~~", "~~");
      }
    }
  }

  // ProcessWordWrap(data) was removed because ImGui handles visual word wrapping natively

  g_formatter_state.saved_selection_start = data->SelectionStart;
  g_formatter_state.saved_selection_end   = data->SelectionEnd;
  g_formatter_state.saved_cursor_pos      = data->CursorPos;
  g_formatter_state.has_saved_state       = true;

  return 0;
}

void ApplyToolbarFormat(const char* prefix, const char* suffix, char* edit_buffer, size_t buffer_size) {
  int s_start = g_formatter_state.saved_selection_start;
  int s_end   = g_formatter_state.saved_selection_end;
  if (s_start > s_end) std::swap(s_start, s_end);
  
  std::string text = edit_buffer;
  if (s_start >= 0 && s_start <= static_cast<int>(text.length()) && s_end >= 0 && s_end <= static_cast<int>(text.length())) {
      text.insert(s_end, suffix);
      text.insert(s_start, prefix);
      strncpy_s(edit_buffer, buffer_size, text.c_str(), _TRUNCATE);
      
      int plen = static_cast<int>(strlen(prefix));
      int slen = static_cast<int>(strlen(suffix));
      if (s_start == s_end) {
          g_formatter_state.saved_cursor_pos = s_start + plen;
          g_formatter_state.saved_selection_start = g_formatter_state.saved_cursor_pos;
          g_formatter_state.saved_selection_end   = g_formatter_state.saved_cursor_pos;
      } else {
          g_formatter_state.saved_cursor_pos      = s_end + plen + slen;
          g_formatter_state.saved_selection_start = s_start + plen;
          g_formatter_state.saved_selection_end   = s_end + plen;
      }
  }
  g_formatter_state.focus_editor_restore_frames = 3;
  g_formatter_state.has_saved_state = true;
}

void WrapGlobalBuffer(char* edit_buffer, size_t buffer_size, float wrap_width, ImFont* font) {
  if (strlen(edit_buffer) == 0 || !font) return;

  std::string clean_str;
  int len = static_cast<int>(strlen(edit_buffer));
  for (int i = 0; i < len; ) {
    if (i + 1 < len && edit_buffer[i] == ' ' && edit_buffer[i+1] == '\n') {
      clean_str += ' ';
      i += 2;
    } else if (i + 1 < len && edit_buffer[i] == '-' && edit_buffer[i+1] == '\n') {
      i += 2;
    } else {
      clean_str += edit_buffer[i];
      i++;
    }
  }

  std::string wrapped_str;
  int clean_len = static_cast<int>(clean_str.length());
  int last_wrap_c_idx = 0;
  int last_space_c_idx = -1;

  for (int i = 0; i <= clean_len; i++) {
    if (i == clean_len || clean_str[i] == '\n') {
      for (int j = last_wrap_c_idx; j < i; j++) {
        wrapped_str += clean_str[j];
      }
      if (i < clean_len) {
        wrapped_str += '\n';
      }
      last_wrap_c_idx = i + 1;
      last_space_c_idx = -1;
      continue;
    }

    if (clean_str[i] == ' ') {
      last_space_c_idx = i;
    }

    float width = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f,
                                      &clean_str[last_wrap_c_idx],
                                      &clean_str[i] + 1).x;

    if (width > wrap_width) {
      if (last_space_c_idx != -1 && last_space_c_idx > last_wrap_c_idx) {
        for (int j = last_wrap_c_idx; j <= last_space_c_idx; j++) {
          wrapped_str += clean_str[j];
        }
        wrapped_str += "\n";
        last_wrap_c_idx = last_space_c_idx + 1;
        i = last_wrap_c_idx - 1;
        last_space_c_idx = -1;
      } else if (i > last_wrap_c_idx) {
        for (int j = last_wrap_c_idx; j < i; j++) {
          wrapped_str += clean_str[j];
        }
        wrapped_str += "-\n";
        last_wrap_c_idx = i;
        i = last_wrap_c_idx - 1;
        last_space_c_idx = -1;
      }
    }
  }

  strncpy_s(edit_buffer, buffer_size, wrapped_str.c_str(), _TRUNCATE);
}

} // namespace dover::overlay::notes
