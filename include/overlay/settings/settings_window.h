#pragma once

#include "shared/ui/components/base_window.h"

namespace dover::overlay::settings {

class SettingsWindow : public shared::ui::BaseWindow {
public:
    SettingsWindow();
    ~SettingsWindow() override = default;

    void Initialize();

    // State accessors for GameStorage persistence
    int  GetSelectedCategory() const { return m_selected_category; }
    void SetSelectedCategory(int cat) { m_selected_category = cat; }

protected:
    void RenderContent(bool interactive) override;

private:
    int m_selected_category = 0; // 0 = General, 1 = Keybinds, 2 = Theme, 3 = About
    float m_sidebar_width = 140.0f;
};

// Global instance getter
SettingsWindow& GetSettingsWindow();

} // namespace dover::overlay::settings
