#include "overlay/notes/layout.h"
#include "overlay/overlay_ui.h"
#include "overlay/notes/manager.h"
#include "overlay/notes/formatter.h"
#include "overlay/notes/style.h"
#include "overlay/icons.h"
#include "overlay/game_storage.h"
#include "shared/log.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <sstream>

namespace dover::overlay {
  extern ImFont* g_font_gui;
  extern ImFont* g_fonts_editor[5];
  extern ImFont* g_fonts_preview_bold[5];
}

namespace dover::overlay::notes {



static NotesWindow g_notes_window;

NotesWindow& GetNotesWindow() {
    return g_notes_window;
}

void NotesWindow::SyncEditBufferFromNote(int idx) {
  auto notes = GetNotes();
  if (idx < 0 || static_cast<size_t>(idx) >= notes.size()) return;
  strncpy_s(m_edit_buffer, sizeof(m_edit_buffer), notes[idx].content.get(), _TRUNCATE);
  m_synced_note_idx = idx;
  strncpy_s(m_synced_note_filename, sizeof(m_synced_note_filename), notes[idx].filename, _TRUNCATE);
}

void NotesWindow::FixupIndicesAfterMutation() {
    auto notes = GetNotes();
    if (notes.empty()) {
        m_selected_note_idx = 0;
        m_synced_note_idx = -1;
        m_selected_note_filename[0] = '\0';
        m_synced_note_filename[0] = '\0';
        return;
    }

    bool sel_found = false;
    if (m_selected_note_filename[0] != '\0') {
        for (size_t i = 0; i < notes.size(); ++i) {
            if (strcmp(notes[i].filename, m_selected_note_filename) == 0) {
                m_selected_note_idx = static_cast<int>(i);
                sel_found = true;
                break;
            }
        }
    }
    
    if (!sel_found) {
        if (m_selected_note_idx >= static_cast<int>(notes.size())) {
            m_selected_note_idx = static_cast<int>(notes.size() - 1);
        }
        if (m_selected_note_idx < 0) m_selected_note_idx = 0;
        strncpy_s(m_selected_note_filename, sizeof(m_selected_note_filename), notes[m_selected_note_idx].filename, _TRUNCATE);
    }

    if (m_synced_note_filename[0] != '\0') {
        bool syn_found = false;
        for (size_t i = 0; i < notes.size(); ++i) {
            if (strcmp(notes[i].filename, m_synced_note_filename) == 0) {
                m_synced_note_idx = static_cast<int>(i);
                syn_found = true;
                break;
            }
        }
        if (!syn_found) {
            m_synced_note_idx = -1;
            m_synced_note_filename[0] = '\0';
        }
    } else {
        m_synced_note_idx = -1;
    }
}

void NotesWindow::FlushEditBufferToNote() {
  auto notes = GetNotes();
  if (m_synced_note_idx < 0 || static_cast<size_t>(m_synced_note_idx) >= notes.size()) return;

  auto& note = notes[m_synced_note_idx];

  char* dest = note.content.get();
  const char* src = m_edit_buffer;
  
  bool is_different = false;
  int d_idx = 0;
  
  for (int s_idx = 0; src[s_idx] != '\0'; ) {
      if (d_idx >= MAX_NOTE_SIZE - 1) break;
      
      if (src[s_idx] == '\r' && src[s_idx+1] == '\n') {
          if (dest[d_idx] != ' ') is_different = true;
          dest[d_idx++] = ' ';
          s_idx += 2;
      } else if (src[s_idx] == '\r' && src[s_idx+1] == '-' && src[s_idx+2] == '\n') {
          s_idx += 3;
      } else {
          if (dest[d_idx] != src[s_idx]) is_different = true;
          dest[d_idx++] = src[s_idx];
          s_idx++;
      }
  }
  
  if (dest[d_idx] != '\0') is_different = true;
  dest[d_idx] = '\0';

  if (is_different) {
      note.is_dirty = true;
  }
}

void NotesWindow::SelectNote(int idx, bool save_state) {
  FlushEditBufferToNote();
  m_selected_note_idx = idx;
  auto notes = GetNotes();
  if (idx >= 0 && static_cast<size_t>(idx) < notes.size()) {
    strncpy_s(m_selected_note_filename, sizeof(m_selected_note_filename), notes[idx].filename, _TRUNCATE);
  } else {
    m_selected_note_filename[0] = '\0';
  }
  SyncEditBufferFromNote(idx);
  m_view_mode = 1;
  if (save_state) {
    GameStorage::Get().SaveState();
  }
}

void NotesWindow::SelectNoteByFilename(const char* filename) {
  auto notes = GetNotes();
  for (size_t i = 0; i < notes.size(); ++i) {
    if (strcmp(notes[i].filename, filename) == 0) {
      strncpy_s(m_selected_note_filename, sizeof(m_selected_note_filename), filename, _TRUNCATE);
      SelectNote(static_cast<int>(i), false);
      return;
    }
  }
  if (!notes.empty()) {
    SelectNote(0, false);
  }
}

const char* NotesWindow::GetSelectedNoteFilename() const {
  if (m_selected_note_filename[0] != '\0') return m_selected_note_filename;
  auto notes = GetNotes();
  if (m_selected_note_idx >= 0 && static_cast<size_t>(m_selected_note_idx) < notes.size()) {
    return notes[m_selected_note_idx].filename;
  }
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

namespace detail {

enum class FloatBtnAction { None, ToggleMode, DeleteNote };

PendingFormat RenderToolbarInternal(NotesWindow* window, bool interactive, float win_w) {
    if (!interactive) return FORMAT_NONE;

    bool show_opacity = win_w >= 500.0f;
    bool show_format_buttons = win_w >= 650.0f;
    bool show_size = win_w >= 320.0f;
    bool show_add_new = win_w >= 170.0f;
    PendingFormat format_ptr = FORMAT_NONE;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);
    ImGui::AlignTextToFramePadding();

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.20f, 0.25f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.28f, 0.28f, 0.35f, 0.80f));

    const char* sidebar_lbl = window->m_sidebar_visible ? ICON_TOGGLE_HIDE_SIDEBAR : ICON_TOGGLE_SHOW_SIDEBAR;
    if (ImGui::Button(sidebar_lbl)) {
      window->m_sidebar_visible = !window->m_sidebar_visible;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(window->m_sidebar_visible ? "Hide Sidebar" : "Show Sidebar");
    ImGui::SameLine();

    if (show_add_new) {
      if (ImGui::Button(ICON_ADD_NEW)) {
        const char* new_title = CreateAutoNote();
        if (new_title[0] != '\0') {
          auto ns = GetNotes();
          for (int i = 0; i < static_cast<int>(ns.size()); ++i) {
            if (strcmp(ns[i].title, new_title) == 0) { window->SelectNote(i); break; }
          }
          window->SwitchToEditor();
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
      
      if (ImGui::Button(size_items[window->m_zoom_idx], ImVec2(80.0f, 0))) {
        ImGui::OpenPopup("##zoom_popup");
      }
      
      ImVec2 popup_pos = ImGui::GetItemRectMin();
      popup_pos.y += ImGui::GetItemRectSize().y;
      
      ImGui::SetNextWindowPos(popup_pos);
      ImGui::SetNextWindowSize(ImVec2(80.0f, 0.0f));
      
      if (ImGui::BeginPopup("##zoom_popup")) {
        for (int i = 0; i < IM_ARRAYSIZE(size_items); i++) {
          bool is_selected = (window->m_zoom_idx == i);
          if (ImGui::Selectable(size_items[i], is_selected)) {
            window->m_zoom_idx = i;
            GameStorage::Get().SaveState();
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
      float bg_alpha = window->GetBgAlpha();
      ImGui::SliderFloat("##opacity", &bg_alpha, 0.00f, 1.00f, "");
      if (bg_alpha != window->GetBgAlpha()) {
          window->SetBgAlpha(bg_alpha);
      }
      
      float grab_center_x = slider_pos.x + bg_alpha * slider_width;
      float grab_center_y = slider_pos.y + frame_height * 0.5f;
      
      ImVec4 grab_color = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
      if (ImGui::IsItemActive()) {
        grab_color = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
      } else if (ImGui::IsItemHovered()) {
        grab_color = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
      }
      
      float track_h = 3.0f;
      ImVec2 track_min = ImVec2(slider_pos.x, slider_pos.y + frame_height * 0.5f - track_h * 0.5f);
      ImVec2 track_max = ImVec2(slider_pos.x + slider_width, slider_pos.y + frame_height * 0.5f + track_h * 0.5f);
      ImGui::GetWindowDrawList()->AddRectFilled(track_min, track_max, ImGui::GetColorU32(ImVec4(1.00f, 1.00f, 1.00f, 0.15f)), 1.5f);

      if (bg_alpha > 0.0f) {
        ImVec2 active_max = ImVec2(grab_center_x, slider_pos.y + frame_height * 0.5f + track_h * 0.5f);
        ImGui::GetWindowDrawList()->AddRectFilled(track_min, active_max, ImGui::GetColorU32(ImVec4(0.118f, 0.478f, 0.812f, 0.90f)), 1.5f);
      }
      
      ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(grab_center_x, grab_center_y), 5.5f, ImGui::GetColorU32(grab_color), 32);
      
      ImGui::PopStyleColor(5);
      ImGui::PopStyleVar(1);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opacity (Ctrl+Click to type exact float value)");
      ImGui::SameLine();

      ImGui::TextDisabled("%.0f%%", bg_alpha * 100.0f);
      ImGui::SameLine();
    }

    if (show_format_buttons && window->m_view_mode == 0) {
      ImGui::TextDisabled("|"); ImGui::SameLine();

      if (ImGui::Button(ICON_TEXT_FORMAT_BOLD)) format_ptr = FORMAT_BOLD;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bold (Ctrl+B)"); ImGui::SameLine();

      if (ImGui::Button(ICON_TEXT_FORMAT_ITALIC)) format_ptr = FORMAT_ITALIC;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Italic (Ctrl+I)"); ImGui::SameLine();

      if (ImGui::Button(ICON_TEXT_FORMAT_STRIKETHROUGH)) format_ptr = FORMAT_STRIKETHROUGH;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Strikethrough (Ctrl+Shift+X)"); ImGui::SameLine();

      if (ImGui::Button(ICON_TEXT_FORMAT_CODE)) format_ptr = FORMAT_CODE;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Code (Ctrl+`)"); ImGui::SameLine();

      if (ImGui::Button(ICON_TEXT_FORMAT_CODE_BLOCK)) format_ptr = FORMAT_CODE_BLOCK;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Code Block"); ImGui::SameLine();

      ImGui::SetNextItemWidth(35.0f);
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.20f, 0.20f, 0.25f, 0.60f));
      if (ImGui::BeginCombo("##h", ICON_TEXT_FORMAT_HEADINGS, ImGuiComboFlags_NoArrowButton)) {
        if (ImGui::Selectable(ICON_TEXT_FORMAT_H_ONE " Heading 1")) format_ptr = FORMAT_H1;
        if (ImGui::Selectable(ICON_TEXT_FORMAT_H_TWO " Heading 2")) format_ptr = FORMAT_H2;
        if (ImGui::Selectable(ICON_TEXT_FORMAT_H_THREE " Heading 3")) format_ptr = FORMAT_H3;
        if (ImGui::Selectable(ICON_TEXT_FORMAT_H_FOUR " Heading 4")) format_ptr = FORMAT_H4;
        if (ImGui::Selectable(ICON_TEXT_FORMAT_H_FIVE " Heading 5")) format_ptr = FORMAT_H5;
        ImGui::EndCombo();
      }
      ImGui::PopStyleColor(2);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Headings");
      ImGui::SameLine();

      ImGui::SetNextItemWidth(35.0f);
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.20f, 0.20f, 0.25f, 0.60f));
      if (ImGui::BeginCombo("##l", ICON_TEXT_FORMAT_LISTS, ImGuiComboFlags_NoArrowButton)) {
        if (ImGui::Selectable(ICON_TEXT_FORMAT_LIST_BULLETS " Bullet List")) format_ptr = FORMAT_LIST_BULLET;
        if (ImGui::Selectable(ICON_TEXT_FORMAT_LIST_NUMBERS " Numbered List")) format_ptr = FORMAT_LIST_NUMBER;
        ImGui::EndCombo();
      }
      ImGui::PopStyleColor(2);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lists");
      ImGui::SameLine();
    }
    
    auto notes = GetNotes();
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
    ImGui::PopStyleVar();
    
    return format_ptr;
}

int RenderSidebarInternal(NotesWindow* window, float sb_w, float win_h) {
    auto notes = GetNotes();
    int new_selected_idx = -1;
    int note_to_delete = -1;

    int cur_selected_idx = window->m_selected_note_idx;

    ImVec2 min_p = ImGui::GetWindowPos();
    ImVec2 max_p = ImVec2(min_p.x + ImGui::GetWindowSize().x, min_p.y + ImGui::GetWindowSize().y);
    ImU32 col_tl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.063f, 0.071f, 0.086f, window->GetBgAlpha()));
    ImU32 col_tr = ImGui::ColorConvertFloat4ToU32(ImVec4(0.059f, 0.067f, 0.082f, window->GetBgAlpha()));
    ImU32 col_br = ImGui::ColorConvertFloat4ToU32(ImVec4(0.055f, 0.063f, 0.078f, window->GetBgAlpha()));
    ImU32 col_bl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.063f, 0.071f, 0.086f, window->GetBgAlpha()));
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(min_p, max_p, col_tl, col_tr, col_br, col_bl);

    // Inner scrollable child window for the note items
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("NoteList", ImVec2(sb_w, win_h), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::SetCursorPosY(6.0f); // Spacing from top of child window

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 8.0f));

    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
      ImGui::PushID(i);
      bool is_sel = (i == cur_selected_idx);

      int max_chars = static_cast<int>((sb_w - 24.0f) / 7.0f);
      if (max_chars < 8) max_chars = 8;
      
      char title_buf[128];
      const char* src = (is_sel && window->m_view_mode == 0) ? window->m_edit_buffer : notes[i].content.get();
      
      int j = 0;
      while (src[j] == ' ' || src[j] == '\t' || src[j] == '\r' || src[j] == '\n') j++;
      if (src[j] == '#') { while (src[j] == '#' || src[j] == ' ') j++; }
      
      int k = 0;
      while (src[j] && src[j] != '\n' && src[j] != '\r' && k < max_chars - 1 && k < sizeof(title_buf) - 4) { 
          title_buf[k++] = src[j++]; 
      }
      
      if (src[j] && src[j] != '\n' && src[j] != '\r') {
          if (k >= max_chars - 1) {
              if (k > 1) { title_buf[k-2] = '.'; title_buf[k-1] = '.'; }
          }
      }
      title_buf[k] = '\0';
      
      if (k == 0) {
          strncpy_s(title_buf, sizeof(title_buf), "(empty)", _TRUNCATE);
      }

      if (notes[i].is_dirty) {
          memmove(title_buf + 2, title_buf, strlen(title_buf) + 1);
          title_buf[0] = '*'; title_buf[1] = ' ';
      }

      if (is_sel) {
        ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.22f, 0.38f, 0.62f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.44f, 0.68f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.33f, 0.50f, 0.75f, 1.00f));
      }

      ImVec2 pos = ImGui::GetCursorScreenPos();
      
      char id_buf[24];
      snprintf(id_buf, sizeof(id_buf), "##note_%d", i);
      
      ImVec2 item_min = ImVec2(pos.x + 6.0f, pos.y);
      ImVec2 item_max = ImVec2(pos.x + sb_w - 6.0f, pos.y + 32.0f);
      
      ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0, 0, 0, 0));
      
      ImGui::SetCursorPosX(6.0f);
      // Reduce width to avoid capturing clicks intended for the floating sort button on the right
      bool selected_now = ImGui::Selectable(id_buf, is_sel,
                             ImGuiSelectableFlags_None,
                             ImVec2(sb_w - 48.0f, 32.0f));
                            
      bool is_hovered = ImGui::IsItemHovered();
      bool is_active = ImGui::IsItemActive();
      
      ImGui::PopStyleColor(3);

      char popup_id[64];
      snprintf(popup_id, sizeof(popup_id), "##popup_note_%d", i);
      if (ImGui::BeginPopupContextItem(popup_id)) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
          if (ImGui::MenuItem(ICON_DELETE " Delete Note")) {
              note_to_delete = i;
          }
          ImGui::PopStyleColor();
          ImGui::EndPopup();
      }
      
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
          ImGui::GetWindowDrawList()->AddRectFilled(item_min, item_max, ImGui::GetColorU32(highlight_color), 4.0f);
        }
      }
      
      ImGui::PushFont(g_fonts_preview_bold[2]);
      float text_y = pos.y + (32.0f - ImGui::GetFontSize()) * 0.5f;
      ImGui::GetWindowDrawList()->AddText(g_fonts_preview_bold[2], g_fonts_preview_bold[2]->FontSize, ImVec2(pos.x + 16.0f, text_y), ImGui::GetColorU32(ImGuiCol_Text), title_buf);
      ImGui::PopFont();

      if (selected_now && !is_sel) {
        new_selected_idx = i;
      }

      if (is_sel) ImGui::PopStyleColor(3);
      ImGui::PopID();
    }

    ImGui::PopStyleVar(2);

    // Floating circular sort button styled to match other premium UI elements
    float btn_size = 28.0f;
    float scroll_y = ImGui::GetScrollY();
    ImVec2 btn_pos = ImVec2(sb_w - btn_size - 12.0f, 6.0f + scroll_y); // Float top-right of sidebar, adjusted higher
    ImGui::SetCursorPos(btn_pos);

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0, 0, 0, 0));

    bool clicked = ImGui::Button("##floating_sort", ImVec2(btn_size, btn_size));

    ImGui::PopStyleColor(4);

    ImVec2 btn_min = ImGui::GetItemRectMin();
    ImVec2 btn_max = ImGui::GetItemRectMax();
    ImVec2 btn_center = ImVec2(btn_min.x + btn_size * 0.5f, btn_min.y + btn_size * 0.5f);

    bool btn_hovered = ImGui::IsItemHovered();
    bool btn_active = btn_hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);

    ImVec4 bg_color = ImVec4(0.13f, 0.16f, 0.22f, 0.90f);
    ImVec4 border_color = ImVec4(0.20f, 0.24f, 0.32f, 0.80f);

    if (btn_active) {
        bg_color = ImVec4(0.25f, 0.35f, 0.50f, 1.00f);
        border_color = ImVec4(0.35f, 0.48f, 0.68f, 1.00f);
    } else if (btn_hovered) {
        bg_color = ImVec4(0.18f, 0.22f, 0.30f, 0.95f);
        border_color = ImVec4(0.26f, 0.32f, 0.42f, 0.90f);
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddCircleFilled(btn_center, btn_size * 0.5f, ImGui::ColorConvertFloat4ToU32(bg_color), 32);
    draw_list->AddCircle(btn_center, btn_size * 0.5f, ImGui::ColorConvertFloat4ToU32(border_color), 32, 1.0f);

    // Draw Sort Icon with custom visual centering offset (+1.0f x, -1.0f y)
    ImGui::PushFont(g_font_gui);
    ImVec2 icon_size = ImGui::CalcTextSize(ICON_SORT);
    ImVec2 icon_pos = ImVec2(btn_center.x - icon_size.x * 0.5f + 1.0f, btn_center.y - icon_size.y * 0.5f - 1.0f);
    draw_list->AddText(icon_pos, ImGui::ColorConvertFloat4ToU32(ImVec4(0.90f, 0.92f, 0.96f, 1.00f)), ICON_SORT);
    ImGui::PopFont();

    if (btn_hovered) {
        ImGui::SetTooltip("Sort Notes");
    }

    if (clicked) {
        ImGui::OpenPopup("SortMenuPopup");
    }

    if (ImGui::BeginPopup("SortMenuPopup")) {
        auto criteria = GetSortCriteria();
        bool asc = IsSortAscending();
        
        auto sort_item = [&](const char* label, NoteSortCriteria c, bool default_asc) {
            bool selected = (criteria == c);
            
            // Pass spaces as a shortcut placeholder to reserve width on the right column
            if (ImGui::MenuItem(label, "    ")) {
                if (selected) {
                    SetSortMode(c, !asc);
                } else {
                    SetSortMode(c, default_asc);
                }
            }
            
            if (selected) {
                const char* icon = asc ? ICON_SORT_ASCENDING : ICON_SORT_DESCENDING;
                ImVec2 item_min = ImGui::GetItemRectMin();
                ImVec2 item_max = ImGui::GetItemRectMax();
                float item_h = item_max.y - item_min.y;
                
                ImGui::PushFont(g_font_gui);
                ImVec2 icon_size = ImGui::CalcTextSize(icon);
                // Perfect far-right alignment with 12px padding
                ImVec2 icon_pos = ImVec2(item_max.x - icon_size.x - 12.0f, item_min.y + (item_h - icon_size.y) * 0.5f);
                ImGui::GetWindowDrawList()->AddText(icon_pos, ImGui::ColorConvertFloat4ToU32(ImVec4(0.90f, 0.92f, 0.96f, 0.85f)), icon);
                ImGui::PopFont();
            }
        };
        
        sort_item("Name", NoteSortCriteria::Name, true);
        sort_item("Date Created", NoteSortCriteria::DateCreated, false);
        sort_item("Date Modified", NoteSortCriteria::DateModified, false);
        
        ImGui::EndPopup();
    }

    ImGui::EndChild(); // End NoteList child window context

    if (note_to_delete != -1) {
        DeleteNote(note_to_delete);
        // FixupIndicesAfterMutation is automatically called by DeleteNote()
        
        // Force SelectNote to be called to sync state and buffers
        new_selected_idx = window->m_selected_note_idx;
        window->m_selected_note_idx = -1;
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && window->m_selected_note_idx > 0) {
            new_selected_idx = window->m_selected_note_idx - 1;
        } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && window->m_selected_note_idx < static_cast<int>(notes.size()) - 1) {
            new_selected_idx = window->m_selected_note_idx + 1;
        }
    }

    return new_selected_idx;
}

void RenderEditorInternal(NotesWindow* window, float content_h, float avail_w) {
    window->m_editor_wrap_width = avail_w - 48.0f;
    if (window->m_editor_wrap_width < 100.0f) window->m_editor_wrap_width = 100.0f;

    static float s_last_wrap_width = 0.0f;
    if (window->m_editor_wrap_width != s_last_wrap_width) {
        WrapGlobalBuffer(window->m_edit_buffer, sizeof(window->m_edit_buffer), window->m_editor_wrap_width, g_fonts_editor[window->m_zoom_idx]);
        s_last_wrap_width = window->m_editor_wrap_width;
    }

    ImGui::PushFont(g_fonts_editor[window->m_zoom_idx]);

    if (window->m_force_focus_frames > 0) {
        ImGuiID editor_id = ImGui::GetID("##ed");
        ImGui::FocusWindow(ImGui::GetCurrentWindow());
        ImGui::SetActiveID(editor_id, ImGui::GetCurrentWindow());
        ImGui::SetKeyboardFocusHere(0);
        window->m_force_focus_frames--;
    }

    ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways;
    SetFormatterContext(window->m_editor_wrap_width, g_fonts_editor[window->m_zoom_idx]);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::Indent(24.0f);
    bool changed = ImGui::InputTextMultiline(
        "##ed", window->m_edit_buffer, sizeof(window->m_edit_buffer),
        ImVec2(-6.0f, content_h),
        input_flags,
        FormatCallback);
        
    ImGui::PopFont();
        
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 8.0f));
    bool open_ctx = ImGui::BeginPopupContextItem("##editor_ctx");
    ImGui::PopStyleVar();
    
    if (open_ctx) {
        ImGui::PushFont(g_font_gui);
        
        int sel_start = GetFormatterState().saved_selection_start;
        int sel_end = GetFormatterState().saved_selection_end;
        if (sel_start > sel_end) std::swap(sel_start, sel_end);
        size_t len = strlen(window->m_edit_buffer);

        if (ImGui::MenuItem(ICON_COPY " Copy", "Ctrl+C")) {
            GetFormatterState().pending_format = FORMAT_COPY;
            GetFormatterState().focus_editor_restore_frames = 1;
            window->m_force_focus_frames = 1;
        }
        if (ImGui::MenuItem(ICON_CUT " Cut", "Ctrl+X")) {
            GetFormatterState().pending_format = FORMAT_CUT;
            GetFormatterState().focus_editor_restore_frames = 1;
            window->m_force_focus_frames = 1;
        }
        if (ImGui::MenuItem(ICON_PASTE " Paste", "Ctrl+V")) {
            GetFormatterState().pending_format = FORMAT_PASTE;
            GetFormatterState().focus_editor_restore_frames = 1;
            window->m_force_focus_frames = 1;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_SQUARE " Select All", "Ctrl+A")) {
            GetFormatterState().saved_selection_start = 0;
            GetFormatterState().saved_selection_end = static_cast<int>(len);
            GetFormatterState().saved_cursor_pos = static_cast<int>(len);
            GetFormatterState().focus_editor_restore_frames = 1;
            GetFormatterState().text_was_formatted_this_frame = true;
        }
        
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    
    ImGui::Unindent(24.0f);
    ImGui::PopStyleColor();

    if (GetFormatterState().focus_editor_restore_frames > 0) {
        window->m_force_focus_frames = GetFormatterState().focus_editor_restore_frames;
    }
    if (GetFormatterState().text_was_formatted_this_frame) {
        window->m_force_focus_frames = 1;
    }

    if (changed) {
        auto notes = GetNotes();
        int sel_idx = window->m_selected_note_idx;
        if (sel_idx < 0 || static_cast<size_t>(sel_idx) >= notes.size()) return;
        char* dest = notes[sel_idx].content.get();
        const char* src = window->m_edit_buffer;
        
        bool is_different = false;
        int d_idx = 0;
        
        for (int s_idx = 0; src[s_idx] != '\0'; ) {
            if (d_idx >= MAX_NOTE_SIZE - 1) break;
            
            if (src[s_idx] == '\r' && src[s_idx+1] == '\n') {
                if (dest[d_idx] != ' ') is_different = true;
                dest[d_idx++] = ' ';
                s_idx += 2;
            } else if (src[s_idx] == '\r' && src[s_idx+1] == '-' && src[s_idx+2] == '\n') {
                s_idx += 3;
            } else {
                if (dest[d_idx] != src[s_idx]) is_different = true;
                dest[d_idx++] = src[s_idx];
                s_idx++;
            }
        }
        
        if (dest[d_idx] != '\0') is_different = true;
        dest[d_idx] = '\0';

        if (is_different) {
          notes[sel_idx].is_dirty = true;
          MarkNoteChanged();
        }
    }
}

void RenderPreviewInternal(NotesWindow* window, float /*content_h*/) {
  auto notes = GetNotes();
  int sel_idx = window->m_selected_note_idx;
  if (sel_idx < 0 || static_cast<size_t>(sel_idx) >= notes.size()) return;
  const auto& content = notes[sel_idx].content;
  if (content && content[0] != '\0') {
    RenderMarkdown(content.get(), window->m_zoom_idx);
  }
}

FloatBtnAction RenderFloatingButtonsInternal(NotesWindow* window) {
    FloatBtnAction action = FloatBtnAction::None;
    ImVec2 content_pos = ImGui::GetWindowPos();
    ImVec2 content_size = ImGui::GetWindowSize();
    ImVec2 float_btn_pos = ImVec2(content_pos.x + content_size.x - 80.0f, content_pos.y + 4.0f);
    ImVec2 delete_btn_pos = ImVec2(content_pos.x + content_size.x - 45.0f, content_pos.y + 4.0f);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // 1. Edit/Read Toggle Button
    {
        ImGui::SetCursorScreenPos(float_btn_pos);
        bool clicked_toggle = ImGui::InvisibleButton("##edit_toggle_btn", ImVec2(30.0f, 30.0f));
        bool hovered_toggle = ImGui::IsItemHovered();
        bool active_toggle = ImGui::IsItemActive();

        ImVec2 min_p = float_btn_pos;
        ImVec2 max_p = ImVec2(float_btn_pos.x + 30.0f, float_btn_pos.y + 30.0f);
        ImVec2 center = ImVec2(float_btn_pos.x + 15.0f, float_btn_pos.y + 15.0f);

        ImVec4 bg_color = ImVec4(0.118f, 0.478f, 0.812f, 0.90f);
        ImVec4 border_color = ImVec4(0.200f, 0.569f, 0.902f, 0.35f);

        if (active_toggle) {
            bg_color = ImVec4(0.000f, 0.384f, 0.722f, 1.00f);
            border_color = ImVec4(0.100f, 0.486f, 0.847f, 0.50f);
        } else if (hovered_toggle) {
            bg_color = ImVec4(0.200f, 0.639f, 1.000f, 0.98f);
            border_color = ImVec4(0.360f, 0.710f, 1.000f, 0.45f);
        }

        ImU32 bg_col32 = ImGui::ColorConvertFloat4ToU32(bg_color);
        ImU32 border_col32 = ImGui::ColorConvertFloat4ToU32(border_color);
        ImU32 text_col32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.960f, 0.965f, 0.973f, 1.00f));

        draw_list->AddRectFilled(min_p, max_p, bg_col32, 2.0f);

        ImVec2 mid_p = ImVec2(max_p.x, min_p.y + 15.0f);
        ImU32 half_hl_col = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.03f));
        draw_list->AddRectFilled(min_p, mid_p, half_hl_col, 2.0f, ImDrawFlags_RoundCornersTop);
        draw_list->AddRect(min_p, max_p, border_col32, 2.0f, 0, 1.0f);

        const char* icon = (window->m_view_mode == 0) ? ICON_TOGGLE_READ : ICON_TOGGLE_EDIT;

        ImGui::PushFont(g_font_gui);
        ImVec2 text_size = ImGui::CalcTextSize(icon);
        ImVec2 text_pos = ImVec2(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f);
        draw_list->AddText(text_pos, text_col32, icon);
        ImGui::PopFont();

        if (hovered_toggle) {
            ImGui::SetTooltip(window->m_view_mode == 0 ? "Switch to Preview Mode" : "Switch to Editor Mode");
        }

        if (clicked_toggle) {
            action = FloatBtnAction::ToggleMode;
        }
    }

    // 2. Delete Note Button
    {
        ImGui::SetCursorScreenPos(delete_btn_pos);
        bool clicked_del = ImGui::InvisibleButton("##delete_note_btn", ImVec2(30.0f, 30.0f));
        bool hovered_del = ImGui::IsItemHovered();
        bool active_del = ImGui::IsItemActive();

        ImVec2 min_p = delete_btn_pos;
        ImVec2 max_p = ImVec2(delete_btn_pos.x + 30.0f, delete_btn_pos.y + 30.0f);
        ImVec2 center = ImVec2(delete_btn_pos.x + 15.0f, delete_btn_pos.y + 15.0f);

        ImVec4 bg_color = ImVec4(0.851f, 0.220f, 0.220f, 0.90f);
        ImVec4 border_color = ImVec4(0.910f, 0.318f, 0.318f, 0.35f);

        if (active_del) {
            bg_color = ImVec4(0.722f, 0.114f, 0.114f, 1.00f);
            border_color = ImVec4(0.800f, 0.169f, 0.169f, 0.50f);
        } else if (hovered_del) {
            bg_color = ImVec4(1.000f, 0.322f, 0.322f, 0.98f);
            border_color = ImVec4(1.000f, 0.471f, 0.471f, 0.45f);
        }

        ImU32 bg_col32 = ImGui::ColorConvertFloat4ToU32(bg_color);
        ImU32 border_col32 = ImGui::ColorConvertFloat4ToU32(border_color);
        ImU32 text_col32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.960f, 0.965f, 0.973f, 1.00f));

        draw_list->AddRectFilled(min_p, max_p, bg_col32, 2.0f);

        ImVec2 mid_p = ImVec2(max_p.x, min_p.y + 15.0f);
        ImU32 half_hl_col = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.03f));
        draw_list->AddRectFilled(min_p, mid_p, half_hl_col, 2.0f, ImDrawFlags_RoundCornersTop);
        draw_list->AddRect(min_p, max_p, border_col32, 2.0f, 0, 1.0f);

        ImGui::PushFont(g_font_gui);
        ImVec2 text_size = ImGui::CalcTextSize(ICON_DELETE);
        ImVec2 text_pos = ImVec2(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f);
        draw_list->AddText(text_pos, text_col32, ICON_DELETE);
        ImGui::PopFont();

        if (hovered_del) {
            ImGui::SetTooltip("Delete Note");
        }

        if (clicked_del) {
            action = FloatBtnAction::DeleteNote;
        }
    }

    return action;
}

} // namespace detail

// ==============================================================================
// ORCHESTRATORS
// ==============================================================================

void NotesWindow::RenderToolbar(bool interactive) {
    if (!interactive) return;

    float win_w = ImGui::GetWindowWidth();
    PendingFormat format_ptr = detail::RenderToolbarInternal(this, interactive, win_w);
    
    if (format_ptr != FORMAT_NONE) {
        GetFormatterState().pending_format = format_ptr;
        m_force_focus_frames = 1;
    }
}

void NotesWindow::RenderContent(bool interactive) {
  if (!interactive) return;

  // Global shortcuts (Alt+V for toggle, Ctrl+N for new note)
  static uint32_t s_prev_win_keys = 0;
  uint32_t curr_win_keys = 0;
  if (ImGui::IsKeyDown(ImGuiKey_V)) curr_win_keys |= (1 << 0);
  if (ImGui::IsKeyDown(ImGuiKey_N)) curr_win_keys |= (1 << 1);
  uint32_t win_pressed = curr_win_keys & ~s_prev_win_keys;
  s_prev_win_keys = curr_win_keys;

  bool trigger_toggle = ImGui::GetIO().KeyAlt && (win_pressed & (1 << 0));
  bool trigger_new_note = ImGui::GetIO().KeyCtrl && (win_pressed & (1 << 1));

  if (trigger_new_note) {
    const char* new_title = CreateAutoNote();
    if (new_title[0] != '\0') {
      auto ns = GetNotes();
      for (int i = 0; i < static_cast<int>(ns.size()); ++i) {
        if (strcmp(ns[i].title, new_title) == 0) { SelectNote(i); break; }
      }
      SwitchToEditor();
    }
  }

  auto notes = GetNotes();
  if (!notes.empty() && m_selected_note_idx >= static_cast<int>(notes.size())) {
    m_selected_note_idx = static_cast<int>(notes.size()) - 1;
  }
  if (m_synced_note_idx != m_selected_note_idx) {
    SyncEditBufferFromNote(m_selected_note_idx);
  }

  TickAutosave();

  const float win_w    = ImGui::GetContentRegionAvail().x;
  const float win_h    = ImGui::GetContentRegionAvail().y;
  const float sb_w     = m_sidebar_visible ? m_sidebar_width : 0.0f;
  const float split_w  = m_sidebar_visible ? 5.0f   : 0.0f;
  const float cont_w   = win_w - sb_w - split_w;

  if (m_sidebar_visible) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("Sidebar", ImVec2(sb_w, win_h), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    int new_idx = detail::RenderSidebarInternal(this, sb_w, win_h);
    
    ImGui::EndChild();

    if (new_idx != -1) {
      SelectNote(new_idx);
    }

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::Button("##spl", ImVec2(split_w, win_h));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemActive()) {
      m_sidebar_width += ImGui::GetIO().MouseDelta.x;
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
  
  ImVec2 padding = (m_view_mode == 0) ? ImVec2(0.0f, 24.0f) : ImVec2(24.0f, 24.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
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
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
    return;
  }

  float content_h = ImGui::GetContentRegionAvail().y;
  float avail_w = ImGui::GetContentRegionAvail().x;
  
  if (m_view_mode == 0) {
    detail::RenderEditorInternal(this, content_h, avail_w);
  } else {
    detail::RenderPreviewInternal(this, content_h);
  }

  detail::FloatBtnAction btn_action = detail::RenderFloatingButtonsInternal(this);

  if (trigger_toggle) {
    btn_action = detail::FloatBtnAction::ToggleMode;
  }
  if (btn_action == detail::FloatBtnAction::ToggleMode) {
    if (m_view_mode == 0) {
      FlushEditBufferToNote();
      m_view_mode = 1;
    } else {
      SwitchToEditor();
    }
  } else if (btn_action == detail::FloatBtnAction::DeleteNote) {
    if (!notes.empty()) {
      DeleteNote(m_selected_note_idx);
      
      // MENCEGAH BUFFER OVERWRITE: 
      // Putuskan sinkronisasi buffer segera setelah delete agar FlushEditBufferToNote 
      // tidak menimpa note lain yang bergeser naik ke index ini.
      m_synced_note_idx = -1;
      
      notes = GetNotes();
      if (m_selected_note_idx > 0) {
        m_selected_note_idx--;
      }
      if (!notes.empty()) {
        SelectNote(m_selected_note_idx);
      } else {
        m_edit_buffer[0] = '\0';
        m_synced_note_idx = -1;
      }
    }
  }

  ImGui::EndChild(); // End NoteContent
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor(5);
}

void NotesWindow::PostRender(bool /*interactive*/) {
    // Buttons are rendered inside NoteContent child window context instead
}

} // namespace dover::overlay::notes
