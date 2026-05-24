#include "overlay/notes/layout.h"
#include "overlay/notes/manager.h"
#include "overlay/notes/formatter.h"
#include "overlay/notes/style.h"
#include "overlay/icons.h"

#include <imgui.h>
#include <string>
#include <sstream>

namespace dover::overlay {
  extern ImFont* g_font_gui;
  extern ImFont* g_fonts_editor[5];
  extern ImFont* g_fonts_preview_bold[5];
}

namespace dover::overlay::notes {

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
static int  g_force_focus_frames = 0;

static char g_edit_buffer[65536] = {};
static int  g_synced_note_idx   = -1;
static float g_editor_wrap_width = 400.0f;

// ---- Background Opacity ----
static float g_bg_alpha = 0.95f;

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

  // Normalize text by completely dropping carriage returns (\r)
  // Ensures compatibility with files edited externally in Notepad (\r\n)
  std::string clean_content;
  const char* buf = g_edit_buffer;
  int len = static_cast<int>(strlen(buf));
  clean_content.reserve(len);
  for (int i = 0; i < len; ++i) {
    if (buf[i] != '\r') {
      clean_content += buf[i];
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

} // namespace

// ---------- Public API ----------

void FlushNotesEditBuffer() {
  FlushEditBufferToNote();
}

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

  // ---- Setup window position, size, and style ----
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

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, g_maximized ? 0.0f : 2.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  if (g_maximized) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  } else {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.12f, 0.15f, 0.22f, 1.00f));
  }

  ImGui::SetNextWindowBgAlpha(g_bg_alpha);

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
  bool begin_ok = ImGui::Begin("Notes", p_open, win_flags);
  ImGui::PopStyleColor();

  if (g_maximized) {
    ImGui::PopStyleVar(3);
  } else {
    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(3);
  }

  if (begin_ok) {
    ImVec2 min_p = ImGui::GetWindowPos();
    ImVec2 max_p = ImVec2(min_p.x + ImGui::GetWindowSize().x, min_p.y + ImGui::GetWindowSize().y);
    ImU32 col_tl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.110f, 0.125f, 0.161f, g_bg_alpha)); // #1c2029 (Cool slate-blue)
    ImU32 col_tr = ImGui::ColorConvertFloat4ToU32(ImVec4(0.090f, 0.102f, 0.130f, g_bg_alpha)); // #171a21 (Base main content)
    ImU32 col_br = ImGui::ColorConvertFloat4ToU32(ImVec4(0.071f, 0.082f, 0.106f, g_bg_alpha)); // #12151b (Darker slate-blue)
    ImU32 col_bl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.094f, 0.106f, 0.137f, g_bg_alpha)); // #181b23
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(min_p, max_p, col_tl, col_tr, col_br, col_bl);
  }

  if (!begin_ok) {
    ImGui::End();
    return;
  }

  // ---- Premium Custom Toolbar / Header Row ----
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);
  ImGui::AlignTextToFramePadding();

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

  // Size Button behave like Combo
  float orig_size_y = ImGui::GetCursorPosY();
  ImGui::SetCursorPosY(orig_size_y - 2.0f);
  ImGui::TextDisabled("Size:"); 
  ImGui::SetCursorPosY(orig_size_y);
  ImGui::SameLine();
  
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
  ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
  
  ImGui::PushStyleColor(ImGuiCol_Button,           ImVec4(0.15f, 0.18f, 0.26f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,    ImVec4(0.22f, 0.27f, 0.38f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,     ImVec4(0.28f, 0.35f, 0.50f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_Border,           ImVec4(0.32f, 0.40f, 0.58f, 0.70f));
  
  const char* size_items[] = { "Tiny", "Small", "Medium", "Large", "Huge" };
  
  if (ImGui::Button(size_items[g_zoom_idx], ImVec2(80.0f, 0))) {
    ImGui::OpenPopup("##zoom_popup");
  }
  
  ImVec2 popup_pos = ImGui::GetItemRectMin();
  popup_pos.y += ImGui::GetItemRectSize().y;
  
  ImGui::SetNextWindowPos(popup_pos);
  ImGui::SetNextWindowSize(ImVec2(80.0f, 0.0f));
  
  if (ImGui::BeginPopup("##zoom_popup")) {
    for (int i = 0; i < IM_ARRAYSIZE(size_items); i++) {
      bool is_selected = (g_zoom_idx == i);
      if (ImGui::Selectable(size_items[i], is_selected)) {
        g_zoom_idx = i;
      }
      if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(4);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Change Text Size");
  ImGui::SameLine();

  // Opacity Control
  float orig_y = ImGui::GetCursorPosY();
  ImGui::SetCursorPosY(orig_y - 2.0f);
  ImGui::TextDisabled("Opacity:"); 
  ImGui::SetCursorPosY(orig_y);
  ImGui::SameLine();
  
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(0.0f, 0.0f));
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
  
  float frame_height = ImGui::GetFrameHeight(); 
  ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize,   frame_height);
  ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding,  frame_height * 0.5f);
  
  ImVec2 slider_pos = ImGui::GetCursorScreenPos();
  float slider_width = 80.0f;
  
  ImVec2 line_start = ImVec2(slider_pos.x, slider_pos.y + frame_height * 0.5f);
  ImVec2 line_end   = ImVec2(slider_pos.x + slider_width, slider_pos.y + frame_height * 0.5f);
  ImGui::GetWindowDrawList()->AddLine(line_start, line_end, ImGui::GetColorU32(ImVec4(1.00f, 1.00f, 1.00f, 0.35f)), 2.5f);
  
  ImGui::PushStyleColor(ImGuiCol_FrameBg,          ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,   ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive,    ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab,       ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
  
  ImGui::SetNextItemWidth(slider_width);
  ImGui::SliderFloat("##opacity", &g_bg_alpha, 0.00f, 1.00f, "");
  
  // Draw custom perfect circle grab (slightly smaller, 11px diameter)
  float grab_center_x = slider_pos.x + g_bg_alpha * slider_width;
  float grab_center_y = slider_pos.y + frame_height * 0.5f;
  
  ImVec4 grab_color = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
  if (ImGui::IsItemActive()) {
    grab_color = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  } else if (ImGui::IsItemHovered()) {
    grab_color = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
  }
  
  ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(grab_center_x, grab_center_y), 5.5f, ImGui::GetColorU32(grab_color), 32);
  
  ImGui::PopStyleColor(5);
  ImGui::PopStyleVar(3);

  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opacity (Ctrl+Click to type exact float value)");
  ImGui::SameLine();

  ImGui::TextDisabled("%.0f%%", g_bg_alpha * 100.0f);
  ImGui::SameLine();

  if (g_view_mode == 0) {
    ImGui::TextDisabled("|"); ImGui::SameLine();

    auto LayoutApplyFormat = [&](const char* prefix, const char* suffix) {
      ApplyToolbarFormat(prefix, suffix, g_edit_buffer, sizeof(g_edit_buffer));
      auto& ns = GetNotes();
      if (g_selected_note_idx >= 0 && g_selected_note_idx < static_cast<int>(ns.size())) {
          ns[g_selected_note_idx].is_dirty = true;
          MarkNoteChanged();
      }
      g_force_focus_frames = 3;
    };

    if (ImGui::Button(ICON_TEXT_FORMAT_BOLD)) LayoutApplyFormat("**", "**");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bold (Ctrl+B)"); ImGui::SameLine();

    if (ImGui::Button(ICON_TEXT_FORMAT_ITALIC)) LayoutApplyFormat("*", "*");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Italic (Ctrl+I)"); ImGui::SameLine();

    if (ImGui::Button(ICON_TEXT_FORMAT_STRIKETHROUGH)) LayoutApplyFormat("~~", "~~");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Strikethrough (Ctrl+Shift+X)"); ImGui::SameLine();

    if (ImGui::Button(ICON_TEXT_FORMAT_CODE)) LayoutApplyFormat("`", "`");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Code (Ctrl+`)"); ImGui::SameLine();

    if (ImGui::Button(ICON_TEXT_FORMAT_CODE_BLOCK)) LayoutApplyFormat("\n```\n", "\n```\n");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Code Block"); ImGui::SameLine();

    ImGui::SetNextItemWidth(35.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.20f, 0.20f, 0.25f, 0.60f));
    if (ImGui::BeginCombo("##h", ICON_TEXT_FORMAT_HEADINGS, ImGuiComboFlags_NoArrowButton)) {
      if (ImGui::Selectable(ICON_TEXT_FORMAT_H_ONE " Heading 1")) LayoutApplyFormat("# ", "");
      if (ImGui::Selectable(ICON_TEXT_FORMAT_H_TWO " Heading 2")) LayoutApplyFormat("## ", "");
      if (ImGui::Selectable(ICON_TEXT_FORMAT_H_THREE " Heading 3")) LayoutApplyFormat("### ", "");
      if (ImGui::Selectable(ICON_TEXT_FORMAT_H_FOUR " Heading 4")) LayoutApplyFormat("#### ", "");
      if (ImGui::Selectable(ICON_TEXT_FORMAT_H_FIVE " Heading 5")) LayoutApplyFormat("##### ", "");
      ImGui::EndCombo();
    }
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Headings");
    ImGui::SameLine();

    ImGui::SetNextItemWidth(35.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.20f, 0.20f, 0.25f, 0.60f));
    if (ImGui::BeginCombo("##l", ICON_TEXT_FORMAT_LISTS, ImGuiComboFlags_NoArrowButton)) {
      if (ImGui::Selectable(ICON_TEXT_FORMAT_LIST_BULLETS " Bullet List")) LayoutApplyFormat("- ", "");
      if (ImGui::Selectable(ICON_TEXT_FORMAT_LIST_NUMBERS " Numbered List")) LayoutApplyFormat("1. ", "");
      ImGui::EndCombo();
    }
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lists");
    ImGui::SameLine();
  }

  // --- 3. RIGHT CONTROLS (Save Status, Maximize, Close) ---
  bool any_dirty = false;
  for (const auto& n : notes) { if (n.is_dirty) { any_dirty = true; break; } }
  bool show_saved = !any_dirty && ShouldShowSavedStatus();

  float avail_x = ImGui::GetContentRegionAvail().x;
  float right_boundary = ImGui::GetCursorPosX() + avail_x;

  // Render status text at its own absolute safe zone (168px from right edge)
  if (any_dirty || show_saved) {
    float status_start = right_boundary - 168.0f;
    if (status_start > ImGui::GetCursorPosX()) ImGui::SameLine(status_start);
    else ImGui::SameLine();
    
    if (any_dirty) {
      ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.20f, 1.00f), "Saving...");
    } else {
      ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.40f, 1.00f), "Saved");
    }
  }

  // Render window decoration buttons at a locked absolute position (68px from right edge)
  float buttons_start = right_boundary - 68.0f;
  if (buttons_start > ImGui::GetCursorPosX()) ImGui::SameLine(buttons_start);
  else ImGui::SameLine();

  if (ImGui::Button(g_maximized ? ICON_WINDOW_WINDOWED : ICON_WINDOW_FULL)) {
    if (!g_maximized) {
      g_prev_pos = ImGui::GetWindowPos();
      g_prev_size = ImGui::GetWindowSize();
      g_was_maximized = true;
    }
    g_maximized = !g_maximized;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(g_maximized ? "Restore Window Size" : "Maximize Window");
  
  float close_btn_start = right_boundary - 38.0f;
  if (close_btn_start > ImGui::GetCursorPosX()) ImGui::SameLine(close_btn_start);
  else ImGui::SameLine();

  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.15f, 0.15f, 0.90f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.90f, 0.20f, 0.20f, 1.00f));
  if (ImGui::Button(ICON_WINDOW_CLOSE)) *p_open = false;
  ImGui::PopStyleColor(2);

  ImGui::PopStyleColor(3);

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
  ImGui::Separator();

  // ---- Main Layout: Sidebar + Content ----
  const float win_w    = ImGui::GetContentRegionAvail().x;
  const float win_h    = ImGui::GetContentRegionAvail().y;
  const float sb_w     = g_sidebar_visible ? g_sidebar_width : 0.0f;
  const float split_w  = g_sidebar_visible ? 5.0f   : 0.0f;
  const float cont_w   = win_w - sb_w - split_w;
  g_editor_wrap_width  = cont_w - 48.0f;
  if (g_editor_wrap_width < 100.0f) g_editor_wrap_width = 100.0f;

  // ImGui natively handles horizontal word wrapping via ImGuiInputTextFlags_NoHorizontalScroll

  // ---- SIDEBAR ----
  if (g_sidebar_visible) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("Sidebar", ImVec2(sb_w, win_h), false,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // Draw Sidebar gradient background (Steam Slate-Blue Darkest Layer - #101216 - Ultra-subtle flat gradient)
    {
      ImVec2 min_p = ImGui::GetWindowPos();
      ImVec2 max_p = ImVec2(min_p.x + ImGui::GetWindowSize().x, min_p.y + ImGui::GetWindowSize().y);
      ImU32 col_tl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.063f, 0.071f, 0.086f, g_bg_alpha)); // #101216 (Base darkest)
      ImU32 col_tr = ImGui::ColorConvertFloat4ToU32(ImVec4(0.059f, 0.067f, 0.082f, g_bg_alpha)); // #0f1115 (Micro-shading)
      ImU32 col_br = ImGui::ColorConvertFloat4ToU32(ImVec4(0.055f, 0.063f, 0.078f, g_bg_alpha)); // #0e1014
      ImU32 col_bl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.063f, 0.071f, 0.086f, g_bg_alpha)); // #101216
      ImGui::GetWindowDrawList()->AddRectFilledMultiColor(min_p, max_p, col_tl, col_tr, col_br, col_bl);
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 8.0f));

    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
      bool is_sel = (i == g_selected_note_idx);

      std::string title;
      if (is_sel && g_view_mode == 0) {
        title = ExtractTitleFromContent(g_edit_buffer);
      } else {
        title = ExtractTitleFromContent(notes[i].content);
      }
      int max_chars = static_cast<int>((sb_w - 24.0f) / 7.0f);
      if (max_chars < 8) max_chars = 8;
      if (static_cast<int>(title.size()) > max_chars) {
        title = title.substr(0, max_chars - 2) + "..";
      }
      if (notes[i].is_dirty) title = "* " + title;

      if (is_sel) {
        ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.22f, 0.38f, 0.62f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.44f, 0.68f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.33f, 0.50f, 0.75f, 1.00f));
      }

      ImGui::PushFont(g_fonts_preview_bold[2]);
      ImVec2 pos = ImGui::GetCursorScreenPos();
      std::string id_str = "##note_" + std::to_string(i);
      
      ImVec2 min_p = ImVec2(pos.x + 6.0f, pos.y);
      ImVec2 max_p = ImVec2(pos.x + sb_w, pos.y + 32.0f);
      
      ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0, 0, 0, 0));
      
      ImGui::SetCursorPosX(6.0f);
      bool selected_now = ImGui::Selectable(id_str.c_str(), is_sel,
                            ImGuiSelectableFlags_None,
                            ImVec2(sb_w - 6.0f, 32.0f));
                            
      bool is_hovered = ImGui::IsItemHovered();
      bool is_active = ImGui::IsItemActive();
      
      ImGui::PopStyleColor(3);
      
      if (is_sel || is_hovered || is_active) {
        ImVec4 highlight_color = ImVec4(0, 0, 0, 0);
        if (is_sel) {
          if (is_active) highlight_color = ImVec4(0.33f, 0.50f, 0.75f, 1.00f);
          else if (is_hovered) highlight_color = ImVec4(0.28f, 0.44f, 0.68f, 0.90f);
          else highlight_color = ImVec4(0.22f, 0.38f, 0.62f, 0.85f);
        } else {
          if (is_active) highlight_color = ImGui::GetStyle().Colors[ImGuiCol_HeaderActive];
          else if (is_hovered) highlight_color = ImGui::GetStyle().Colors[ImGuiCol_HeaderHovered];
        }
        if (highlight_color.w > 0.0f) {
          ImGui::GetWindowDrawList()->AddRectFilled(min_p, max_p, ImGui::GetColorU32(highlight_color), 4.0f);
        }
      }
      
      float text_y = pos.y + (32.0f - ImGui::GetFontSize()) * 0.5f;
      ImGui::GetWindowDrawList()->AddText(g_fonts_preview_bold[2], g_fonts_preview_bold[2]->FontSize, ImVec2(pos.x + 16.0f, text_y), ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
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
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemActive()) {
      g_sidebar_width += io.MouseDelta.x;
      if (g_sidebar_width < 100.0f) g_sidebar_width = 100.0f;
      if (g_sidebar_width > 300.0f) g_sidebar_width = 300.0f;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0.0f, 0.0f);
  }

  // ---- CONTENT PANEL (Solid Cod Gray #111111 with Concave Top Shadow) ----
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.067f, 0.067f, 0.067f, g_bg_alpha));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 24.0f));
  bool content_ok = ImGui::BeginChild("NoteContent", ImVec2(cont_w, win_h), false, ImGuiWindowFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  if (content_ok) {
    // Draw top concave/sunken inner shadow gradient
    ImVec2 min_p = ImGui::GetWindowPos();
    ImVec2 max_p = ImVec2(min_p.x + ImGui::GetWindowSize().x, min_p.y + 6.0f);
    ImU32 col_top = ImGui::ColorConvertFloat4ToU32(ImVec4(0.020f, 0.024f, 0.031f, g_bg_alpha * 0.85f)); // Dark slate-black concave shadow
    ImU32 col_bot = ImGui::ColorConvertFloat4ToU32(ImVec4(0.067f, 0.067f, 0.067f, g_bg_alpha)); // Blend seamlessly to #111111
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(min_p, max_p, col_top, col_top, col_bot, col_bot);
  }

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
    // Edit/Read toggle shifted to the left
    float_btn_pos = ImVec2(content_pos.x + content_size.x - 80.0f, content_pos.y + 10.0f);
    // Delete button placed to the right of the Edit/Read toggle
    delete_btn_pos = ImVec2(content_pos.x + content_size.x - 45.0f, content_pos.y + 10.0f);
    show_float_btn = true;
    show_delete_btn = true;
  }

  float content_h = ImGui::GetContentRegionAvail().y;

  if (g_view_mode == 0) {
    // ---- EDITOR MODE ----
    content_h = ImGui::GetContentRegionAvail().y;

    ImGui::PushFont(g_fonts_editor[g_zoom_idx]);

    if (g_force_focus_frames > 0) {
        ImGui::SetKeyboardFocusHere(0);
        g_force_focus_frames--;
        // Update formatter state as well if needed, or it handles its own.
    }

    ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_NoHorizontalScroll;
    
    // Provide formatter with active context parameters
    SetFormatterContext(g_editor_wrap_width, g_fonts_editor[g_zoom_idx]);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::Indent(24.0f);
    bool changed = ImGui::InputTextMultiline(
        "##ed", g_edit_buffer, sizeof(g_edit_buffer),
        ImVec2(-24.0f, content_h),
        input_flags,
        FormatCallback);
    ImGui::Unindent(24.0f);
    ImGui::PopStyleColor();

    // Check if formatter requested a focus spam (e.g. from toolbar interaction inside callback)
    if (GetFormatterState().focus_editor_restore_frames > 0) {
        g_force_focus_frames = GetFormatterState().focus_editor_restore_frames;
    }

    ImGui::PopFont();

    if (changed) {
      // Normalize text by dropping \r to prevent dirty loops with external files
      std::string clean_content;
      int len = static_cast<int>(strlen(g_edit_buffer));
      clean_content.reserve(len);
      for (int i = 0; i < len; ++i) {
        if (g_edit_buffer[i] != '\r') {
          clean_content += g_edit_buffer[i];
        }
      }
      if (notes[g_selected_note_idx].content != clean_content) {
        notes[g_selected_note_idx].is_dirty = true;
        MarkNoteChanged();
      }
    }

  } else {
    // ---- PREVIEW MODE ----
    content_h = ImGui::GetContentRegionAvail().y;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 12.0f);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(1.0f, 1.0f, 1.0f, 0.45f));

    ImGui::BeginChild("MDPreview", ImVec2(-6.0f, content_h), false,
                      ImGuiWindowFlags_AlwaysUseWindowPadding);
    const auto& content = notes[g_selected_note_idx].content;
    if (!content.empty()) {
      RenderMarkdown(content, g_zoom_idx);
    }
    ImGui::EndChild();

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);
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
    
    ImVec4 bg_color = ImVec4(0.118f, 0.478f, 0.812f, 0.90f); // 1. Elevated Dodger Blue base
    ImVec4 border_color = ImVec4(0.200f, 0.569f, 0.902f, 0.35f); // 2. Subtle "Satu Tingkat Lebih Terang" border (35% alpha)
    
    if (active) {
      bg_color = ImVec4(0.000f, 0.384f, 0.722f, 1.00f);
      border_color = ImVec4(0.100f, 0.486f, 0.847f, 0.50f); // 50% alpha active border
    } else if (hovered) {
      bg_color = ImVec4(0.200f, 0.639f, 1.000f, 0.98f); // 3. "Solid State" instant high-contrast hover color
      border_color = ImVec4(0.360f, 0.710f, 1.000f, 0.45f); // 45% alpha hover border
    }
    
    ImU32 bg_col32 = ImGui::ColorConvertFloat4ToU32(bg_color);
    ImU32 border_col32 = ImGui::ColorConvertFloat4ToU32(border_color);
    ImU32 text_col32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.960f, 0.965f, 0.973f, 1.00f)); // Crisp clean white
    
    // Main background solid elevated base color
    ImGui::GetForegroundDrawList()->AddRectFilled(min_p, max_p, bg_col32, 2.0f); // 2px rounded squircle box
    
    // 1. Micro-Gradient Vertikal (Sangat Tipis - Subtle 3% upper light reflection)
    {
      ImVec2 mid_p = ImVec2(max_p.x, min_p.y + 15.0f);
      ImU32 half_hl_col = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.03f)); // Ultra-subtle 3% vertical blend
      ImGui::GetForegroundDrawList()->AddRectFilled(min_p, mid_p, half_hl_col, 2.0f, ImDrawFlags_RoundCornersTop);
    }

    // 2. Pembatas Border Warna "Satu Tingkat Lebih Terang" keliling tombol
    {
      ImGui::GetForegroundDrawList()->AddRect(min_p, max_p, border_col32, 2.0f, 0, 1.0f);
    }
    
    const char* icon = (g_view_mode == 0) ? ICON_TOGGLE_READ : ICON_TOGGLE_EDIT;
    
    ImGui::PushFont(g_font_gui);
    ImVec2 text_size = ImGui::CalcTextSize(icon);
    ImVec2 text_pos = ImVec2(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f);
    ImGui::GetForegroundDrawList()->AddText(text_pos, text_col32, icon);
    ImGui::PopFont();
    
    if (hovered) {
      ImGui::SetTooltip(g_view_mode == 0 ? "Switch to Preview Mode" : "Switch to Editor Mode");
      ImGui::GetIO().WantCaptureMouse = true;
    }
    
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
    
    ImVec4 bg_color = ImVec4(0.851f, 0.220f, 0.220f, 0.90f); // 1. Elevated Red base
    ImVec4 border_color = ImVec4(0.910f, 0.318f, 0.318f, 0.35f); // 2. Subtle "Satu Tingkat Lebih Terang" border (35% alpha)
    
    if (active) {
      bg_color = ImVec4(0.722f, 0.114f, 0.114f, 1.00f);
      border_color = ImVec4(0.800f, 0.169f, 0.169f, 0.50f); // 50% alpha active border
    } else if (hovered) {
      bg_color = ImVec4(1.000f, 0.322f, 0.322f, 0.98f); // 3. "Solid State" instant high-contrast hover color
      border_color = ImVec4(1.000f, 0.471f, 0.471f, 0.45f); // 45% alpha hover border
    }
    
    ImU32 bg_col32 = ImGui::ColorConvertFloat4ToU32(bg_color);
    ImU32 border_col32 = ImGui::ColorConvertFloat4ToU32(border_color);
    ImU32 text_col32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.960f, 0.965f, 0.973f, 1.00f)); // Crisp clean white
    
    // Main background solid elevated base color
    ImGui::GetForegroundDrawList()->AddRectFilled(min_p, max_p, bg_col32, 2.0f); // 2px rounded squircle box
    
    // 1. Micro-Gradient Vertikal (Sangat Tipis - Subtle 3% upper light reflection)
    {
      ImVec2 mid_p = ImVec2(max_p.x, min_p.y + 15.0f);
      ImU32 half_hl_col = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.03f)); // Ultra-subtle 3% vertical blend
      ImGui::GetForegroundDrawList()->AddRectFilled(min_p, mid_p, half_hl_col, 2.0f, ImDrawFlags_RoundCornersTop);
    }

    // 2. Pembatas Border Warna "Satu Tingkat Lebih Terang" keliling tombol
    {
      ImGui::GetForegroundDrawList()->AddRect(min_p, max_p, border_col32, 2.0f, 0, 1.0f);
    }
    
    ImGui::PushFont(g_font_gui);
    ImVec2 text_size = ImGui::CalcTextSize(ICON_DELETE);
    ImVec2 text_pos = ImVec2(center.x - text_size.x * 0.5f + 1.0f, center.y - text_size.y * 0.5f + 1.0f);
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

  // Delete confirmation modal popup (Parent Window ID stack context)
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f); // Matched with notes frame rounding
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 16.0f));

  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.10f, 0.11f, 0.14f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.22f, 0.24f, 0.30f, 0.80f));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.88f, 0.92f, 1.00f));

  if (ImGui::BeginPopupModal("Delete Note?", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
    // Centering the header title
    ImGui::PushFont(g_font_gui);
    ImVec2 icon_size = ImGui::CalcTextSize(ICON_DELETE);
    ImGui::PopFont();
    
    ImGui::PushFont(g_fonts_preview_bold[2]);
    ImVec2 label_size = ImGui::CalcTextSize(" Delete Note");
    ImGui::PopFont();
    
    float total_header_w = icon_size.x + label_size.x;
    float header_offset = (ImGui::GetContentRegionAvail().x - total_header_w) * 0.5f;
    if (header_offset > 0.0f) {
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + header_offset);
    }
    
    // Render icon with g_font_gui and text with preview bold
    ImGui::PushFont(g_font_gui);
    ImGui::TextColored(ImVec4(0.90f, 0.40f, 0.40f, 1.00f), ICON_DELETE);
    ImGui::PopFont();
    ImGui::SameLine(0.0f, 0.0f);
    
    ImGui::PushFont(g_fonts_preview_bold[2]);
    ImGui::TextColored(ImVec4(0.90f, 0.40f, 0.40f, 1.00f), " Delete Note");
    ImGui::PopFont();
    
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    ImGui::Text("Are you sure you want to permanently delete this note?\nThis action cannot be undone.");
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    // Centering Cancel and Delete buttons
    float popup_avail_x = ImGui::GetContentRegionAvail().x;
    float total_btn_width = 110.0f + ImGui::GetStyle().ItemSpacing.x + 110.0f;
    float offset_x = (popup_avail_x - total_btn_width) * 0.5f;
    if (offset_x > 0.0f) {
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);
    }

    // Cancel Button (Left, Slate neutral)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.20f, 0.25f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.27f, 0.33f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.16f, 0.20f, 1.00f));
    if (ImGui::Button("Cancel", ImVec2(110.0f, 32.0f))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();

    // Delete Button (Right, Soft danger red)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.20f, 0.20f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60f, 0.15f, 0.15f, 1.00f));
    if (ImGui::Button("Delete", ImVec2(110.0f, 32.0f))) {
      DeleteNote(static_cast<size_t>(g_selected_note_idx));
      auto& ns = GetNotes();
      if (g_selected_note_idx >= static_cast<int>(ns.size()))
        g_selected_note_idx = static_cast<int>(ns.size()) - 1;
      SyncEditBufferFromNote(g_selected_note_idx);
      ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(3);

    ImGui::EndPopup();
  }

  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(6);

  ImGui::End();
}

} // namespace dover::overlay::notes
