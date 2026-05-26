#include "overlay/notes/layout.h"
#include "overlay/overlay_ui.h"
#include "overlay/notes/manager.h"
#include "overlay/notes/formatter.h"
#include "overlay/notes/style.h"
#include "overlay/icons.h"
#include "overlay/game_storage.h"
#include "shared/log.h"

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
std::string ExtractTitleFromContent(const std::string& content) {
  if (content.empty()) return "(empty)";
  std::istringstream ss(content);
  std::string line;
  while (std::getline(ss, line)) {
    size_t start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) continue;
    line = line.substr(start);
    if (line.empty()) continue;
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
} // namespace

static NotesWindow g_notes_window;

NotesWindow& GetNotesWindow() {
    return g_notes_window;
}

void NotesWindow::SyncEditBufferFromNote(int idx) {
  auto& notes = GetNotes();
  if (idx < 0 || static_cast<size_t>(idx) >= notes.size()) return;
  strncpy_s(m_edit_buffer, sizeof(m_edit_buffer),
            notes[idx].content.c_str(), _TRUNCATE);
  m_synced_note_idx = idx;
}

void NotesWindow::FlushEditBufferToNote() {
  auto& notes = GetNotes();
  if (m_synced_note_idx < 0 ||
      static_cast<size_t>(m_synced_note_idx) >= notes.size()) return;
  auto& note = notes[m_synced_note_idx];

  std::string clean_content;
  const char* buf = m_edit_buffer;
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

void NotesWindow::SelectNote(int idx, bool save_state) {
  FlushEditBufferToNote();
  m_selected_note_idx = idx;
  SyncEditBufferFromNote(idx);
  m_view_mode = 1;
  if (save_state) {
    GameStorage::Get().SaveState();
  }
}

void NotesWindow::SelectNoteByFilename(const std::string& filename) {
  auto& notes = GetNotes();
  {
    std::string msg = "NotesWindow::SelectNoteByFilename - Searching for filename: '" + filename + "', total notes loaded: " + std::to_string(notes.size());
    shared::LogInfo(msg.c_str());
  }
  for (size_t i = 0; i < notes.size(); ++i) {
    {
      std::string msg = "  Note [" + std::to_string(i) + "]: filename='" + notes[i].filename + "', title='" + notes[i].title + "'";
      shared::LogInfo(msg.c_str());
    }
    if (notes[i].filename == filename) {
      {
        std::string msg = "NotesWindow::SelectNoteByFilename - MATCH found at index " + std::to_string(i) + ". Selecting note.";
        shared::LogInfo(msg.c_str());
      }
      SelectNote(static_cast<int>(i), false);
      return;
    }
  }
  shared::LogInfo("NotesWindow::SelectNoteByFilename - NO MATCH found. Falling back to note index 0.");
  if (!notes.empty()) {
    SelectNote(0, false);
  }
}

std::string NotesWindow::GetSelectedNoteFilename() const {
  auto& notes = GetNotes();
  if (m_selected_note_idx >= 0 && static_cast<size_t>(m_selected_note_idx) < notes.size()) {
    std::string fn = notes[m_selected_note_idx].filename;
    {
      std::string msg = "NotesWindow::GetSelectedNoteFilename - Active note index " + std::to_string(m_selected_note_idx) + " -> filename='" + fn + "'";
      shared::LogInfo(msg.c_str());
    }
    return fn;
  }
  shared::LogInfo("NotesWindow::GetSelectedNoteFilename - Active index invalid or empty, returning empty string.");
  return "";
}

void NotesWindow::SwitchToEditor() {
  m_view_mode = 0;
}

void NotesWindow::FlushEditBuffer() {
  FlushEditBufferToNote();
}

void NotesWindow::Initialize() {
  m_selected_note_idx = 0;
  m_view_mode         = 1;
  m_sidebar_visible   = true;
  m_is_maximized      = false;
  SyncEditBufferFromNote(0);
}

void NotesWindow::Shutdown() {
  FlushEditBufferToNote();
}

void NotesWindow::RenderToolbar(bool interactive) {
    auto& notes = GetNotes();
    float win_w = ImGui::GetWindowWidth();
    bool show_format_buttons = win_w >= 550.0f;
    bool show_opacity = win_w >= 470.0f;
    bool show_size = win_w >= 320.0f;
    bool show_add_new = win_w >= 170.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);
    ImGui::AlignTextToFramePadding();

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.20f, 0.25f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.28f, 0.28f, 0.35f, 0.80f));

    const char* sidebar_lbl = m_sidebar_visible ? ICON_TOGGLE_HIDE_SIDEBAR : ICON_TOGGLE_SHOW_SIDEBAR;
    if (ImGui::Button(sidebar_lbl)) {
      m_sidebar_visible = !m_sidebar_visible;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(m_sidebar_visible ? "Hide Sidebar" : "Show Sidebar");
    ImGui::SameLine();

    if (show_add_new) {
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
    }

    if (show_size) {
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
      
      if (ImGui::Button(size_items[m_zoom_idx], ImVec2(80.0f, 0))) {
        ImGui::OpenPopup("##zoom_popup");
      }
      
      ImVec2 popup_pos = ImGui::GetItemRectMin();
      popup_pos.y += ImGui::GetItemRectSize().y;
      
      ImGui::SetNextWindowPos(popup_pos);
      ImGui::SetNextWindowSize(ImVec2(80.0f, 0.0f));
      
      if (ImGui::BeginPopup("##zoom_popup")) {
        for (int i = 0; i < IM_ARRAYSIZE(size_items); i++) {
          bool is_selected = (m_zoom_idx == i);
          if (ImGui::Selectable(size_items[i], is_selected)) {
            m_zoom_idx = i;
          }
          if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndPopup();
      }
      ImGui::PopStyleColor(4);
      ImGui::PopStyleVar(4);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Change Text Size");
      ImGui::SameLine();
      ImGui::TextDisabled("|"); ImGui::SameLine();
    }

    if (show_opacity) {
      float orig_y = ImGui::GetCursorPosY();
      ImGui::SetCursorPosY(orig_y - 2.0f);
      ImGui::TextDisabled("Opacity:"); 
      ImGui::SetCursorPosY(orig_y);
      ImGui::SameLine();
      
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(0.0f, 0.0f));
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
      
      ImGui::PushStyleColor(ImGuiCol_FrameBg,          ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,   ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgActive,    ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
      ImGui::PushStyleColor(ImGuiCol_SliderGrab,       ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
      ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
      
      const float slider_width = 80.0f;
      float frame_height = ImGui::GetFrameHeight();
      ImVec2 slider_pos = ImGui::GetCursorScreenPos();
      ImGui::SetNextItemWidth(slider_width);
      ImGui::SliderFloat("##opacity", &m_bg_alpha, 0.00f, 1.00f, "");
      
      float grab_center_x = slider_pos.x + m_bg_alpha * slider_width;
      float grab_center_y = slider_pos.y + frame_height * 0.5f;
      
      ImVec4 grab_color = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
      if (ImGui::IsItemActive()) {
        grab_color = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
      } else if (ImGui::IsItemHovered()) {
        grab_color = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
      }
      
      // Draw modern slim background track
      float track_h = 3.0f;
      ImVec2 track_min = ImVec2(slider_pos.x, slider_pos.y + frame_height * 0.5f - track_h * 0.5f);
      ImVec2 track_max = ImVec2(slider_pos.x + slider_width, slider_pos.y + frame_height * 0.5f + track_h * 0.5f);
      ImGui::GetWindowDrawList()->AddRectFilled(track_min, track_max, ImGui::GetColorU32(ImVec4(1.00f, 1.00f, 1.00f, 0.15f)), 1.5f);

      // Draw active filled track (theme accent color matching other premium buttons)
      if (m_bg_alpha > 0.0f) {
        ImVec2 active_max = ImVec2(grab_center_x, slider_pos.y + frame_height * 0.5f + track_h * 0.5f);
        ImGui::GetWindowDrawList()->AddRectFilled(track_min, active_max, ImGui::GetColorU32(ImVec4(0.118f, 0.478f, 0.812f, 0.90f)), 1.5f);
      }
      
      // Draw premium grab indicator
      ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(grab_center_x, grab_center_y), 5.5f, ImGui::GetColorU32(grab_color), 32);
      
      ImGui::PopStyleColor(5);
      ImGui::PopStyleVar(1);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opacity (Ctrl+Click to type exact float value)");
      ImGui::SameLine();

      ImGui::TextDisabled("%.0f%%", m_bg_alpha * 100.0f);
      ImGui::SameLine();
    }

    if (show_format_buttons && m_view_mode == 0) {
      ImGui::TextDisabled("|"); ImGui::SameLine();

      auto LayoutApplyFormat = [&](const char* prefix, const char* suffix) {
        ApplyToolbarFormat(prefix, suffix, m_edit_buffer, sizeof(m_edit_buffer));
        auto& ns = GetNotes();
        if (m_selected_note_idx >= 0 && m_selected_note_idx < static_cast<int>(ns.size())) {
            ns[m_selected_note_idx].is_dirty = true;
            MarkNoteChanged();
        }
        m_force_focus_frames = 3;
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
    
    // Save status text
    bool any_dirty = false;
    for (const auto& n : notes) { if (n.is_dirty) { any_dirty = true; break; } }
    bool show_saved = !any_dirty && ShouldShowSavedStatus();

    float avail_x = ImGui::GetContentRegionAvail().x;
    float right_boundary = ImGui::GetCursorPosX() + avail_x;

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
    
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(); // Pop ImGuiStyleVar_FramePadding pushed for toolbar
}

void NotesWindow::RenderContent(bool interactive) {
  auto& notes = GetNotes();
  ImGuiIO& io = ImGui::GetIO();

  if (!notes.empty() && m_selected_note_idx >= static_cast<int>(notes.size())) {
    m_selected_note_idx = static_cast<int>(notes.size()) - 1;
  }
  if (m_synced_note_idx != m_selected_note_idx) {
    SyncEditBufferFromNote(m_selected_note_idx);
  }

  TickAutosave();

  const float win_w    = ImGui::GetContentRegionAvail().x;
  const float win_h    = ImGui::GetContentRegionAvail().y;
  const float sb_w     = (interactive && m_sidebar_visible) ? m_sidebar_width : 0.0f;
  const float split_w  = (interactive && m_sidebar_visible) ? 5.0f   : 0.0f;
  const float cont_w   = win_w - sb_w - split_w;

  if (interactive && m_sidebar_visible) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("Sidebar", ImVec2(sb_w, win_h), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    {
      ImVec2 min_p = ImGui::GetWindowPos();
      ImVec2 max_p = ImVec2(min_p.x + ImGui::GetWindowSize().x, min_p.y + ImGui::GetWindowSize().y);
      ImU32 col_tl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.063f, 0.071f, 0.086f, m_bg_alpha));
      ImU32 col_tr = ImGui::ColorConvertFloat4ToU32(ImVec4(0.059f, 0.067f, 0.082f, m_bg_alpha));
      ImU32 col_br = ImGui::ColorConvertFloat4ToU32(ImVec4(0.055f, 0.063f, 0.078f, m_bg_alpha));
      ImU32 col_bl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.063f, 0.071f, 0.086f, m_bg_alpha));
      ImGui::GetWindowDrawList()->AddRectFilledMultiColor(min_p, max_p, col_tl, col_tr, col_br, col_bl);
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 8.0f));

    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
      bool is_sel = (i == m_selected_note_idx);

      std::string title;
      if (is_sel && m_view_mode == 0) {
        title = ExtractTitleFromContent(m_edit_buffer);
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
      ImVec2 max_p = ImVec2(pos.x + sb_w - 6.0f, pos.y + 32.0f);
      
      ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0, 0, 0, 0));
      
      ImGui::SetCursorPosX(6.0f);
      bool selected_now = ImGui::Selectable(id_str.c_str(), is_sel,
                            ImGuiSelectableFlags_None,
                            ImVec2(sb_w - 12.0f, 32.0f));
                            
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

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::Button("##spl", ImVec2(split_w, win_h));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemActive()) {
      m_sidebar_width += io.MouseDelta.x;
      if (m_sidebar_width < 100.0f) m_sidebar_width = 100.0f;
      if (m_sidebar_width > 300.0f) m_sidebar_width = 300.0f;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0.0f, 0.0f);
  }

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.067f, 0.067f, 0.067f, m_bg_alpha));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.30f));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(1.0f, 1.0f, 1.0f, 0.45f));
  
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 24.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 12.0f);
  
  bool content_ok = ImGui::BeginChild("NoteContent", ImVec2(cont_w, win_h), false, ImGuiWindowFlags_AlwaysUseWindowPadding);

  if (content_ok) {
    ImVec2 min_p = ImGui::GetWindowPos();
    ImVec2 max_p = ImVec2(min_p.x + ImGui::GetWindowSize().x, min_p.y + 6.0f);
    ImU32 col_top = ImGui::ColorConvertFloat4ToU32(ImVec4(0.020f, 0.024f, 0.031f, m_bg_alpha * 0.85f));
    ImU32 col_bot = ImGui::ColorConvertFloat4ToU32(ImVec4(0.067f, 0.067f, 0.067f, m_bg_alpha));
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(min_p, max_p, col_top, col_top, col_bot, col_bot);
  }

  if (notes.empty()) {
    ImGui::TextDisabled("No notes. Click \"+ New Note\" to get started.");
    ImGui::EndChild();
    return;
  }

  float content_h = ImGui::GetContentRegionAvail().y;
  float avail_w = ImGui::GetContentRegionAvail().x;
  m_editor_wrap_width = avail_w - 48.0f;
  if (m_editor_wrap_width < 100.0f) m_editor_wrap_width = 100.0f;

  static float s_last_wrap_width = 0.0f;
  if (m_editor_wrap_width != s_last_wrap_width && m_view_mode == 0) {
      WrapGlobalBuffer(m_edit_buffer, sizeof(m_edit_buffer), m_editor_wrap_width, g_fonts_editor[m_zoom_idx]);
      s_last_wrap_width = m_editor_wrap_width;
  }

  if (m_view_mode == 0) {
    ImGui::PushFont(g_fonts_editor[m_zoom_idx]);

    if (m_force_focus_frames > 0) {
        ImGui::SetKeyboardFocusHere(0);
        m_force_focus_frames--;
    }

    ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_NoHorizontalScroll;
    
    SetFormatterContext(m_editor_wrap_width, g_fonts_editor[m_zoom_idx]);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::Indent(24.0f);
    bool changed = ImGui::InputTextMultiline(
        "##ed", m_edit_buffer, sizeof(m_edit_buffer),
        ImVec2(-6.0f, content_h),
        input_flags,
        FormatCallback);
    ImGui::Unindent(24.0f);
    ImGui::PopStyleColor();

    if (GetFormatterState().focus_editor_restore_frames > 0) {
        m_force_focus_frames = GetFormatterState().focus_editor_restore_frames;
    }

    ImGui::PopFont();

    if (changed) {
      std::string clean_content;
      int len = static_cast<int>(strlen(m_edit_buffer));
      clean_content.reserve(len);
      for (int i = 0; i < len; ++i) {
        if (m_edit_buffer[i] != '\r') {
          clean_content += m_edit_buffer[i];
        }
      }
      if (notes[m_selected_note_idx].content != clean_content) {
        notes[m_selected_note_idx].is_dirty = true;
        MarkNoteChanged();
      }
    }

  } else {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 0.0f));
    ImGui::BeginChild("MDPreview", ImVec2(-6.0f, content_h), false,
                      ImGuiWindowFlags_AlwaysUseWindowPadding);
    const auto& content = notes[m_selected_note_idx].content;
    if (!content.empty()) {
      RenderMarkdown(content, m_zoom_idx);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
  }

  ImGui::EndChild(); // End NoteContent
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor(5);
}

void NotesWindow::PostRender(bool interactive) {
    if (!interactive) return;

    ImVec2 content_pos = ImGui::GetWindowPos();
    ImVec2 content_size = ImGui::GetWindowSize();
    ImVec2 float_btn_pos = ImVec2(content_pos.x + content_size.x - 80.0f, content_pos.y + 46.0f);
    ImVec2 delete_btn_pos = ImVec2(content_pos.x + content_size.x - 45.0f, content_pos.y + 46.0f);

    {
        ImVec2 min_p = float_btn_pos;
        ImVec2 max_p = ImVec2(float_btn_pos.x + 30.0f, float_btn_pos.y + 30.0f);
        ImVec2 center = ImVec2(float_btn_pos.x + 15.0f, float_btn_pos.y + 15.0f);
        
        bool hovered = ImGui::IsMouseHoveringRect(min_p, max_p);
        bool active = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        
        ImVec4 bg_color = ImVec4(0.118f, 0.478f, 0.812f, 0.90f);
        ImVec4 border_color = ImVec4(0.200f, 0.569f, 0.902f, 0.35f);
        
        if (active) {
            bg_color = ImVec4(0.000f, 0.384f, 0.722f, 1.00f);
            border_color = ImVec4(0.100f, 0.486f, 0.847f, 0.50f);
        } else if (hovered) {
            bg_color = ImVec4(0.200f, 0.639f, 1.000f, 0.98f);
            border_color = ImVec4(0.360f, 0.710f, 1.000f, 0.45f);
        }
        
        ImU32 bg_col32 = ImGui::ColorConvertFloat4ToU32(bg_color);
        ImU32 border_col32 = ImGui::ColorConvertFloat4ToU32(border_color);
        ImU32 text_col32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.960f, 0.965f, 0.973f, 1.00f));
        
        ImGui::GetForegroundDrawList()->AddRectFilled(min_p, max_p, bg_col32, 2.0f);
        
        ImVec2 mid_p = ImVec2(max_p.x, min_p.y + 15.0f);
        ImU32 half_hl_col = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.03f));
        ImGui::GetForegroundDrawList()->AddRectFilled(min_p, mid_p, half_hl_col, 2.0f, ImDrawFlags_RoundCornersTop);
        ImGui::GetForegroundDrawList()->AddRect(min_p, max_p, border_col32, 2.0f, 0, 1.0f);
        
        const char* icon = (m_view_mode == 0) ? ICON_TOGGLE_READ : ICON_TOGGLE_EDIT;
        
        ImGui::PushFont(g_font_gui);
        ImVec2 text_size = ImGui::CalcTextSize(icon);
        ImVec2 text_pos = ImVec2(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f);
        ImGui::GetForegroundDrawList()->AddText(text_pos, text_col32, icon);
        ImGui::PopFont();
        
        if (hovered) {
            ImGui::SetTooltip(m_view_mode == 0 ? "Switch to Preview Mode" : "Switch to Editor Mode");
            ImGui::GetIO().WantCaptureMouse = true;
        }
        
        if (clicked) {
            if (m_view_mode == 0) {
                FlushEditBufferToNote();
                m_view_mode = 1;
            } else {
                SwitchToEditor();
            }
        }
    }

    {
        ImVec2 min_p = delete_btn_pos;
        ImVec2 max_p = ImVec2(delete_btn_pos.x + 30.0f, delete_btn_pos.y + 30.0f);
        ImVec2 center = ImVec2(delete_btn_pos.x + 15.0f, delete_btn_pos.y + 15.0f);
        
        bool hovered = ImGui::IsMouseHoveringRect(min_p, max_p);
        bool active = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        
        ImVec4 bg_color = ImVec4(0.851f, 0.220f, 0.220f, 0.90f);
        ImVec4 border_color = ImVec4(0.910f, 0.318f, 0.318f, 0.35f);
        
        if (active) {
            bg_color = ImVec4(0.722f, 0.114f, 0.114f, 1.00f);
            border_color = ImVec4(0.800f, 0.169f, 0.169f, 0.50f);
        } else if (hovered) {
            bg_color = ImVec4(1.000f, 0.322f, 0.322f, 0.98f);
            border_color = ImVec4(1.000f, 0.471f, 0.471f, 0.45f);
        }
        
        ImU32 bg_col32 = ImGui::ColorConvertFloat4ToU32(bg_color);
        ImU32 border_col32 = ImGui::ColorConvertFloat4ToU32(border_color);
        ImU32 text_col32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.960f, 0.965f, 0.973f, 1.00f));
        
        ImGui::GetForegroundDrawList()->AddRectFilled(min_p, max_p, bg_col32, 2.0f);
        
        ImVec2 mid_p = ImVec2(max_p.x, min_p.y + 15.0f);
        ImU32 half_hl_col = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.03f));
        ImGui::GetForegroundDrawList()->AddRectFilled(min_p, mid_p, half_hl_col, 2.0f, ImDrawFlags_RoundCornersTop);
        ImGui::GetForegroundDrawList()->AddRect(min_p, max_p, border_col32, 2.0f, 0, 1.0f);
        
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
            auto& ns = GetNotes();
            if (!ns.empty()) {
                ns.erase(ns.begin() + m_selected_note_idx);
                MarkNoteChanged();
                if (m_selected_note_idx > 0) {
                    m_selected_note_idx--;
                }
                if (!ns.empty()) {
                    SyncEditBufferFromNote(m_selected_note_idx);
                } else {
                    m_edit_buffer[0] = '\0';
                    m_synced_note_idx = -1;
                }
            }
        }
    }
}

} // namespace dover::overlay::notes
