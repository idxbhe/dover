#include "overlay/notes_ui.h"
#include "overlay/notes_manager.h"

#include <imgui.h>
#include <imgui_md.h>
#include <windows.h>

#include <string>
#include <vector>

namespace dover::overlay {

namespace {

// --- UI State ---
static int g_selected_note_idx = 0;
static bool g_sidebar_visible = true;   // Toggle-hidden sidebar
static bool g_sidebar_collapsed = false; // Collapse sections within sidebar

// 0 = Editor, 1 = Preview  (persisted, last used mode)
static int g_view_mode = 0;

// Scratch buffer for inline editing (synced to NoteFile::content on change)
static char g_edit_buffer[65536] = {};
static int g_synced_note_idx = -1; // Which note index is currently loaded in buffer

// For "new note" dialog
static char g_new_note_title[128] = {};
static bool g_show_new_note_dialog = false;

// For "delete confirm" dialog
static int g_delete_confirm_idx = -1;

// imgui_md renderer instance (one per context lifetime)
struct DoverMarkdownRenderer : public imgui_md {
  ImFont* get_font() const override { return nullptr; }
  void open_url() const override {}
  bool get_image(image_info&) const override { return false; }
};
static DoverMarkdownRenderer g_md_renderer;

// ---- Helpers ----
void SyncEditBufferFromNote(int idx) {
  auto& notes = GetNotes();
  if (idx < 0 || static_cast<size_t>(idx) >= notes.size()) return;
  strncpy_s(g_edit_buffer, sizeof(g_edit_buffer), notes[idx].content.c_str(), _TRUNCATE);
  g_synced_note_idx = idx;
}

void FlushEditBufferToNote() {
  auto& notes = GetNotes();
  if (g_synced_note_idx < 0 || static_cast<size_t>(g_synced_note_idx) >= notes.size()) return;
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
}

} // namespace

void InitializeNotesUI() {
  g_selected_note_idx = 0;
  g_view_mode = 0;
  g_sidebar_visible = true;
  g_sidebar_collapsed = false;
  SyncEditBufferFromNote(0);
}

void ShutdownNotesUI() {
  FlushEditBufferToNote();
}

void RenderNotesWindow(bool* p_open) {
  auto& notes = GetNotes();

  // Ensure selected index is valid
  if (!notes.empty() && g_selected_note_idx >= static_cast<int>(notes.size())) {
    g_selected_note_idx = static_cast<int>(notes.size()) - 1;
  }
  if (g_synced_note_idx != g_selected_note_idx) {
    SyncEditBufferFromNote(g_selected_note_idx);
  }

  // Trigger autosave tick
  TickAutosave();

  // --- Notes Window ---
  ImGui::SetNextWindowSize(ImVec2(700.0f, 450.0f), ImGuiCond_FirstUseEver);
  ImGuiWindowFlags win_flags = ImGuiWindowFlags_NoCollapse;
  if (!ImGui::Begin("Notes", p_open, win_flags)) {
    ImGui::End();
    return;
  }

  // ---- Toolbar row (top of window) ----
  ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.00f, 1.00f), "Game: %s", GetNotesGameName().c_str());
  ImGui::SameLine();

  // Toggle sidebar button
  const char* sidebar_btn_label = g_sidebar_visible ? "Hide Sidebar" : "Show Sidebar";
  if (ImGui::SmallButton(sidebar_btn_label)) {
    g_sidebar_visible = !g_sidebar_visible;
  }
  ImGui::SameLine();

  if (ImGui::SmallButton("+ New Note")) {
    g_show_new_note_dialog = true;
    g_new_note_title[0] = '\0';
  }
  ImGui::SameLine();

  bool any_dirty = false;
  for (const auto& n : notes) { if (n.is_dirty) { any_dirty = true; break; } }
  if (any_dirty) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.35f, 0.15f, 0.85f));
    if (ImGui::SmallButton("Save All*")) { AutoSaveAll(); }
    ImGui::PopStyleColor();
  } else {
    if (ImGui::SmallButton("Save All")) { AutoSaveAll(); }
  }

  ImGui::Separator();

  // --- New Note Dialog ---
  if (g_show_new_note_dialog) {
    ImGui::SetNextWindowSize(ImVec2(300.0f, 110.0f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("New Note", &g_show_new_note_dialog,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove)) {
      ImGui::Text("Note title:");
      ImGui::SetNextItemWidth(-FLT_MIN);
      bool enter_pressed = ImGui::InputText("##new_title", g_new_note_title, sizeof(g_new_note_title),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
      if (ImGui::Button("Create", ImVec2(120, 0)) || enter_pressed) {
        if (g_new_note_title[0] != '\0') {
          CreateNote(g_new_note_title);
          // Select the newly created note
          for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
            if (notes[i].title == g_new_note_title) { SelectNote(i); break; }
          }
          g_show_new_note_dialog = false;
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(120, 0))) { g_show_new_note_dialog = false; }
    }
    ImGui::End();
  }

  // --- Delete Confirm Dialog ---
  if (g_delete_confirm_idx >= 0 && g_delete_confirm_idx < static_cast<int>(notes.size())) {
    ImGui::SetNextWindowSize(ImVec2(300.0f, 100.0f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("Confirm Delete", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove)) {
      ImGui::Text("Delete \"%s\"?", notes[g_delete_confirm_idx].title.c_str());
      ImGui::Spacing();
      if (ImGui::Button("Delete", ImVec2(120, 0))) {
        DeleteNote(static_cast<size_t>(g_delete_confirm_idx));
        if (g_selected_note_idx >= static_cast<int>(notes.size()))
          g_selected_note_idx = static_cast<int>(notes.size()) - 1;
        SyncEditBufferFromNote(g_selected_note_idx);
        g_delete_confirm_idx = -1;
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(120, 0))) { g_delete_confirm_idx = -1; }
    }
    ImGui::End();
  }

  // --- Main layout: sidebar + content panel ---
  float window_width = ImGui::GetContentRegionAvail().x;
  float window_height = ImGui::GetContentRegionAvail().y;
  const float sidebar_width = g_sidebar_visible ? 180.0f : 0.0f;
  const float splitter_width = g_sidebar_visible ? 6.0f : 0.0f;
  const float content_width = window_width - sidebar_width - splitter_width;

  // --- SIDEBAR ---
  if (g_sidebar_visible) {
    ImGui::BeginChild("Sidebar", ImVec2(sidebar_width, window_height), false,
                      ImGuiWindowFlags_NoScrollbar);

    // Collapsible game section header
    ImGuiTreeNodeFlags tree_flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth;
    if (g_sidebar_collapsed) tree_flags &= ~ImGuiTreeNodeFlags_DefaultOpen;

    std::string header_label = "NOTES (" + GetNotesGameName() + ")";
    bool section_open = ImGui::CollapsingHeader(header_label.c_str(), tree_flags);
    if (!section_open) {
      g_sidebar_collapsed = true;
    } else {
      g_sidebar_collapsed = false;
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 3.0f));

      for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
        const auto& note = notes[i];
        bool is_selected = (i == g_selected_note_idx);

        // Unsaved indicator
        std::string label = (note.is_dirty ? "* " : "  ") + note.title;

        if (is_selected) {
          ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.40f, 0.65f, 0.85f));
          ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.30f, 0.45f, 0.70f, 0.90f));
        }

        bool clicked = ImGui::Selectable(label.c_str(), is_selected,
                                         ImGuiSelectableFlags_None,
                                         ImVec2(sidebar_width - 32.0f, 0));
        if (clicked && !is_selected) { SelectNote(i); }

        if (is_selected) {
          ImGui::PopStyleColor(2);
        }

        // Delete button on the right of each item
        ImGui::SameLine(sidebar_width - 28.0f);
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.15f, 0.15f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
        if (ImGui::SmallButton("x")) {
          g_delete_confirm_idx = i;
        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();
      }
      ImGui::PopStyleVar();
    }

    ImGui::EndChild();

    // Vertical splitter
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.22f, 1.0f));
    ImGui::Button("##splitter", ImVec2(splitter_width, window_height));
    ImGui::PopStyleColor();
    ImGui::SameLine();
  }

  // --- CONTENT PANEL ---
  ImGui::BeginChild("NoteContent", ImVec2(content_width, window_height), false);

  if (notes.empty()) {
    ImGui::TextDisabled("No notes yet. Click \"+ New Note\" to get started.");
    ImGui::EndChild();
    ImGui::End();
    return;
  }

  // View mode tabs
  ImGui::SetNextItemWidth(200.0f);
  if (ImGui::RadioButton("Editor", g_view_mode == 0)) { g_view_mode = 0; }
  ImGui::SameLine();
  if (ImGui::RadioButton("Preview", g_view_mode == 1)) {
    FlushEditBufferToNote();
    g_view_mode = 1;
  }
  ImGui::SameLine();

  // Save current note button
  auto& cur = notes[g_selected_note_idx];
  if (cur.is_dirty) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.35f, 0.10f, 0.85f));
    if (ImGui::SmallButton("Save*")) { SaveNote(static_cast<size_t>(g_selected_note_idx)); }
    ImGui::PopStyleColor();
  } else {
    ImGui::SmallButton("Saved");
  }
  ImGui::Separator();

  float content_h = ImGui::GetContentRegionAvail().y;

  if (g_view_mode == 0) {
    // --- Editor Mode ---
    ImGuiInputTextFlags text_flags = ImGuiInputTextFlags_AllowTabInput;
    bool changed = ImGui::InputTextMultiline(
        "##editor",
        g_edit_buffer,
        sizeof(g_edit_buffer),
        ImVec2(-FLT_MIN, content_h),
        text_flags);
    if (changed) {
      // Lazily mark dirty; actual sync deferred to FlushEditBufferToNote
      notes[g_selected_note_idx].is_dirty = true;
    }
  } else {
    // --- Preview Mode (Lazy Markdown Render) ---
    FlushEditBufferToNote();
    ImGui::BeginChild("MarkdownPreview", ImVec2(-FLT_MIN, content_h), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const auto& content = notes[g_selected_note_idx].content;
    if (!content.empty()) {
      g_md_renderer.print(content.c_str(), content.c_str() + content.size());
    } else {
      ImGui::TextDisabled("(empty note)");
    }
    ImGui::EndChild();
  }

  ImGui::EndChild();
  ImGui::End();
}

} // namespace dover::overlay
