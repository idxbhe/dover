#pragma once

#include <string>
#include <atomic>
#include <imgui.h>

namespace dover::shared::ui {

enum class RenderContext { Overlay, Launcher };

// Feature flags for window decorations (Lego blocks)
enum class WindowFeature {
    None       = 0,
    Pin        = 1 << 0,
    Maximize   = 1 << 1,
    Close      = 1 << 2,
    Fullscreen = 1 << 3,
    Default    = Pin | Maximize | Fullscreen | Close,
    NoPin      = Maximize | Fullscreen | Close
};

inline WindowFeature operator|(WindowFeature a, WindowFeature b) {
    return static_cast<WindowFeature>(static_cast<int>(a) | static_cast<int>(b));
}

inline bool HasFeature(WindowFeature flags, WindowFeature feature) {
    return (static_cast<int>(flags) & static_cast<int>(feature)) != 0;
}

class BaseWindow {
protected:
    RenderContext m_ctx;
    char m_window_name[64];
    WindowFeature m_features;
    
    // Core Window States
    std::atomic<bool> m_is_open{false};
    std::atomic<bool> m_is_pinned{false};
    std::atomic<bool> m_is_maximized{false};
    bool m_was_maximized = false;
    std::atomic<bool> m_is_fullscreen{false};
    bool m_was_fullscreen = false;
    bool m_is_focused = false;
    
    ImVec2 m_prev_pos{0.0f, 0.0f};
    ImVec2 m_prev_size{0.0f, 0.0f};
    ImVec2 m_default_size{800.0f, 500.0f};
    ImVec2 m_min_size{250.0f, 250.0f};

    // Appearance State
    float m_bg_alpha = 0.95f;

public:
    BaseWindow(RenderContext ctx, const char* name, WindowFeature features = WindowFeature::Default, 
               ImVec2 default_size = ImVec2(800.0f, 500.0f), ImVec2 min_size = ImVec2(250.0f, 250.0f))
        : m_ctx(ctx), m_features(features), m_default_size(default_size), m_min_size(min_size) {
        strncpy_s(m_window_name, sizeof(m_window_name), name, _TRUNCATE);
    }
    
    virtual ~BaseWindow() = default;

    // Call this every frame inside RenderImGuiUI
    void Render(bool interactive);

    void Open();
    void Close();
    void ToggleOpen();
    void SetOpenDirect(bool open) { m_is_open = open; }
    void SetRenderContext(RenderContext ctx) { m_ctx = ctx; }
    
    bool IsOpen() const { return m_is_open; }

    
    bool IsPinned() const { return m_is_pinned; }
    void SetPinned(bool p) { m_is_pinned = p; }
    
    bool IsMaximized() const { return m_is_maximized; }
    void SetMaximized(bool m) { 
        m_is_maximized = m; 
        if (m) m_was_maximized = true;
    }
    
    bool IsFullscreen() const { return m_is_fullscreen; }
    void SetFullscreen(bool f) { 
        m_is_fullscreen = f; 
        if (f) m_was_fullscreen = true;
    }
    
    ImVec2 GetPrevPos() const { return m_prev_pos; }
    void SetPrevPos(ImVec2 pos) { m_prev_pos = pos; }
    
    ImVec2 GetPrevSize() const { return m_prev_size; }
    void SetPrevSize(ImVec2 sz) { m_prev_size = sz; }
    
    bool IsFocused() const { return m_is_focused; }
    
    float GetBgAlpha() const { return m_bg_alpha; }
    void SetBgAlpha(float alpha) { m_bg_alpha = alpha; }

protected:
    // Core template methods to be overridden by child classes
    
    // Rendered before toolbar, typically for inner window background overlays
    virtual void PreRender(bool /*interactive*/) {}
    
    // Render custom toolbar items (if interactive is true)
    virtual void RenderToolbar(bool /*interactive*/) {}
    
    // Render the main content (required)
    virtual void RenderContent(bool interactive) = 0;
    
    // Render after content (floating buttons, popups)
    virtual void PostRender(bool /*interactive*/) {}

    // Internal rendering logic made available to child window classes
    void RenderWindowDecorations(bool interactive, float right_boundary, float custom_y_pos = -1.0f);
};

} // namespace dover::shared::ui
