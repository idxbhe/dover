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

enum PendingFormat {
  FORMAT_NONE, FORMAT_BOLD, FORMAT_ITALIC, FORMAT_STRIKETHROUGH, FORMAT_UNDERLINE,
  FORMAT_CODE, FORMAT_CODE_BLOCK, FORMAT_H1, FORMAT_H2, FORMAT_H3, FORMAT_H4, FORMAT_H5,
  FORMAT_LIST_BULLET, FORMAT_LIST_NUMBER, FORMAT_INDENT, FORMAT_OUTDENT
};
static PendingFormat g_pending_format = FORMAT_NONE;

// ---- Delete confirm ----
static bool g_confirm_delete = false;

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
  if (note.content != g_edit_buffer) {
    note.content = g_edit_buffer;
    note.is_dirty = true;
  }
}

void SelectNote(int idx) {
  FlushEditBufferToNote();
  g_selected_note_idx = idx;
  SyncEditBufferFromNote(idx);
  g_view_mode = 1;
  g_confirm_delete = false;
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
  g_confirm_delete    = false;
  SyncEditBufferFromNote(0);
}

void ShutdownNotesUI() {
  FlushEditBufferToNote();
}

void RenderNotesWindow(bool* p_open) {
  auto& notes = GetNotes();
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 display_size = io.DisplaySize;

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

  // Set slightly transparent background for overlay feel
  ImGui::SetNextWindowBgAlpha(0.95f);

  if (!ImGui::Begin("Notes", p_open, win_flags)) {
    ImGui::End();
    return;
  }

  // ---- Premium Custom Toolbar / Header Row ----
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

  if (!notes.empty()) {
    if (!g_confirm_delete) {
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.18f, 0.18f, 0.85f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.90f, 0.20f, 0.20f, 1.00f));
      if (ImGui::Button(ICON_DELETE)) g_confirm_delete = true;
      ImGui::PopStyleColor(2);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete Selected Note");
    } else {
      ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.75f, 0.15f, 0.15f, 0.90f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.20f, 0.20f, 1.00f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.00f, 0.25f, 0.25f, 1.00f));
      if (ImGui::Button(ICON_DELETE)) {
        DeleteNote(static_cast<size_t>(g_selected_note_idx));
        auto& ns = GetNotes();
        if (g_selected_note_idx >= static_cast<int>(ns.size()))
          g_selected_note_idx = static_cast<int>(ns.size()) - 1;
        SyncEditBufferFromNote(g_selected_note_idx);
        g_confirm_delete = false;
      }
      ImGui::PopStyleColor(3);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Confirm Deletion");
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) g_confirm_delete = false;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cancel Deletion");
    }
    ImGui::SameLine();
  }

  ImGui::TextDisabled("|"); ImGui::SameLine();

  // --- 2. MIDDLE CONTROLS (View Toggles & Formatting) ---
  if (g_view_mode == 0) {
    if (ImGui::Button(ICON_TOGGLE_READ)) { FlushEditBufferToNote(); g_view_mode = 1; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Switch to Preview Mode");
  } else {
    if (ImGui::Button(ICON_TOGGLE_EDIT)) SwitchToEditor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Switch to Editor Mode");
  }
  ImGui::SameLine();

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

  // Zoom Combo
  ImGui::SetNextItemWidth(100.0f);
  const char* size_items[] = { "Size: Tiny", "Size: Small", "Size: Medium", "Size: Large", "Size: Huge" };
  ImGui::Combo("##zoom", &g_zoom_idx, size_items, IM_ARRAYSIZE(size_items));
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Change Text Size");
  ImGui::SameLine();

  // --- 3. RIGHT CONTROLS (Save Status, Maximize, Close) ---
  bool any_dirty = false;
  for (const auto& n : notes) { if (n.is_dirty) { any_dirty = true; break; } }

  float avail_x = ImGui::GetContentRegionAvail().x;
  float right_align_start = ImGui::GetCursorPosX() + avail_x - (any_dirty ? 140.0f : 120.0f);
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

  ImGui::Separator();

  // ---- Main Layout: Sidebar + Content ----
  const float win_w    = ImGui::GetContentRegionAvail().x;
  const float win_h    = ImGui::GetContentRegionAvail().y;
  const float sb_w     = g_sidebar_visible ? g_sidebar_width : 0.0f;
  const float split_w  = g_sidebar_visible ? 5.0f   : 0.0f;
  const float cont_w   = win_w - sb_w - split_w;

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

      if (ImGui::Selectable(title.c_str(), is_sel,
                            ImGuiSelectableFlags_None,
                            ImVec2(sb_w - 4.0f, 0)) && !is_sel) {
        SelectNote(i);
      }

      if (is_sel) ImGui::PopStyleColor(3);
    }

    ImGui::PopStyleVar(2);
    ImGui::EndChild();

    // Splitter bar
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.14f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.25f, 0.30f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.20f, 0.25f, 1.00f));
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
  ImGui::BeginChild("NoteContent", ImVec2(cont_w, win_h), false);

  if (notes.empty()) {
    ImGui::TextDisabled("No notes. Click \"+ New Note\" to get started.");
    ImGui::EndChild();
    ImGui::End();
    return;
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

    ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways;

    bool changed = ImGui::InputTextMultiline(
        "##ed", g_edit_buffer, sizeof(g_edit_buffer),
        ImVec2(-FLT_MIN, content_h),
        input_flags,
        FormatCallback);

    ImGui::PopFont();

    if (changed) {
      notes[g_selected_note_idx].is_dirty = true;
    }

    bool click_outside = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                         ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                         !ImGui::IsAnyItemHovered();

    if (click_outside) {
      FlushEditBufferToNote();
      g_view_mode = 1;
    }

  } else {
    // ---- PREVIEW MODE ----
    
    content_h = ImGui::GetContentRegionAvail().y;

    ImGui::BeginChild("MDPreview", ImVec2(-FLT_MIN, content_h), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    bool clicked = ImGui::IsWindowHovered() &&
                   ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    const auto& content = notes[g_selected_note_idx].content;
    if (!content.empty()) {
      ImGui::PushFont(g_fonts_preview[g_zoom_idx]);
      g_md_renderer.print(content.c_str(), content.c_str() + content.size());
      ImGui::PopFont();
    }

    ImGui::EndChild();
    if (clicked) SwitchToEditor();
  }

  ImGui::EndChild();
  ImGui::End();
}

} // namespace dover::overlay
