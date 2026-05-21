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

namespace {

using Clock = std::chrono::steady_clock;

// --- UI State ---
static int g_selected_note_idx = 0;
static bool g_sidebar_visible = true;

// 0 = editor, 1 = preview — managed automatically
static int g_view_mode = 1;
static Clock::time_point g_last_edit_time{};
static bool g_edit_timer_active = false;
constexpr long long kAutoPreviewIdleMs = 1500; // auto-switch to preview after 1.5s idle

// Scratch buffer for editing
static char g_edit_buffer[65536] = {};
static int g_synced_note_idx = -1;

// New note dialog
static char g_new_note_title[128] = {};
static bool g_show_new_note_dialog = false;

// Delete confirm
static int g_delete_confirm_idx = -1;

// imgui_md renderer
struct DoverMarkdownRenderer : public imgui_md {
  ImFont* get_font() const override { return nullptr; }
  void open_url() const override {}
  bool get_image(image_info&) const override { return false; }
};
static DoverMarkdownRenderer g_md_renderer;

// ---- Helpers ----

// Derive a display title from note content (first non-empty line, strip leading #)
std::string ExtractTitleFromContent(const std::string& content) {
  if (content.empty()) return "(empty)";
  std::istringstream ss(content);
  std::string line;
  while (std::getline(ss, line)) {
    size_t start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) continue;
    line = line.substr(start);
    if (line.empty()) continue;
    // Strip Markdown heading markers: ##, ###, etc.
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
  // Start in preview when switching notes
  g_view_mode = 1;
  g_edit_timer_active = false;
}

void SwitchToEditor() {
  g_view_mode = 0;
  g_edit_timer_active = false; // don't immediately time out
}

} // namespace

void InitializeNotesUI() {
  g_selected_note_idx = 0;
  g_view_mode = 1;
  g_sidebar_visible = true;
  g_edit_timer_active = false;
  SyncEditBufferFromNote(0);
}

void ShutdownNotesUI() {
  FlushEditBufferToNote();
}

void RenderNotesWindow(bool* p_open) {
  auto& notes = GetNotes();

  // Clamp selected index
  if (!notes.empty() && g_selected_note_idx >= static_cast<int>(notes.size())) {
    g_selected_note_idx = static_cast<int>(notes.size()) - 1;
  }
  if (g_synced_note_idx != g_selected_note_idx) {
    SyncEditBufferFromNote(g_selected_note_idx);
  }

  TickAutosave();

  // Auto-switch to preview when editing has been idle for kAutoPreviewIdleMs
  if (g_view_mode == 0 && g_edit_timer_active) {
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - g_last_edit_time).count();
    if (elapsed_ms >= kAutoPreviewIdleMs) {
      FlushEditBufferToNote();
      g_view_mode = 1;
      g_edit_timer_active = false;
    }
  }

  ImGui::SetNextWindowSize(ImVec2(700.0f, 450.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Notes", p_open, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  // ---- Toolbar ----
  const char* sidebar_lbl = g_sidebar_visible ? "< Hide" : "> Show";
  if (ImGui::SmallButton(sidebar_lbl)) {
    g_sidebar_visible = !g_sidebar_visible;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("+ New")) {
    g_show_new_note_dialog = true;
    g_new_note_title[0] = '\0';
  }
  ImGui::SameLine();

  // Steam-style save indicator: Saving... / Saved
  bool any_dirty = false;
  for (const auto& n : notes) { if (n.is_dirty) { any_dirty = true; break; } }
  if (any_dirty) {
    ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.20f, 1.00f), "Saving...");
  } else {
    ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.40f, 1.00f), "Saved");
  }

  ImGui::Separator();

  // --- New Note Dialog ---
  if (g_show_new_note_dialog) {
    ImGui::SetNextWindowSize(ImVec2(300.0f, 110.0f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(
        ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
               ImGui::GetIO().DisplaySize.y * 0.5f),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("New Note", &g_show_new_note_dialog,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove)) {
      ImGui::Text("Note title:");
      ImGui::SetNextItemWidth(-FLT_MIN);
      bool enter = ImGui::InputText("##new_title", g_new_note_title,
                                    sizeof(g_new_note_title),
                                    ImGuiInputTextFlags_EnterReturnsTrue);
      if (ImGui::Button("Create", ImVec2(120, 0)) || enter) {
        if (g_new_note_title[0] != '\0') {
          CreateNote(g_new_note_title);
          for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
            if (notes[i].title == g_new_note_title) { SelectNote(i); break; }
          }
          g_show_new_note_dialog = false;
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(120, 0))) g_show_new_note_dialog = false;
    }
    ImGui::End();
  }

  // --- Delete Confirm Dialog ---
  if (g_delete_confirm_idx >= 0 && g_delete_confirm_idx < static_cast<int>(notes.size())) {
    ImGui::SetNextWindowSize(ImVec2(300.0f, 100.0f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(
        ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
               ImGui::GetIO().DisplaySize.y * 0.5f),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("Confirm Delete", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove)) {
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
      if (ImGui::Button("Cancel", ImVec2(120, 0))) g_delete_confirm_idx = -1;
    }
    ImGui::End();
  }

  // --- Main Layout: Sidebar + Content ---
  const float window_width = ImGui::GetContentRegionAvail().x;
  const float window_height = ImGui::GetContentRegionAvail().y;
  const float sidebar_width = g_sidebar_visible ? 180.0f : 0.0f;
  const float splitter_width = g_sidebar_visible ? 5.0f : 0.0f;
  const float content_width = window_width - sidebar_width - splitter_width;

  // ---- SIDEBAR (plain list, no collapsible header) ----
  if (g_sidebar_visible) {
    ImGui::BeginChild("Sidebar", ImVec2(sidebar_width, window_height), false,
                      ImGuiWindowFlags_NoScrollbar);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 5.0f));

    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
      const auto& note = notes[i];
      bool is_selected = (i == g_selected_note_idx);

      // Auto-title from first content line (like Steam Notes)
      std::string display_title = ExtractTitleFromContent(note.content);
      // Truncate to fit sidebar width
      if (display_title.size() > 20) display_title = display_title.substr(0, 18) + "..";

      if (is_selected) {
        ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.22f, 0.38f, 0.62f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.44f, 0.68f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.33f, 0.50f, 0.75f, 1.00f));
      }

      bool clicked = ImGui::Selectable(display_title.c_str(), is_selected,
                                       ImGuiSelectableFlags_None,
                                       ImVec2(sidebar_width - 30.0f, 0));
      if (clicked && !is_selected) SelectNote(i);

      if (is_selected) ImGui::PopStyleColor(3);

      // Delete button
      ImGui::SameLine(sidebar_width - 26.0f);
      ImGui::PushID(i);
      ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.50f, 0.12f, 0.12f, 0.65f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.18f, 0.18f, 1.00f));
      if (ImGui::SmallButton("x")) g_delete_confirm_idx = i;
      ImGui::PopStyleColor(2);
      ImGui::PopID();
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();

    // Splitter
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.14f, 0.18f, 1.0f));
    ImGui::Button("##splitter", ImVec2(splitter_width, window_height));
    ImGui::PopStyleColor();
    ImGui::SameLine();
  }

  // ---- CONTENT PANEL ----
  ImGui::BeginChild("NoteContent", ImVec2(content_width, window_height), false);

  if (notes.empty()) {
    ImGui::TextDisabled("No notes yet. Click \"+ New\" to get started.");
    ImGui::EndChild();
    ImGui::End();
    return;
  }

  float content_h = ImGui::GetContentRegionAvail().y;

  if (g_view_mode == 0) {
    // ---- EDITOR MODE ----
    long long elapsed_show = g_edit_timer_active
        ? std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_last_edit_time).count()
        : kAutoPreviewIdleMs;
    long long remaining_ms = kAutoPreviewIdleMs - elapsed_show;
    if (remaining_ms < 0) remaining_ms = 0;
    ImGui::TextDisabled("Editing  |  Auto-preview in %.1fs", static_cast<float>(remaining_ms) / 1000.0f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Preview now")) {
      FlushEditBufferToNote();
      g_view_mode = 1;
      g_edit_timer_active = false;
    }
    ImGui::Separator();
    content_h = ImGui::GetContentRegionAvail().y;

    bool changed = ImGui::InputTextMultiline(
        "##editor",
        g_edit_buffer,
        sizeof(g_edit_buffer),
        ImVec2(-FLT_MIN, content_h),
        ImGuiInputTextFlags_AllowTabInput);

    if (changed) {
      notes[g_selected_note_idx].is_dirty = true;
      g_last_edit_time = Clock::now();
      g_edit_timer_active = true;
    }

  } else {
    // ---- PREVIEW MODE ----
    ImGui::TextDisabled("Click to edit");
    ImGui::Separator();
    content_h = ImGui::GetContentRegionAvail().y;

    ImGui::BeginChild("MarkdownPreview", ImVec2(-FLT_MIN, content_h), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    bool clicked_in_preview = ImGui::IsWindowHovered() &&
                               ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    const auto& content = notes[g_selected_note_idx].content;
    if (!content.empty()) {
      g_md_renderer.print(content.c_str(), content.c_str() + content.size());
    } else {
      ImGui::TextDisabled("(empty note — click to start writing)");
    }

    ImGui::EndChild();

    if (clicked_in_preview) {
      SwitchToEditor();
    }
  }

  ImGui::EndChild();
  ImGui::End();
}

} // namespace dover::overlay
