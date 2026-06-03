#pragma once

#include <overlay/ui/components/base_window.h>

namespace dover::overlay::input {

struct ButtonRenderData {
    const char* name;
    float x_pct, y_pct, w_pct, h_pct;
    unsigned int xinput_flag;
    void* tex_id;
    bool is_trigger;
};

class InputWindow : public ui::BaseWindow {
public:
    InputWindow();
    ~InputWindow() override = default;

    void Initialize();
    void RenderGamepadOverlay();

protected:
    void RenderContent(bool interactive) override;

private:
    void LoadGamepadTextures();
    void RenderRemapper(bool interactive);
    void RenderVisualizer();
    void InitializeVisualizerButtons();

    int m_active_tab = 0;
    int m_recording_index = -1;
    bool m_textures_loaded = false;
    
    ButtonRenderData m_visualizer_buttons[20];
    int m_visualizer_button_count = 0;
};

// Global instance getter
InputWindow& GetInputWindow();

} // namespace dover::overlay::input
