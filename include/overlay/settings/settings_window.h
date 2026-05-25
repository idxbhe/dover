#pragma once

#include <overlay/ui/components/base_window.h>

namespace dover::overlay::settings {

class SettingsWindow : public ui::BaseWindow {
public:
    SettingsWindow();
    ~SettingsWindow() override = default;

    // We only need to override Initialize if we want defaults,
    // and RenderContent to draw our configurations UI.
    void Initialize();

protected:
    void RenderContent(bool interactive) override;

private:
    int m_selected_category = 0; // 0 = General, 1 = Keybinds, 2 = Theme, 3 = About
    float m_sidebar_width = 140.0f;
};

// Global instance getter
SettingsWindow& GetSettingsWindow();

} // namespace dover::overlay::settings
