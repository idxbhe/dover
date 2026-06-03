#pragma once

#include <string>
#include <imgui.h>

namespace dover::overlay::ui {

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
    std::string m_window_name;
    WindowFeature m_features;
    
    // Core Window States
    bool m_is_open = false;
    bool m_is_pinned = false;
    bool m_is_maximized = false;
    bool m_was_maximized = false;
    bool m_is_fullscreen = false;
    bool m_was_fullscreen = false;
    bool m_is_focused = false;
    
    ImVec2 m_prev_pos{0.0f, 0.0f};
    ImVec2 m_prev_size{0.0f, 0.0f};
    ImVec2 m_default_size{800.0f, 500.0f};

    // Appearance State
    float m_bg_alpha = 0.95f;

public:
    BaseWindow(const std::string& name, WindowFeature features = WindowFeature::Default, ImVec2 default_size = ImVec2(800.0f, 500.0f))
        : m_window_name(name), m_features(features), m_default_size(default_size) {}
    
    virtual ~BaseWindow() = default;

    // Call this every frame inside RenderImGuiUI
    void Render(bool interactive);

    void Open();
    void Close();
    void ToggleOpen();
    void SetOpenDirect(bool open) { m_is_open = open; }
    
    bool IsOpen() const { return m_is_open; }
    bool* GetOpenPtr() { return &m_is_open; }
    
    bool IsPinned() const { return m_is_pinned; }
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

} // namespace dover::overlay::ui
