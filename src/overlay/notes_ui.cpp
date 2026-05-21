#include "overlay/notes_ui.h"
#include "overlay/notes_manager.h"

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
static float g_sidebar_width = 180.0f;

// ---- Editor States ----
static int  g_selected_note_idx = 0;
static bool g_sidebar_visible   = true;
static int  g_view_mode         = 1;   // 0=editor, 1=preview
static int  g_zoom_idx          = 2;   // 0=Tiny, 1=Small, 2=Medium, 3=Large, 4=Huge

static char g_edit_buffer[65536] = {};
static int  g_synced_note_idx   = -1;

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
  ImGuiIO& io = ImGui::GetIO();
  if (io.KeyCtrl) {
    if (ImGui::IsKeyPressed(ImGuiKey_B, false))           { WrapSelection(data, "**", "**"); }
    if (ImGui::IsKeyPressed(ImGuiKey_I, false))           { WrapSelection(data, "*",  "*");  }
    if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false)) { WrapSelection(data, "`",  "`");  }
    if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_X, false)) { WrapSelection(data, "~~", "~~"); }
  }
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

  // ---- Setup window position, size, and style ----
  // Always borderless (NoTitleBar). If maximized, block moving/resizing.
  ImGuiWindowFlags win_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
  if (g_maximized) {
    ImGui::SetNextWindowPos( ImVec2(0.0f, kNavBarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(display_size.x, display_size.y - kNavBarHeight), ImGuiCond_Always);
    win_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
  } else {
    ImGui::SetNextWindowSize(ImVec2(700.0f, 450.0f), ImGuiCond_FirstUseEver);
  }

  // Set slightly transparent background for overlay feel
  ImGui::SetNextWindowBgAlpha(0.95f);

  if (!ImGui::Begin("Notes", p_open, win_flags)) {
    ImGui::End();
    return;
  }

  // ---- Premium Custom Toolbar / Header Row ----
  ImGui::AlignTextToFramePadding();

  const char* sidebar_lbl = g_sidebar_visible ? "< Hide Sidebar" : "> Show Sidebar";
  if (ImGui::Button(sidebar_lbl)) {
    g_sidebar_visible = !g_sidebar_visible;
  }
  ImGui::SameLine();

  if (ImGui::Button("+ New Note")) {
    std::string new_title = CreateAutoNote();
    if (!new_title.empty()) {
      auto& ns = GetNotes();
      for (int i = 0; i < static_cast<int>(ns.size()); ++i) {
        if (ns[i].title == new_title) { SelectNote(i); break; }
      }
      SwitchToEditor();
    }
  }
  ImGui::SameLine();

  ImGui::SetNextItemWidth(110.0f);
  const char* size_items[] = { "Size: Tiny", "Size: Small", "Size: Medium", "Size: Large", "Size: Huge" };
  ImGui::Combo("##zoom", &g_zoom_idx, size_items, IM_ARRAYSIZE(size_items));
  ImGui::SameLine();

  // Save indicator (Steam-style)
  bool any_dirty = false;
  for (const auto& n : notes) { if (n.is_dirty) { any_dirty = true; break; } }
  if (any_dirty) {
    ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.20f, 1.00f), "Saving...");
  } else {
    ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.40f, 1.00f), "Saved");
  }

  // --- Right-aligned toolbar items (Delete, Maximize/Restore, Close X) ---
  float avail_x = ImGui::GetContentRegionAvail().x;
  // Calculate total width needed for right items:
  // Close: ~28px, Maximize: ~75px, Delete: ~95px/140px. Align from the right edge.
  float right_align_start = ImGui::GetCursorPosX() + avail_x - 220.0f;
  ImGui::SameLine(right_align_start > ImGui::GetCursorPosX() ? right_align_start : ImGui::GetCursorPosX());

  // 1. Delete button
  if (!notes.empty()) {
    if (!g_confirm_delete) {
      ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.45f, 0.12f, 0.12f, 0.70f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.18f, 0.18f, 1.00f));
      if (ImGui::Button("Delete")) g_confirm_delete = true;
      ImGui::PopStyleColor(2);
    } else {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.15f, 0.15f, 0.90f));
      if (ImGui::Button("Confirm?")) {
        DeleteNote(static_cast<size_t>(g_selected_note_idx));
        auto& ns = GetNotes();
        if (g_selected_note_idx >= static_cast<int>(ns.size()))
          g_selected_note_idx = static_cast<int>(ns.size()) - 1;
        SyncEditBufferFromNote(g_selected_note_idx);
        g_confirm_delete = false;
      }
      ImGui::PopStyleColor();
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) g_confirm_delete = false;
    }
  }
  ImGui::SameLine();

  // 2. Maximize / Restore button
  if (ImGui::Button(g_maximized ? "Restore" : "Maximize")) {
    g_maximized = !g_maximized;
  }
  ImGui::SameLine();

  // 3. Custom Close Button (X)
  ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.30f, 0.30f, 0.35f, 0.50f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.15f, 0.15f, 0.90f));
  if (ImGui::Button(" X ")) {
    *p_open = false;
  }
  ImGui::PopStyleColor(2);

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
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 5.0f));

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

    ImGui::PopStyleVar();
    ImGui::EndChild();

    // Splitter bar
    ImGui::SameLine();
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
    ImGui::SameLine();
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

    ImGui::TextDisabled("Shortcuts: Ctrl+B Bold | Ctrl+I Italic | Ctrl+` Code | Ctrl+Shift+X Strike");
    ImGui::SameLine();
    if (ImGui::SmallButton("Preview")) {
      FlushEditBufferToNote();
      g_view_mode = 1;
    }
    ImGui::Separator();
    content_h = ImGui::GetContentRegionAvail().y;

    ImGui::PushFont(g_fonts_editor[g_zoom_idx]);
    bool changed = ImGui::InputTextMultiline(
        "##ed", g_edit_buffer, sizeof(g_edit_buffer),
        ImVec2(-FLT_MIN, content_h),
        ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways,
        FormatCallback);
    ImGui::PopFont();

    if (changed) {
      notes[g_selected_note_idx].is_dirty = true;
    }

    bool click_outside = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                         ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                         !ImGui::IsAnyItemHovered();

    if (ImGui::IsItemDeactivated() || click_outside) {
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
