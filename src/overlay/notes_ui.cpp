#include "overlay/notes_ui.h"
#include "overlay/notes_manager.h"
#include "overlay/icons.h"

#include <imgui.h>
#include <imgui_md.h>
#include <windows.h>

#include <string>
#include <vector>
#include <sstream>
#include <chrono>

namespace dover::overlay {

extern ImFont* g_font_gui;
extern ImFont* g_fonts_editor[5];
extern ImFont* g_fonts_preview[5];
extern ImFont* g_fonts_preview_bold[5];
extern ImFont* g_fonts_preview_italic[5];
extern ImFont* g_fonts_preview_bold_italic[5];
extern ImFont* g_fonts_preview_h1[5];
extern ImFont* g_fonts_preview_h2[5];
extern ImFont* g_fonts_preview_h3[5];

namespace {

constexpr float kNavBarHeight = 42.0f;

// ---- UI Window States ----
static bool g_maximized = false;
static float g_sidebar_width = 240.0f;
static ImVec2 g_prev_pos(0.0f, 0.0f);
static ImVec2 g_prev_size(0.0f, 0.0f);
static bool g_was_maximized = false;

// ---- Editor States ----
static int  g_selected_note_idx = 0;
static bool g_sidebar_visible   = true;
static int  g_view_mode         = 1;   // 0=editor, 1=preview
static int  g_zoom_idx          = 2;   // 0=Tiny, 1=Small, 2=Medium, 3=Large, 4=Huge
static int g_force_focus_frames = 0;
static int g_focus_editor_restore_frames = 0;

static int  g_saved_selection_start = 0;
static int  g_saved_selection_end   = 0;
static int  g_saved_cursor_pos      = 0;
static bool g_has_saved_state       = false;

static char g_edit_buffer[65536] = {};
static int  g_synced_note_idx   = -1;
static float g_editor_wrap_width = 400.0f;

enum PendingFormat {
  FORMAT_NONE, FORMAT_BOLD, FORMAT_ITALIC, FORMAT_STRIKETHROUGH, FORMAT_UNDERLINE,
  FORMAT_CODE, FORMAT_CODE_BLOCK, FORMAT_H1, FORMAT_H2, FORMAT_H3, FORMAT_H4, FORMAT_H5,
  FORMAT_LIST_BULLET, FORMAT_LIST_NUMBER, FORMAT_INDENT, FORMAT_OUTDENT
};
static PendingFormat g_pending_format = FORMAT_NONE;

// ---- Background Opacity ----
static float g_bg_alpha = 0.95f;

// ---- imgui_md renderer ----
struct DoverMarkdownRenderer : public imgui_md {
  ImFont* get_font() const override {
    int idx = g_zoom_idx;
    if (m_is_code) return g_fonts_editor[idx];
    if (m_hlevel == 1) return g_fonts_preview_h1[idx];
    if (m_hlevel == 2) return g_fonts_preview_h2[idx];
    if (m_hlevel >= 3) return g_fonts_preview_h3[idx];
    if (m_is_strong && m_is_em) return g_fonts_preview_bold_italic[idx];
    if (m_is_strong) return g_fonts_preview_bold[idx];
    if (m_is_em) return g_fonts_preview_italic[idx];
    if (m_is_table_header) return g_fonts_preview_bold[idx];
    return g_fonts_preview[idx];
  }
  
  ImVec4 get_color() const override {
    if (m_is_code) {
      return ImVec4(0.85f, 0.40f, 0.40f, 1.00f); // Soft reddish-orange for code text
    }
    return imgui_md::get_color();
  }

  void BLOCK_CODE(const MD_BLOCK_CODE_DETAIL* d, bool e) override {
    m_is_code = e;
    if (e) {
      ImGui::PushFont(get_font());
      ImGui::PushStyleColor(ImGuiCol_Text, get_color());
    } else {
      ImGui::PopStyleColor();
      ImGui::PopFont();
    }
  }

  void SPAN_CODE(bool e) override {
    m_is_code = e;
    if (e) {
      ImGui::PushFont(get_font());
      ImGui::PushStyleColor(ImGuiCol_Text, get_color());
    } else {
      ImGui::PopStyleColor();
      ImGui::PopFont();
    }
  }

  void BLOCK_H(const MD_BLOCK_H_DETAIL* d, bool e) override {
    if (e) {
      m_hlevel = d->level;
      ImGui::Dummy(ImVec2(0.0f, 10.0f)); // Small gap before heading
    } else {
      m_hlevel = 0;
      ImGui::Dummy(ImVec2(0.0f, 4.0f)); // Tiny gap after heading
    }
    if (e) {
      ImGui::PushFont(get_font());
    } else {
      ImGui::PopFont();
    }
  }

  int m_list_level = 0;

  void BLOCK_UL(const MD_BLOCK_UL_DETAIL* d, bool e) override {
    if (e) m_list_level++; else m_list_level--;
    imgui_md::BLOCK_UL(d, e);
  }
  
  void BLOCK_OL(const MD_BLOCK_OL_DETAIL* d, bool e) override {
    if (e) m_list_level++; else m_list_level--;
    imgui_md::BLOCK_OL(d, e);
  }

  void BLOCK_P(bool e) override {
    if (m_list_level > 0) return;
    if (e) {
      ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }
  }

  void open_url() const override {}
  bool get_image(image_info&) const override { return false; }
};
static DoverMarkdownRenderer g_md_renderer;

// ---------- Helpers ----------

std::string ExtractTitleFromContent(const std::string& content) {
  if (content.empty()) return "(empty)";
  std::istringstream ss(content);
  std::string line;
  while (std::getline(ss, line)) {
    size_t start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) continue;
    line = line.substr(start);
    if (line.empty()) continue;
    // Strip leading Markdown heading markers
    size_t hash_end = line.find_first_not_of('#');
    if (hash_end != std::string::npos && hash_end > 0) {
      line = line.substr(hash_end);
      size_t sp = line.find_first_not_of(' ');
      if (sp != std::string::npos) line = line.substr(sp);
    }
    if (!line.empty()) return line;
  }
  return "(empty)";
}

void SyncEditBufferFromNote(int idx) {
  auto& notes = GetNotes();
  if (idx < 0 || static_cast<size_t>(idx) >= notes.size()) return;
  strncpy_s(g_edit_buffer, sizeof(g_edit_buffer),
            notes[idx].content.c_str(), _TRUNCATE);
  g_synced_note_idx = idx;
}

void FlushEditBufferToNote() {
  auto& notes = GetNotes();
  if (g_synced_note_idx < 0 ||
      static_cast<size_t>(g_synced_note_idx) >= notes.size()) return;
  auto& note = notes[g_synced_note_idx];

  // Clean soft-wrapped characters before saving/comparing
  std::string clean_content;
  const char* buf = g_edit_buffer;
  int len = static_cast<int>(strlen(buf));
  for (int i = 0; i < len; ) {
    if (i + 1 < len && buf[i] == ' ' && buf[i+1] == '\n') {
      clean_content += ' ';
      i += 2;
    } else if (i + 1 < len && buf[i] == '-' && buf[i+1] == '\n') {
      i += 2;
    } else {
      clean_content += buf[i];
      i++;
    }
  }

  if (note.content != clean_content) {
    note.content = clean_content;
    note.is_dirty = true;
  }
}

void SelectNote(int idx) {
  FlushEditBufferToNote();
  g_selected_note_idx = idx;
  SyncEditBufferFromNote(idx);
  g_view_mode = 1;
}

void SwitchToEditor() {
  g_view_mode = 0;
}

// ---------- Formatting callback ----------

void WrapSelection(ImGuiInputTextCallbackData* data,
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

void ProcessWordWrap(ImGuiInputTextCallbackData* data) {
  if (data->BufTextLen == 0) return;

  // Build clean string and mapping from wrapped index to clean index
  std::string clean_str;
  std::vector<int> map_W_to_C(data->BufTextLen + 1, 0);
  int c_idx = 0;
  for (int i = 0; i < data->BufTextLen; ) {
    if (i + 1 < data->BufTextLen && data->Buf[i] == ' ' && data->Buf[i+1] == '\n') {
      // Soft word wrap! Map " \n" (2 bytes) to a single space ' ' (1 byte)
      map_W_to_C[i] = c_idx;
      map_W_to_C[i+1] = c_idx;
      clean_str += ' ';
      c_idx++;
      i += 2;
    } else if (i + 1 < data->BufTextLen && data->Buf[i] == '-' && data->Buf[i+1] == '\n') {
      // Soft char wrap! Map "-\n" (2 bytes) to nothing (0 bytes) since it was a broken word
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
  static int s_last_zoom_idx = -1;

  if (clean_str == s_last_clean_str &&
      g_editor_wrap_width == s_last_wrap_width &&
      g_zoom_idx == s_last_zoom_idx) {
    return;
  }

  s_last_clean_str = clean_str;
  s_last_wrap_width = g_editor_wrap_width;
  s_last_zoom_idx = g_zoom_idx;

  // Wrap clean string to build wrapped string and mapping from clean index to new wrapped index
  ImFont* font = g_fonts_editor[g_zoom_idx];
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

    float width = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f,
                                      &clean_str[last_wrap_c_idx],
                                      &clean_str[i] + 1).x;

    if (width > g_editor_wrap_width) {
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
        // Character wrap
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

  // Map cursor & selection positions
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

void WrapGlobalBuffer() {
  if (strlen(g_edit_buffer) == 0) return;

  // 1. Clean soft wrap sequences from g_edit_buffer
  std::string clean_str;
  int len = static_cast<int>(strlen(g_edit_buffer));
  for (int i = 0; i < len; ) {
    if (i + 1 < len && g_edit_buffer[i] == ' ' && g_edit_buffer[i+1] == '\n') {
      clean_str += ' ';
      i += 2;
    } else if (i + 1 < len && g_edit_buffer[i] == '-' && g_edit_buffer[i+1] == '\n') {
      i += 2;
    } else {
      clean_str += g_edit_buffer[i];
      i++;
    }
  }

  // 2. Wrap clean string
  ImFont* font = g_fonts_editor[g_zoom_idx];
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

    if (width > g_editor_wrap_width) {
      if (last_space_c_idx != -1 && last_space_c_idx > last_wrap_c_idx) {
        for (int j = last_wrap_c_idx; j <= last_space_c_idx; j++) {
          wrapped_str += clean_str[j];
        }
        wrapped_str += "\n";

        last_wrap_c_idx = last_space_c_idx + 1;
        i = last_wrap_c_idx - 1;
        last_space_c_idx = -1;
      } else if (i > last_wrap_c_idx) {
        // Character wrap
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

  // 3. Write back to g_edit_buffer
  strncpy_s(g_edit_buffer, sizeof(g_edit_buffer), wrapped_str.c_str(), _TRUNCATE);
}

int FormatCallback(ImGuiInputTextCallbackData* data) {
  if (g_focus_editor_restore_frames > 0 && g_has_saved_state) {
    data->SelectionStart          = g_saved_selection_start;
    data->SelectionEnd            = g_saved_selection_end;
    data->CursorPos               = g_saved_cursor_pos;
    g_focus_editor_restore_frames--; 
  }

  if (g_pending_format != FORMAT_NONE) {
    switch (g_pending_format) {
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
    g_pending_format = FORMAT_NONE;
  } else {
    // Robust Windows API-driven shortcut detection inside callback
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

  // Automatically wrap the editor's buffer text based on Zoom and Window width
  ProcessWordWrap(data);

  // Record selection state while editor is active/focused
  g_saved_selection_start = data->SelectionStart;
  g_saved_selection_end   = data->SelectionEnd;
  g_saved_cursor_pos      = data->CursorPos;
  g_has_saved_state       = true;

  return 0;
}

} // namespace

// ---------- Public API ----------

void InitializeNotesUI() {
  g_selected_note_idx = 0;
  g_view_mode         = 1;
  g_sidebar_visible   = true;
  g_maximized         = false;
  SyncEditBufferFromNote(0);
}

void ShutdownNotesUI() {
  FlushEditBufferToNote();
}

void RenderNotesWindow(bool* p_open) {
  auto& notes = GetNotes();
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 display_size = io.DisplaySize;

  ImVec2 float_btn_pos(0, 0);
  bool show_float_btn = false;

  // Clamp selected index
  if (!notes.empty() &&
      g_selected_note_idx >= static_cast<int>(notes.size())) {
    g_selected_note_idx = static_cast<int>(notes.size()) - 1;
  }
  if (g_synced_note_idx != g_selected_note_idx) {
    SyncEditBufferFromNote(g_selected_note_idx);
  }

  TickAutosave();

  // ---- Check Global Editor Shortcuts (Handled internally in FormatCallback) ----

  // ---- Setup window position, size, and style ----
  // Always borderless (NoTitleBar). If maximized, block moving/resizing.
  ImGuiWindowFlags win_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  if (g_maximized) {
    ImGui::SetNextWindowPos( ImVec2(0.0f, kNavBarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(display_size.x, display_size.y - kNavBarHeight), ImGuiCond_Always);
    win_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
  } else {
    if (g_was_maximized) {
      ImGui::SetNextWindowPos(g_prev_pos, ImGuiCond_Always);
      ImGui::SetNextWindowSize(g_prev_size, ImGuiCond_Always);
      g_was_maximized = false;
    } else {
      ImGui::SetNextWindowSize(ImVec2(800.0f, 500.0f), ImGuiCond_FirstUseEver);
    }
  }

  // Push window styling (No rounding, and no border when maximized, zero padding for full-bleed look)
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, g_maximized ? 0.0f : 2.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  if (g_maximized) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  } else {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.12f, 0.15f, 0.22f, 1.00f));
  }

  // Set dynamic background opacity for overlay feel
  ImGui::SetNextWindowBgAlpha(g_bg_alpha);

  bool begin_ok = ImGui::Begin("Notes", p_open, win_flags);

  if (g_maximized) {
    ImGui::PopStyleVar(3);
  } else {
    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(3);
  }

  if (!begin_ok) {
    ImGui::End();
    return;
  }

  // ---- Premium Custom Toolbar / Header Row ----
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);
  ImGui::AlignTextToFramePadding();

  // Push flat style for toolbar buttons
  ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.20f, 0.25f, 0.60f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.28f, 0.28f, 0.35f, 0.80f));

  // --- 1. LEFT CONTROLS (Sidebar, New, Delete) ---
  const char* sidebar_lbl = g_sidebar_visible ? ICON_TOGGLE_HIDE_SIDEBAR : ICON_TOGGLE_SHOW_SIDEBAR;
  if (ImGui::Button(sidebar_lbl)) {
    g_sidebar_visible = !g_sidebar_visible;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(g_sidebar_visible ? "Hide Sidebar" : "Show Sidebar");
  ImGui::SameLine();

  if (ImGui::Button(ICON_ADD_NEW)) {
    std::string new_title = CreateAutoNote();
    if (!new_title.empty()) {
      auto& ns = GetNotes();
      for (int i = 0; i < static_cast<int>(ns.size()); ++i) {
        if (ns[i].title == new_title) { SelectNote(i); break; }
      }
      SwitchToEditor();
    }
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create New Note");
  ImGui::SameLine();

  ImGui::TextDisabled("|"); ImGui::SameLine();



  if (g_view_mode == 0) {
    ImGui::TextDisabled("|"); ImGui::SameLine();

    auto ApplyToolbarFormat = [&](const char* prefix, const char* suffix) {
      int s_start = g_saved_selection_start;
      int s_end   = g_saved_selection_end;
      if (s_start > s_end) std::swap(s_start, s_end);
      
      std::string text = g_edit_buffer;
      if (s_start >= 0 && s_start <= static_cast<int>(text.length()) && s_end >= 0 && s_end <= static_cast<int>(text.length())) {
          text.insert(s_end, suffix);
          text.insert(s_start, prefix);
          strncpy_s(g_edit_buffer, sizeof(g_edit_buffer), text.c_str(), _TRUNCATE);
          
          int plen = static_cast<int>(strlen(prefix));
          int slen = static_cast<int>(strlen(suffix));
          if (s_start == s_end) {
              g_saved_cursor_pos = s_start + plen;
              g_saved_selection_start = g_saved_cursor_pos;
              g_saved_selection_end   = g_saved_cursor_pos;
          } else {
              g_saved_cursor_pos      = s_end + plen + slen;
              g_saved_selection_start = s_start + plen;
              g_saved_selection_end   = s_end + plen;
          }
          
          auto& ns = GetNotes();
          if (g_selected_note_idx >= 0 && g_selected_note_idx < static_cast<int>(ns.size())) {
              ns[g_selected_note_idx].is_dirty = true;
          }
      }
      // THE FIX: Spam perintah fokus selama 3 frame penuh!
      g_force_focus_frames = 3;
      g_focus_editor_restore_frames = 3;
      g_has_saved_state = true;
    };

    if (ImGui::Button(ICON_TEXT_FORMAT_BOLD)) ApplyToolbarFormat("**", "**");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bold (Ctrl+B)"); ImGui::SameLine();

    if (ImGui::Button(ICON_TEXT_FORMAT_ITALIC)) ApplyToolbarFormat("*", "*");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Italic (Ctrl+I)"); ImGui::SameLine();

    if (ImGui::Button(ICON_TEXT_FORMAT_STRIKETHROUGH)) ApplyToolbarFormat("~~", "~~");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Strikethrough (Ctrl+Shift+X)"); ImGui::SameLine();

    if (ImGui::Button(ICON_TEXT_FORMAT_CODE)) ApplyToolbarFormat("`", "`");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Code (Ctrl+`)"); ImGui::SameLine();

    if (ImGui::Button(ICON_TEXT_FORMAT_CODE_BLOCK)) ApplyToolbarFormat("\n```\n", "\n```\n");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Code Block"); ImGui::SameLine();

    // Headings Dropdown
    ImGui::SetNextItemWidth(35.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.20f, 0.20f, 0.25f, 0.60f));
    if (ImGui::BeginCombo("##h", ICON_TEXT_FORMAT_HEADINGS, ImGuiComboFlags_NoArrowButton)) {
      if (ImGui::Selectable(ICON_TEXT_FORMAT_H_ONE " Heading 1")) ApplyToolbarFormat("# ", "");
      if (ImGui::Selectable(ICON_TEXT_FORMAT_H_TWO " Heading 2")) ApplyToolbarFormat("## ", "");
      if (ImGui::Selectable(ICON_TEXT_FORMAT_H_THREE " Heading 3")) ApplyToolbarFormat("### ", "");
      if (ImGui::Selectable(ICON_TEXT_FORMAT_H_FOUR " Heading 4")) ApplyToolbarFormat("#### ", "");
      if (ImGui::Selectable(ICON_TEXT_FORMAT_H_FIVE " Heading 5")) ApplyToolbarFormat("##### ", "");
      ImGui::EndCombo();
    }
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Headings");
    ImGui::SameLine();

    // Lists Dropdown
    ImGui::SetNextItemWidth(35.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.20f, 0.20f, 0.25f, 0.60f));
    if (ImGui::BeginCombo("##l", ICON_TEXT_FORMAT_LISTS, ImGuiComboFlags_NoArrowButton)) {
      if (ImGui::Selectable(ICON_TEXT_FORMAT_LIST_BULLETS " Bullet List")) ApplyToolbarFormat("- ", "");
      if (ImGui::Selectable(ICON_TEXT_FORMAT_LIST_NUMBERS " Numbered List")) ApplyToolbarFormat("1. ", "");
      ImGui::EndCombo();
    }
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lists");
    ImGui::SameLine();
  }

  // Size Dropdown (Text "Size:" and the active size inside a raised square box with no arrow)
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("Size:"); ImGui::SameLine();
  
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f); // Minimal round corners
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f); // Make box stand out with a border
  
  ImGui::PushStyleColor(ImGuiCol_FrameBg,          ImVec4(0.15f, 0.18f, 0.26f, 1.00f)); // Solid steel blue bg
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,   ImVec4(0.22f, 0.27f, 0.38f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive,    ImVec4(0.28f, 0.35f, 0.50f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_Border,           ImVec4(0.32f, 0.40f, 0.58f, 0.70f)); // Glowing border
  
  ImGui::SetNextItemWidth(65.0f); // Sleek smaller width now that the arrow is gone
  const char* size_items[] = { "Tiny", "Small", "Medium", "Large", "Huge" };
  
  // ImGuiComboFlags_NoArrowButton hides the dropdown triangle entirely!
  if (ImGui::BeginCombo("##zoom", size_items[g_zoom_idx], ImGuiComboFlags_NoArrowButton)) {
    for (int i = 0; i < IM_ARRAYSIZE(size_items); i++) {
      bool is_selected = (g_zoom_idx == i);
      if (ImGui::Selectable(size_items[i], is_selected)) {
        g_zoom_idx = i;
      }
      if (is_selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(2);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Change Text Size");
  ImGui::SameLine();

  // Opacity Control (Ultra-thin flat line with perfect circle grab, no text value, with percentage display)
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("Opacity:"); ImGui::SameLine();
  
  // Render the actual SliderFloat on top of the custom drawn line, with transparent track
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(0.0f, 0.0f)); // Fit exact bounds
  
  // High precision circle calculation (GrabMinSize must equal FrameHeight, and GrabRounding is half of it)
  float frame_height = ImGui::GetFrameHeight(); 
  ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize,   frame_height);       // Perfect square grab
  ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding,  frame_height * 0.5f); // Perfect circle grab
  
  ImVec2 slider_pos = ImGui::GetCursorScreenPos();
  float slider_width = 80.0f;
  
  // Draw a custom 2.5px horizontal line right across the middle of the slider area (like '-------------------o')
  ImVec2 line_start = ImVec2(slider_pos.x, slider_pos.y + frame_height * 0.5f);
  ImVec2 line_end   = ImVec2(slider_pos.x + slider_width, slider_pos.y + frame_height * 0.5f);
  ImGui::GetWindowDrawList()->AddLine(line_start, line_end, ImGui::GetColorU32(ImVec4(1.00f, 1.00f, 1.00f, 0.35f)), 2.5f);
  
  ImGui::PushStyleColor(ImGuiCol_FrameBg,          ImVec4(0, 0, 0, 0)); // Fully transparent track!
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,   ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive,    ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab,       ImVec4(1.00f, 1.00f, 1.00f, 0.90f)); // White circular grab
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
  
  ImGui::SetNextItemWidth(slider_width);
  ImGui::SliderFloat("##opacity", &g_bg_alpha, 0.00f, 1.00f, ""); // Empty string hides numeric value
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opacity (Ctrl+Click to type exact float value)");
  ImGui::SameLine();

  ImGui::PopStyleColor(5);
  ImGui::PopStyleVar(3);

  // Display the current opacity percentage next to the slider
  ImGui::TextDisabled("%.0f%%", g_bg_alpha * 100.0f);
  ImGui::SameLine();

  // --- 3. RIGHT CONTROLS (Save Status, Maximize, Close) ---
  bool any_dirty = false;
  for (const auto& n : notes) { if (n.is_dirty) { any_dirty = true; break; } }

  float avail_x = ImGui::GetContentRegionAvail().x;
  float right_align_start = ImGui::GetCursorPosX() + avail_x - (any_dirty ? 138.0f : 108.0f);
  if (right_align_start > ImGui::GetCursorPosX()) ImGui::SameLine(right_align_start);
  else ImGui::SameLine();

  if (any_dirty) {
    ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.20f, 1.00f), "Saving...");
  } else {
    ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.40f, 1.00f), "Saved");
  }
  ImGui::SameLine();

  if (ImGui::Button(g_maximized ? ICON_WINDOW_WINDOWED : ICON_WINDOW_FULL)) {
    if (!g_maximized) {
      g_prev_pos = ImGui::GetWindowPos();
      g_prev_size = ImGui::GetWindowSize();
      g_was_maximized = true;
    }
    g_maximized = !g_maximized;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(g_maximized ? "Restore Window Size" : "Maximize Window");
  ImGui::SameLine();

  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.15f, 0.15f, 0.90f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.90f, 0.20f, 0.20f, 1.00f));
  if (ImGui::Button(ICON_WINDOW_CLOSE)) *p_open = false;
  ImGui::PopStyleColor(2);

  ImGui::PopStyleColor(3); // Pop main flat button styles

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
  ImGui::Separator();

  // ---- Main Layout: Sidebar + Content ----
  const float win_w    = ImGui::GetContentRegionAvail().x;
  const float win_h    = ImGui::GetContentRegionAvail().y;
  const float sb_w     = g_sidebar_visible ? g_sidebar_width : 0.0f;
  const float split_w  = g_sidebar_visible ? 5.0f   : 0.0f;
  const float cont_w   = win_w - sb_w - split_w;
  g_editor_wrap_width  = cont_w - 48.0f; // 24px padding on each side
  if (g_editor_wrap_width < 100.0f) g_editor_wrap_width = 100.0f;

  static float s_last_rendered_wrap_width = 0.0f;
  static int s_last_rendered_zoom_idx = -1;

  if (g_view_mode == 0) {
    if (g_editor_wrap_width != s_last_rendered_wrap_width || g_zoom_idx != s_last_rendered_zoom_idx) {
      WrapGlobalBuffer();
      s_last_rendered_wrap_width = g_editor_wrap_width;
      s_last_rendered_zoom_idx = g_zoom_idx;
    }
  }

  // ---- SIDEBAR (plain list, no collapsible header) ----
  if (g_sidebar_visible) {
    ImGui::BeginChild("Sidebar", ImVec2(sb_w, win_h), false,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));

    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
      bool is_sel = (i == g_selected_note_idx);

      std::string title;
      if (is_sel && g_view_mode == 0) {
        title = ExtractTitleFromContent(g_edit_buffer);
      } else {
        title = ExtractTitleFromContent(notes[i].content);
      }
      if (title.size() > 20) title = title.substr(0, 18) + "..";
      if (notes[i].is_dirty) title = "* " + title;

      if (is_sel) {
        ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.22f, 0.38f, 0.62f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.44f, 0.68f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.33f, 0.50f, 0.75f, 1.00f));
      }

      ImGui::PushFont(g_fonts_preview_bold[2]); // Bold and slightly larger font
      bool selected_now = ImGui::Selectable(title.c_str(), is_sel,
                            ImGuiSelectableFlags_None,
                            ImVec2(sb_w - 4.0f, 0));
      ImGui::PopFont();

      if (selected_now && !is_sel) {
        SelectNote(i);
      }

      if (is_sel) ImGui::PopStyleColor(3);
    }

    ImGui::PopStyleVar(2);
    ImGui::EndChild();

    // Splitter bar
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::Button("##spl", ImVec2(split_w, win_h));
    if (ImGui::IsItemActive()) {
      g_sidebar_width += io.MouseDelta.x;
      if (g_sidebar_width < 100.0f) g_sidebar_width = 100.0f;
      if (g_sidebar_width > 300.0f) g_sidebar_width = 300.0f;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0.0f, 0.0f);
  }

  // ---- CONTENT PANEL ----
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 24.0f));
  ImGui::BeginChild("NoteContent", ImVec2(cont_w, win_h), false, ImGuiWindowFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  if (notes.empty()) {
    ImGui::TextDisabled("No notes. Click \"+ New Note\" to get started.");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();
    return;
  }

  ImVec2 delete_btn_pos;
  bool show_delete_btn = false;
  // Calculate floating button screen position
  {
    ImVec2 content_pos = ImGui::GetWindowPos();
    ImVec2 content_size = ImGui::GetWindowSize();
    float_btn_pos = ImVec2(content_pos.x + content_size.x - 45.0f, content_pos.y + 10.0f);
    delete_btn_pos = ImVec2(content_pos.x + content_size.x - 45.0f, content_pos.y + content_size.y - 45.0f);
    show_float_btn = true;
    show_delete_btn = true;
  }

  float content_h = ImGui::GetContentRegionAvail().y;

  if (g_view_mode == 0) {
    // ---- EDITOR MODE ----
    content_h = ImGui::GetContentRegionAvail().y;

    ImGui::PushFont(g_fonts_editor[g_zoom_idx]);

    // THE FIX: Eksekusi spam fokus selama 3 frame untuk mengalahkan sistem popup ImGui
    if (g_force_focus_frames > 0) {
        ImGui::SetKeyboardFocusHere(0);
        g_force_focus_frames--;
    }

    ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_NoHorizontalScroll;
    
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    bool changed = ImGui::InputTextMultiline(
        "##ed", g_edit_buffer, sizeof(g_edit_buffer),
        ImVec2(-FLT_MIN, content_h),
        input_flags,
        FormatCallback);
    ImGui::PopStyleColor();

    ImGui::PopFont();

    if (changed) {
      // Smart dirty check: only mark dirty if the actual non-wrapped content has changed
      std::string clean_content;
      int len = static_cast<int>(strlen(g_edit_buffer));
      for (int i = 0; i < len; ) {
        if (i + 1 < len && g_edit_buffer[i] == ' ' && g_edit_buffer[i+1] == '\n') {
          clean_content += ' ';
          i += 2;
        } else if (i + 1 < len && g_edit_buffer[i] == '-' && g_edit_buffer[i+1] == '\n') {
          i += 2;
        } else {
          clean_content += g_edit_buffer[i];
          i++;
        }
      }
      if (notes[g_selected_note_idx].content != clean_content) {
        notes[g_selected_note_idx].is_dirty = true;
      }
    }

  } else {
    // ---- PREVIEW MODE ----
    
    content_h = ImGui::GetContentRegionAvail().y;

    ImGui::BeginChild("MDPreview", ImVec2(-FLT_MIN, content_h), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const auto& content = notes[g_selected_note_idx].content;
    if (!content.empty()) {
      ImGui::PushFont(g_fonts_preview[g_zoom_idx]);
      g_md_renderer.print(content.c_str(), content.c_str() + content.size());
      ImGui::PopFont();
    }

    ImGui::EndChild();
  }

  // Delete confirmation modal popup
  if (ImGui::BeginPopupModal("Delete Note?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Are you sure you want to delete this note?");
    ImGui::Separator();
    
    if (ImGui::Button("OK", ImVec2(120, 0))) {
      DeleteNote(static_cast<size_t>(g_selected_note_idx));
      auto& ns = GetNotes();
      if (g_selected_note_idx >= static_cast<int>(ns.size()))
        g_selected_note_idx = static_cast<int>(ns.size()) - 1;
      SyncEditBufferFromNote(g_selected_note_idx);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::EndChild(); // End NoteContent
  ImGui::PopStyleVar();

  // Render Floating Edit/Read Toggle Button on the Foreground Draw List (always on top, 100% clickable)
  if (show_float_btn) {
    ImVec2 min_p = float_btn_pos;
    ImVec2 max_p = ImVec2(float_btn_pos.x + 30.0f, float_btn_pos.y + 30.0f);
    ImVec2 center = ImVec2(float_btn_pos.x + 15.0f, float_btn_pos.y + 15.0f);
    
    // Check hover and click state globally
    bool hovered = ImGui::IsMouseHoveringRect(min_p, max_p);
    bool active = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    
    // Select color based on state
    ImVec4 bg_color = ImVec4(0.10f, 0.14f, 0.22f, 0.85f);
    if (active) {
      bg_color = ImVec4(0.28f, 0.35f, 0.50f, 1.00f);
    } else if (hovered) {
      bg_color = ImVec4(0.20f, 0.25f, 0.35f, 0.95f);
    }
    
    ImU32 bg_col32 = ImGui::ColorConvertFloat4ToU32(bg_color);
    ImU32 border_col32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.45f, 0.65f, 0.60f)); // Steel-blue glowing border
    ImU32 text_col32 = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]);
    
    // Draw background circle and glowing border
    ImGui::GetForegroundDrawList()->AddCircleFilled(center, 15.0f, bg_col32, 32);
    ImGui::GetForegroundDrawList()->AddCircle(center, 15.0f, border_col32, 32, 1.0f);
    
    // Render the icon text in the center
    const char* icon = (g_view_mode == 0) ? ICON_TOGGLE_READ : ICON_TOGGLE_EDIT;
    
    // Draw icon centered inside the circle
    ImGui::PushFont(g_font_gui);
    ImVec2 text_size = ImGui::CalcTextSize(icon);
    ImVec2 text_pos = ImVec2(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f);
    ImGui::GetForegroundDrawList()->AddText(text_pos, text_col32, icon);
    ImGui::PopFont();
    
    // Prevent mouse click from passing through to editor child window below
    if (hovered) {
      ImGui::SetTooltip(g_view_mode == 0 ? "Switch to Preview Mode" : "Switch to Editor Mode");
      ImGui::GetIO().WantCaptureMouse = true;
    }
    
    // Process action
    if (clicked) {
      if (g_view_mode == 0) {
        FlushEditBufferToNote();
        g_view_mode = 1;
      } else {
        SwitchToEditor();
      }
    }
  }

  // Render Floating Delete Button
  if (show_delete_btn) {
    ImVec2 min_p = delete_btn_pos;
    ImVec2 max_p = ImVec2(delete_btn_pos.x + 30.0f, delete_btn_pos.y + 30.0f);
    ImVec2 center = ImVec2(delete_btn_pos.x + 15.0f, delete_btn_pos.y + 15.0f);
    
    bool hovered = ImGui::IsMouseHoveringRect(min_p, max_p);
    bool active = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    
    ImVec4 bg_color = ImVec4(0.10f, 0.14f, 0.22f, 0.85f);
    if (active) bg_color = ImVec4(0.50f, 0.20f, 0.20f, 1.00f);
    else if (hovered) bg_color = ImVec4(0.35f, 0.15f, 0.15f, 0.95f);
    
    ImU32 bg_col32 = ImGui::ColorConvertFloat4ToU32(bg_color);
    ImU32 border_col32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.65f, 0.35f, 0.35f, 0.60f));
    ImU32 text_col32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
    
    ImGui::GetForegroundDrawList()->AddCircleFilled(center, 15.0f, bg_col32, 32);
    ImGui::GetForegroundDrawList()->AddCircle(center, 15.0f, border_col32, 32, 1.0f);
    
    ImGui::PushFont(g_font_gui);
    ImVec2 text_size = ImGui::CalcTextSize(ICON_DELETE);
    ImVec2 text_pos = ImVec2(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f);
    ImGui::GetForegroundDrawList()->AddText(text_pos, text_col32, ICON_DELETE);
    ImGui::PopFont();
    
    if (hovered) {
      ImGui::SetTooltip("Delete Note");
      ImGui::GetIO().WantCaptureMouse = true;
    }
    
    if (clicked) {
      ImGui::OpenPopup("Delete Note?");
    }
  }

  ImGui::End(); // End Notes main window
}

} // namespace dover::overlay
