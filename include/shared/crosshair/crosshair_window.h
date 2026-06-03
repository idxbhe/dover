#pragma once

#include "shared/ui/components/base_window.h"
#include <imgui.h>

namespace dover::shared::crosshair {

class CrosshairWindow : public shared::ui::BaseWindow {
public:
    CrosshairWindow();
    
    // Loads assets to GPU and initialized states
    void Initialize();
    
    // Releases GPU assets
    void Shutdown();

    // Renders the Crosshair Picker UI window
    void RenderContent(bool interactive) override;

    // Call this inside the main render loop to draw the crosshair 
    // onto ImGui::GetBackgroundDrawList() when active.
    void RenderCrosshairOverlay();

    // Getters and Setters for State Persistence
    bool IsCrosshairActive() const { return m_active; }
    void SetCrosshairActive(bool v) { m_active = v; }

    int GetSelectedIndex() const { return m_selected_index; }
    void SetSelectedIndex(int v) { m_selected_index = v; }

    const ImVec4& GetColor() const { return m_color; }
    void SetColor(const ImVec4& c) { m_color = c; }

    float GetScale() const { return m_scale; }
    void SetScale(float s) { m_scale = s; }

    float GetOpacity() const { return m_opacity; }
    void SetOpacity(float o) { m_opacity = o; }

    float GetPosX() const { return m_pos_x; }
    void SetPosX(float x) { m_pos_x = x; }

    float GetPosY() const { return m_pos_y; }
    void SetPosY(float y) { m_pos_y = y; }

    bool IsOutlineEnabled() const { return m_outline_enabled; }
    void SetOutlineEnabled(bool v) { m_outline_enabled = v; }

    const ImVec4& GetOutlineColor() const { return m_outline_color; }
    void SetOutlineColor(const ImVec4& c) { m_outline_color = c; }

protected:
    void PreRender(bool interactive) override;

private:
    bool m_active = false;
    int m_selected_index = 0;
    ImVec4 m_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    float m_scale = 1.0f;
    float m_opacity = 1.0f;
    float m_pos_x = 0.0f;
    float m_pos_y = 0.0f;
    
    bool m_outline_enabled = false;
    ImVec4 m_outline_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    
    bool m_textures_loaded = false;
};

CrosshairWindow& GetCrosshairWindow();

} // namespace dover::shared::crosshair
