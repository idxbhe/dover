#pragma once

#include "overlay/ui/components/base_window.h"

namespace dover::overlay::notes {

class NotesWindow : public ui::BaseWindow {
public:
    NotesWindow() : ui::BaseWindow("Notes", ui::WindowFeature::Default) {
        m_bg_alpha = 0.95f; // Set default Notes opacity
    }

    void Initialize();
    void Shutdown();

    // Replaces FlushNotesEditBuffer
    void FlushEditBuffer();

protected:
    void RenderToolbar(bool interactive) override;
    void RenderContent(bool interactive) override;
    void PostRender(bool interactive) override;

private:
    // Local state previously in anonymous namespace
    float m_sidebar_width = 240.0f;
    bool m_sidebar_visible = true;
    int m_view_mode = 1; // 0=editor, 1=preview
    int m_zoom_idx = 2;
    int m_force_focus_frames = 0;
    
    int m_selected_note_idx = 0;
    int m_synced_note_idx = -1;
    float m_editor_wrap_width = 400.0f;
    
    char m_edit_buffer[65536] = {};

    // Helper methods
    void SyncEditBufferFromNote(int idx);
    void FlushEditBufferToNote();
    void SelectNote(int idx);
    void SwitchToEditor();
};

// Global instance getter
NotesWindow& GetNotesWindow();

} // namespace dover::overlay::notes
