#pragma once

#include "shared/ui/components/base_window.h"
#include "shared/notes/formatter.h"
#include <mutex>
#include <atomic>

namespace dover::shared::notes {
class NotesWindow;
namespace detail {
    enum class FloatBtnAction;
    PendingFormat RenderToolbarInternal(NotesWindow*, bool, float);
    int RenderSidebarInternal(NotesWindow*, float, float);
    void RenderEditorInternal(NotesWindow*, float, float);
    void RenderPreviewInternal(NotesWindow*, float);
    void SanitizeEditBufferToNote(const char* src, char* dest, size_t dest_size, bool& is_different);
    FloatBtnAction RenderFloatingButtonsInternal(NotesWindow*);
}

class NotesWindow : public shared::ui::BaseWindow {
public:
    NotesWindow() : shared::ui::BaseWindow(shared::ui::RenderContext::Overlay, "Notes", shared::ui::WindowFeature::Default) {
        m_bg_alpha = 0.95f; // Set default Notes opacity
    }

    void Initialize();
    void Shutdown();

    // Replaces FlushNotesEditBuffer
    void FlushEditBuffer();

    // Public for GameStorage state persistence
    void SelectNote(int idx, bool save_state = true);
    void SelectNoteByFilename(const char* filename);
    void GetSelectedNoteFilename(char* out_buffer, size_t out_size) const;
    void SetViewMode(int mode) { m_view_mode = mode; }
    int  GetSelectedNoteIndex() const { return m_selected_note_idx; }
    int  GetViewMode() const { return m_view_mode; }
    void SetFontSize(int size) { m_font_size = size; }
    int  GetFontSize() const { return m_font_size; }


    // Sync helper for interactive Read-Mode mutations
    void SyncEditBufferFromNote(int idx);

    // Call this immediately after any mutation to the underlying notes array (sort, create, delete)
    void FixupIndicesAfterMutation();

protected:
    void RenderToolbar(bool interactive) override;
    void RenderContent(bool interactive) override;
    void PostRender(bool interactive) override;

private:
    // Local state previously in anonymous namespace
    float m_sidebar_width = 240.0f;
    bool m_sidebar_visible = true;
    std::atomic<int> m_view_mode{1}; // 0=editor, 1=preview
    std::atomic<int> m_font_size{18};
    int m_force_focus_frames = 0;
    
    std::atomic<int> m_selected_note_idx{0};
    int m_synced_note_idx = -1;
    char m_selected_note_filename[64] = {};
    mutable std::mutex m_selected_note_filename_mutex;
    char m_synced_note_filename[64] = {};
    float m_editor_wrap_width = 400.0f;
    
    char m_edit_buffer[65536] = {};

    // Helper methods
    void FlushEditBufferToNote();
    void SwitchToEditor();

    friend PendingFormat detail::RenderToolbarInternal(NotesWindow*, bool, float);
    friend int detail::RenderSidebarInternal(NotesWindow*, float, float);
    friend void detail::RenderEditorInternal(NotesWindow*, float, float);
    friend void detail::RenderPreviewInternal(NotesWindow*, float);
    friend void detail::SanitizeEditBufferToNote(const char* src, char* dest, size_t dest_size, bool& is_different);
    friend detail::FloatBtnAction detail::RenderFloatingButtonsInternal(NotesWindow*);
};

// Global instance getter
NotesWindow& GetNotesWindow();

} // namespace dover::shared::notes
