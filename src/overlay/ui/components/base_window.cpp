#include "overlay/ui/components/base_window.h"
#include "overlay/icons.h"
#include <imgui.h>

namespace dover::overlay::ui {

namespace {
    constexpr float kNavBarHeight = 42.0f;
}

void BaseWindow::Render(bool interactive) {
    // If not interactive and not pinned, completely hide the window
    if (!interactive && !m_is_pinned) {
        return;
    }

    if (!m_is_open) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display_size = io.DisplaySize;

    ImGuiWindowFlags win_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | 
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    
    if (!interactive) {
        win_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMouseInputs |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
    }
    
    if (m_is_maximized) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, kNavBarHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(display_size.x, display_size.y - kNavBarHeight), ImGuiCond_Always);
        win_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    } else {
        if (m_was_maximized) {
            ImGui::SetNextWindowPos(m_prev_pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(m_prev_size, ImGuiCond_Always);
            m_was_maximized = false;
        } else {
            ImGui::SetNextWindowSize(ImVec2(800.0f, 500.0f), ImGuiCond_FirstUseEver);
        }
    }

    bool no_border = m_is_maximized || !interactive;
    ImGui::SetNextWindowSizeConstraints(ImVec2(150.0f, 150.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, m_is_maximized ? 0.0f : 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    
    if (no_border) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.12f, 0.15f, 0.22f, 1.00f));
    }

    ImGui::SetNextWindowBgAlpha(m_bg_alpha);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    bool begin_ok = ImGui::Begin(m_window_name.c_str(), &m_is_open, win_flags);
    ImGui::PopStyleColor();

    if (no_border) {
        ImGui::PopStyleVar(3);
    } else {
        ImGui::PopStyleColor(1);
        ImGui::PopStyleVar(3);
    }

    if (begin_ok) {
        m_is_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        
        // Draw Slate-Blue gradient background base
        ImVec2 min_p = ImGui::GetWindowPos();
        ImVec2 max_p = ImVec2(min_p.x + ImGui::GetWindowSize().x, min_p.y + ImGui::GetWindowSize().y);
        ImU32 col_tl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.110f, 0.125f, 0.161f, m_bg_alpha)); // #1c2029
        ImU32 col_tr = ImGui::ColorConvertFloat4ToU32(ImVec4(0.090f, 0.102f, 0.130f, m_bg_alpha)); // #171a21
        ImU32 col_br = ImGui::ColorConvertFloat4ToU32(ImVec4(0.071f, 0.082f, 0.106f, m_bg_alpha)); // #12151b
        ImU32 col_bl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.094f, 0.106f, 0.137f, m_bg_alpha)); // #181b23
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(min_p, max_p, col_tl, col_tr, col_br, col_bl);

        // Pre-render hooks (like internal backgrounds/shadows)
        PreRender(interactive);

        // Standardized Header / Toolbar
        if (interactive) {
            RenderToolbar(interactive);
            
            float avail_x = ImGui::GetContentRegionAvail().x;
            float right_boundary = ImGui::GetCursorPosX() + avail_x;
            
            RenderWindowDecorations(interactive, right_boundary);

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
            ImGui::Separator();
            ImGui::PopStyleVar();
        }

        // Main content area
        RenderContent(interactive);

        // Floating buttons or popups
        PostRender(interactive);
    }

    if (!begin_ok) {
        ImGui::End();
        ImGui::PopStyleVar(3);
        return;
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}

void BaseWindow::RenderWindowDecorations(bool interactive, float right_boundary) {
    if (!interactive) return;

    // Fixed absolute positions from right edge to prevent wrapping
    if (HasFeature(m_features, WindowFeature::Pin)) {
        ImGui::SameLine(right_boundary - 86.0f);
        bool was_pinned = m_is_pinned;
        if (was_pinned) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.80f, 0.20f, 1.00f));
        if (ImGui::Button(ICON_WINDOW_PINNED)) m_is_pinned = !m_is_pinned;
        if (was_pinned) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(m_is_pinned ? "Unpin from screen" : "Pin to screen");
    }

    if (HasFeature(m_features, WindowFeature::Maximize)) {
        ImGui::SameLine(right_boundary - 60.0f);
        if (ImGui::Button(m_is_maximized ? ICON_WINDOW_WINDOWED : ICON_WINDOW_FULL)) {
            if (!m_is_maximized) {
                m_prev_pos = ImGui::GetWindowPos();
                m_prev_size = ImGui::GetWindowSize();
                m_was_maximized = true;
            }
            m_is_maximized = !m_is_maximized;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(m_is_maximized ? "Restore Window Size" : "Maximize Window");
    }

    if (HasFeature(m_features, WindowFeature::Close)) {
        ImGui::SameLine(right_boundary - 34.0f);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.15f, 0.15f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.90f, 0.20f, 0.20f, 1.00f));
        if (ImGui::Button(ICON_WINDOW_CLOSE)) Close();
        ImGui::PopStyleColor(2);
    }
}

} // namespace dover::overlay::ui
