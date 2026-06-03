#include "overlay/notes/formatter.h"
#include <windows.h>
#include <algorithm> // For std::swap
#include <cstring>   // For memcpy, memcmp, memmove, strlen

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

  // Automatic Line Detection & Toggle for Headings (e.g. prefix starts with '#')
  bool is_heading = (plen > 0 && prefix[0] == '#');
  if (is_heading) {
    int len = data->BufTextLen;
    int start = sel_s;
    while (start > 0 && data->Buf[start - 1] != '\n') {
      start--;
    }
    int end = sel_e;
    while (end < len && data->Buf[end] != '\r' && data->Buf[end] != '\n') {
      end++;
    }

    int old_h_count = 0;
    while (start + old_h_count < len && data->Buf[start + old_h_count] == '#') {
      old_h_count++;
    }
    
    bool has_space = (start + old_h_count < len && data->Buf[start + old_h_count] == ' ');
    int strip_len = old_h_count + (has_space ? 1 : 0);

    int new_h_count = 0;
    for (int i = 0; i < plen; i++) {
      if (prefix[i] == '#') new_h_count++;
    }

    if (old_h_count > 0) {
      data->DeleteChars(start, strip_len);
      len = data->BufTextLen;
      end = (end >= start + strip_len) ? (end - strip_len) : start;

      if (old_h_count == new_h_count) {
        data->SelectionStart = start;
        data->SelectionEnd = end;
        data->CursorPos = end;
        g_formatter_state.text_was_formatted_this_frame = true;
        return;
      }
    }

    sel_s = start;
    sel_e = end;
  }

  // Automatic Word Detection when no selection exists (only for enclosing styles)
  if (sel_s == sel_e && slen > 0) {
    int cur = data->CursorPos;
    int len = data->BufTextLen;

    auto IsWordChar = [](char c) -> bool {
      return c != ' ' && c != '\t' && c != '\r' && c != '\n' &&
             c != ',' && c != ';' && c != '(' && c != ')' &&
             c != '{' && c != '}' && c != '[' && c != ']' &&
             c != '|' && c != '.' && c != '!' && c != '`' &&
             c != '*' && c != '~' && c != '<' && c != '>';
    };

    bool on_word = false;
    if (cur > 0 && IsWordChar(data->Buf[cur - 1])) {
      on_word = true;
    } else if (cur < len && IsWordChar(data->Buf[cur])) {
      on_word = true;
    }

    if (on_word) {
      int start = cur;
      while (start > 0 && IsWordChar(data->Buf[start - 1])) {
        start--;
      }
      int end = cur;
      while (end < len && IsWordChar(data->Buf[end])) {
        end++;
      }
      if (start < end) {
        sel_s = start;
        sel_e = end;
      }
    }
  }

  if (sel_s == sel_e) {
    data->InsertChars(sel_s, suffix, suffix + slen);
    data->InsertChars(sel_s, prefix, prefix + plen);
    data->CursorPos     = sel_s + plen;
    data->SelectionStart = data->SelectionEnd = data->CursorPos;
  } else {
    data->InsertChars(sel_e, suffix, suffix + slen);
    data->InsertChars(sel_s, prefix, prefix + plen);
    data->SelectionStart = sel_s + plen;
    data->SelectionEnd   = sel_e + plen;
    data->CursorPos      = sel_e + plen;
  }
  g_formatter_state.text_was_formatted_this_frame = true;
}

static void ProcessWordWrap(ImGuiInputTextCallbackData* data) {
  if (data->BufTextLen == 0 || !g_editor_font) return;

  // Zero-Overhead Early Exit Buffers
  constexpr int MAX_LEN = 131072;
  static char s_last_wrapped_buffer[MAX_LEN] = {0};
  static int s_last_wrapped_len = -1;
  static float s_last_wrap_width = 0.0f;
  static ImFont* s_last_font = nullptr;

  if (data->BufTextLen == s_last_wrapped_len &&
      g_wrap_width == s_last_wrap_width &&
      g_editor_font == s_last_font &&
      memcmp(data->Buf, s_last_wrapped_buffer, data->BufTextLen) == 0) {
    return; // Instant early exit!
  }

  // Static persistent buffers (No Heap Allocations)
  static char s_clean_str[MAX_LEN];
  static int s_map_W_to_C[MAX_LEN];
  static char s_wrapped_str[MAX_LEN];
  static int s_map_C_to_Wnew[MAX_LEN];

  if (data->BufTextLen >= MAX_LEN - 1) return;

  int clean_len = 0;
  int c_idx = 0;
  for (int i = 0; i < data->BufTextLen; ) {
    if (i + 1 < data->BufTextLen && data->Buf[i] == '\r' && data->Buf[i+1] == '\n') {
      s_map_W_to_C[i] = c_idx;
      s_map_W_to_C[i+1] = c_idx;
      s_clean_str[clean_len++] = ' ';
      c_idx++;
      i += 2;
    } else if (i + 2 < data->BufTextLen && data->Buf[i] == '\r' && data->Buf[i+1] == '-' && data->Buf[i+2] == '\n') {
      s_map_W_to_C[i] = c_idx;
      s_map_W_to_C[i+1] = c_idx;
      s_map_W_to_C[i+2] = c_idx;
      i += 3;
    } else {
      s_map_W_to_C[i] = c_idx;
      s_clean_str[clean_len++] = data->Buf[i];
      c_idx++;
      i++;
    }
  }
  s_map_W_to_C[data->BufTextLen] = c_idx;
  s_clean_str[clean_len] = '\0';

  int wrapped_len = 0;
  int last_wrap_c_idx = 0;
  int last_space_c_idx = -1;

  float current_line_width = 0.0f;

  for (int i = 0; i <= clean_len; i++) {
    if (i == clean_len || s_clean_str[i] == '\n') {
      for (int j = last_wrap_c_idx; j < i; j++) {
        if (wrapped_len < MAX_LEN - 1) {
          s_map_C_to_Wnew[j] = wrapped_len;
          s_wrapped_str[wrapped_len++] = s_clean_str[j];
        }
      }
      if (i < clean_len) {
        if (wrapped_len < MAX_LEN - 1) {
          s_map_C_to_Wnew[i] = wrapped_len;
          s_wrapped_str[wrapped_len++] = '\n';
        }
      }
      last_wrap_c_idx = i + 1;
      last_space_c_idx = -1;
      current_line_width = 0.0f;
      continue;
    }

    if (s_clean_str[i] == ' ') {
      last_space_c_idx = i;
    }

    float char_width = g_editor_font->CalcTextSizeA(g_editor_font->FontSize, FLT_MAX, 0.0f,
                                               &s_clean_str[i],
                                               &s_clean_str[i] + 1).x;
    current_line_width += char_width;

    if (current_line_width > g_wrap_width) {
      if (last_space_c_idx != -1 && last_space_c_idx >= last_wrap_c_idx) {
        for (int j = last_wrap_c_idx; j < last_space_c_idx; j++) {
          if (wrapped_len < MAX_LEN - 1) {
            s_map_C_to_Wnew[j] = wrapped_len;
            s_wrapped_str[wrapped_len++] = s_clean_str[j];
          }
        }
        if (wrapped_len < MAX_LEN - 2) {
          s_map_C_to_Wnew[last_space_c_idx] = wrapped_len;
          s_wrapped_str[wrapped_len++] = '\r';
          s_wrapped_str[wrapped_len++] = '\n';
        }
        last_wrap_c_idx = last_space_c_idx + 1;
        i = last_wrap_c_idx - 1;
        last_space_c_idx = -1;
        current_line_width = 0.0f;
      } else if (i > last_wrap_c_idx) {
        for (int j = last_wrap_c_idx; j < i; j++) {
          if (wrapped_len < MAX_LEN - 1) {
            s_map_C_to_Wnew[j] = wrapped_len;
            s_wrapped_str[wrapped_len++] = s_clean_str[j];
          }
        }
        if (wrapped_len < MAX_LEN - 3) {
          s_wrapped_str[wrapped_len++] = '\r';
          s_wrapped_str[wrapped_len++] = '-';
          s_wrapped_str[wrapped_len++] = '\n';
        }
        last_wrap_c_idx = i;
        i = last_wrap_c_idx - 1;
        last_space_c_idx = -1;
        current_line_width = 0.0f;
      }
    }
  }
  s_map_C_to_Wnew[clean_len] = wrapped_len;
  s_wrapped_str[wrapped_len] = '\0';

  auto MapOldToNew = [&](int old_pos) -> int {
    if (old_pos < 0) return 0;
    if (old_pos > data->BufTextLen) old_pos = data->BufTextLen;
    int clean_pos = s_map_W_to_C[old_pos];
    if (clean_pos > clean_len) clean_pos = clean_len;
    return s_map_C_to_Wnew[clean_pos];
  };

  int new_cursor = MapOldToNew(data->CursorPos);
  int new_sel_start = MapOldToNew(data->SelectionStart);
  int new_sel_end = MapOldToNew(data->SelectionEnd);

  if (data->BufTextLen != wrapped_len || memcmp(data->Buf, s_wrapped_str, wrapped_len) != 0) {
    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, s_wrapped_str, s_wrapped_str + wrapped_len);
    data->CursorPos = new_cursor;
    data->SelectionStart = new_sel_start;
    data->SelectionEnd = new_sel_end;
    g_formatter_state.text_was_formatted_this_frame = true;
  }

  s_last_wrapped_len = wrapped_len;
  s_last_wrap_width = g_wrap_width;
  s_last_font = g_editor_font;
  memcpy(s_last_wrapped_buffer, s_wrapped_str, wrapped_len);
}

int FormatCallback(ImGuiInputTextCallbackData* data) {
  g_formatter_state.text_was_formatted_this_frame = false;

  if (ImGui::IsMouseDoubleClicked(0) && data->HasSelection()) {
    int s_start = data->SelectionStart;
    int s_end = data->SelectionEnd;
    bool reversed = false;
    if (s_start > s_end) {
      std::swap(s_start, s_end);
      reversed = true;
    }
    while (s_end > s_start) {
      char c = data->Buf[s_end - 1];
      if (c == '\n' || c == '\r') {
        s_end--;
      } else if (c == '-' && s_end >= 2 && data->Buf[s_end - 2] == '\r') {
        s_end--;
      } else {
        break;
      }
    }
    while (s_start < s_end) {
      char c = data->Buf[s_start];
      if (c == '\n' || c == '\r') {
        s_start++;
      } else {
        break;
      }
    }
    if (reversed) {
      data->SelectionStart = s_end;
      data->SelectionEnd = s_start;
      data->CursorPos = s_start;
    } else {
      data->SelectionStart = s_start;
      data->SelectionEnd = s_end;
      data->CursorPos = s_end;
    }
  }

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
      case FORMAT_COPY: {
        if (data->HasSelection()) {
          int s_start = data->SelectionStart;
          int s_end = data->SelectionEnd;
          if (s_start > s_end) std::swap(s_start, s_end);
          constexpr int MAX_COPY = 131072;
          static char clean_copy[MAX_COPY];
          int c_idx = 0;
          for (int i = s_start; i < s_end; ) {
             if (c_idx >= MAX_COPY - 1) break;
             if (i + 1 < s_end && data->Buf[i] == '\r' && data->Buf[i+1] == '\n') {
                 clean_copy[c_idx++] = ' ';
                 i += 2;
             } else if (i + 2 < s_end && data->Buf[i] == '\r' && data->Buf[i+1] == '-' && data->Buf[i+2] == '\n') {
                 i += 3;
             } else {
                 clean_copy[c_idx++] = data->Buf[i];
                 i++;
             }
          }
          clean_copy[c_idx] = '\0';
          ImGui::SetClipboardText(clean_copy);
        } else {
          ImGui::SetClipboardText("");
        }
        break;
      }
      case FORMAT_CUT: {
        if (data->HasSelection()) {
          int s_start = data->SelectionStart;
          int s_end = data->SelectionEnd;
          if (s_start > s_end) std::swap(s_start, s_end);
          constexpr int MAX_COPY = 131072;
          static char clean_copy[MAX_COPY];
          int c_idx = 0;
          for (int i = s_start; i < s_end; ) {
             if (c_idx >= MAX_COPY - 1) break;
             if (i + 1 < s_end && data->Buf[i] == '\r' && data->Buf[i+1] == '\n') {
                 clean_copy[c_idx++] = ' ';
                 i += 2;
             } else if (i + 2 < s_end && data->Buf[i] == '\r' && data->Buf[i+1] == '-' && data->Buf[i+2] == '\n') {
                 i += 3;
             } else {
                 clean_copy[c_idx++] = data->Buf[i];
                 i++;
             }
          }
          clean_copy[c_idx] = '\0';
          ImGui::SetClipboardText(clean_copy);
          data->DeleteChars(s_start, s_end - s_start);
          data->SelectionEnd = s_start;
          data->CursorPos = s_start;
        } else {
          ImGui::SetClipboardText("");
        }
        break;
      }
      case FORMAT_PASTE: {
        const char* clip = ImGui::GetClipboardText();
        if (clip && clip[0] != '\0') {
          int s_start = data->SelectionStart;
          int s_end = data->SelectionEnd;
          if (s_start > s_end) std::swap(s_start, s_end);
          if (s_start != s_end) {
            data->DeleteChars(s_start, s_end - s_start);
          }
          data->InsertChars(s_start, clip);
          int paste_len = static_cast<int>(strlen(clip));
          data->CursorPos = s_start + paste_len;
          data->SelectionStart = data->CursorPos;
          data->SelectionEnd = data->CursorPos;
        }
        break;
      }
      default: break;
    }
    g_formatter_state.pending_format = FORMAT_NONE;
  } else {
    bool ctrl = ImGui::GetIO().KeyCtrl;
    bool shift = ImGui::GetIO().KeyShift;
    
    static uint32_t s_prev_keys = 0;
    uint32_t curr_keys = 0;
    if (ImGui::IsKeyDown(ImGuiKey_B)) curr_keys |= (1 << 0);
    if (ImGui::IsKeyDown(ImGuiKey_I)) curr_keys |= (1 << 1);
    if (ImGui::IsKeyDown(ImGuiKey_K)) curr_keys |= (1 << 2);
    if (ImGui::IsKeyDown(ImGuiKey_1)) curr_keys |= (1 << 3);
    if (ImGui::IsKeyDown(ImGuiKey_2)) curr_keys |= (1 << 4);
    if (ImGui::IsKeyDown(ImGuiKey_3)) curr_keys |= (1 << 5);
    if (ImGui::IsKeyDown(ImGuiKey_4)) curr_keys |= (1 << 6);
    if (ImGui::IsKeyDown(ImGuiKey_5)) curr_keys |= (1 << 7);
    if (ImGui::IsKeyDown(ImGuiKey_X)) curr_keys |= (1 << 8);
    if (ImGui::IsKeyDown(ImGuiKey_8)) curr_keys |= (1 << 9);
    if (ImGui::IsKeyDown(ImGuiKey_GraveAccent)) curr_keys |= (1 << 10);
    
    uint32_t pressed = curr_keys & ~s_prev_keys;
    s_prev_keys = curr_keys;

    if (ctrl) {
      if (shift) {
        if (pressed & (1 << 8)) WrapSelection(data, "~~", "~~"); // X
        else if (pressed & (1 << 9)) WrapSelection(data, "- ", ""); // 8
        else if (pressed & (1 << 10)) WrapSelection(data, "> ", ""); // GraveAccent
      } else {
        if (pressed & (1 << 0)) WrapSelection(data, "**", "**"); // B
        else if (pressed & (1 << 1)) WrapSelection(data, "*", "*"); // I
        else if (pressed & (1 << 2)) WrapSelection(data, "[", "](URL)"); // K
        else if (pressed & (1 << 3)) WrapSelection(data, "# ", ""); // 1
        else if (pressed & (1 << 4)) WrapSelection(data, "## ", ""); // 2
        else if (pressed & (1 << 5)) WrapSelection(data, "### ", ""); // 3
        else if (pressed & (1 << 6)) WrapSelection(data, "#### ", ""); // 4
        else if (pressed & (1 << 7)) WrapSelection(data, "##### ", ""); // 5
        else if (pressed & (1 << 10)) WrapSelection(data, "`", "`"); // GraveAccent
      }
    }
  }

  ProcessWordWrap(data);

  g_formatter_state.saved_selection_start = data->SelectionStart;
  g_formatter_state.saved_selection_end   = data->SelectionEnd;
  g_formatter_state.saved_cursor_pos      = data->CursorPos;
  g_formatter_state.has_saved_state       = true;

  return 0;
}

// Removed ApplyToolbarFormat

void WrapGlobalBuffer(char* edit_buffer, size_t buffer_size, float wrap_width, ImFont* font) {
  if (strlen(edit_buffer) == 0 || !font) return;

  constexpr int MAX_LEN = 131072;
  static char s_clean_str[MAX_LEN];
  static char s_wrapped_str[MAX_LEN];

  int len = static_cast<int>(strlen(edit_buffer));
  if (len >= MAX_LEN - 1) return;

  int clean_len = 0;
  for (int i = 0; i < len; ) {
    if (i + 1 < len && edit_buffer[i] == '\r' && edit_buffer[i+1] == '\n') {
      s_clean_str[clean_len++] = ' ';
      i += 2;
    } else if (i + 2 < len && edit_buffer[i] == '\r' && edit_buffer[i+1] == '-' && edit_buffer[i+2] == '\n') {
      i += 3;
    } else {
      s_clean_str[clean_len++] = edit_buffer[i];
      i++;
    }
  }
  s_clean_str[clean_len] = '\0';

  int wrapped_len = 0;
  int last_wrap_c_idx = 0;
  int last_space_c_idx = -1;

  float current_line_width = 0.0f;

  for (int i = 0; i <= clean_len; i++) {
    if (i == clean_len || s_clean_str[i] == '\n') {
      for (int j = last_wrap_c_idx; j < i; j++) {
        if (wrapped_len < MAX_LEN - 1) s_wrapped_str[wrapped_len++] = s_clean_str[j];
      }
      if (i < clean_len) {
        if (wrapped_len < MAX_LEN - 1) s_wrapped_str[wrapped_len++] = '\n';
      }
      last_wrap_c_idx = i + 1;
      last_space_c_idx = -1;
      current_line_width = 0.0f;
      continue;
    }

    if (s_clean_str[i] == ' ') {
      last_space_c_idx = i;
    }

    float char_width = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f,
                                      &s_clean_str[i],
                                      &s_clean_str[i] + 1).x;
    current_line_width += char_width;

    if (current_line_width > wrap_width) {
      if (last_space_c_idx != -1 && last_space_c_idx >= last_wrap_c_idx) {
        for (int j = last_wrap_c_idx; j < last_space_c_idx; j++) {
          if (wrapped_len < MAX_LEN - 1) s_wrapped_str[wrapped_len++] = s_clean_str[j];
        }
        if (wrapped_len < MAX_LEN - 2) {
          s_wrapped_str[wrapped_len++] = '\r';
          s_wrapped_str[wrapped_len++] = '\n';
        }
        last_wrap_c_idx = last_space_c_idx + 1;
        i = last_wrap_c_idx - 1;
        last_space_c_idx = -1;
        current_line_width = 0.0f;
      } else if (i > last_wrap_c_idx) {
        for (int j = last_wrap_c_idx; j < i; j++) {
          if (wrapped_len < MAX_LEN - 1) s_wrapped_str[wrapped_len++] = s_clean_str[j];
        }
        if (wrapped_len < MAX_LEN - 3) {
          s_wrapped_str[wrapped_len++] = '\r';
          s_wrapped_str[wrapped_len++] = '-';
          s_wrapped_str[wrapped_len++] = '\n';
        }
        last_wrap_c_idx = i;
        i = last_wrap_c_idx - 1;
        last_space_c_idx = -1;
        current_line_width = 0.0f;
      }
    }
  }
  s_wrapped_str[wrapped_len] = '\0';
  strncpy_s(edit_buffer, buffer_size, s_wrapped_str, _TRUNCATE);
}

} // namespace dover::overlay::notes
