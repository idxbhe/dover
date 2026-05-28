#include "overlay/ui/components/base_window.h"
#include "overlay/icons.h"
#include "overlay/game_storage.h"
#include <imgui.h>

namespace dover::overlay {
    extern ImFont* g_font_gui;
}

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
            ImGui::SetNextWindowSize(m_default_size, ImGuiCond_FirstUseEver);
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
        
        // Handle Esc key to close the focused window
        if (interactive && m_is_focused && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            Close();
        }
        
        // Track the correct parent window size and position while windowed
        if (!m_is_maximized) {
            m_prev_pos = ImGui::GetWindowPos();
            m_prev_size = ImGui::GetWindowSize();
        }
        
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
            if (m_window_name != "Settings") {
                RenderToolbar(interactive);
                
                float avail_x = ImGui::GetContentRegionAvail().x;
                float right_boundary = ImGui::GetCursorPosX() + avail_x;
                
                RenderWindowDecorations(interactive, right_boundary);

                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
                ImGui::Separator();
                ImGui::PopStyleVar();
            }
        }

        // Main content area
        RenderContent(interactive);

        // Floating buttons or popups
        PostRender(interactive);
    }

    if (!begin_ok) {
        ImGui::End();
        return;
    }

    ImGui::End();
}

void BaseWindow::RenderWindowDecorations(bool interactive, float right_boundary) {
    if (!interactive) return;

    auto DrawCustomButton = [&](const char* icon, float same_line_pos, const char* tooltip, bool* toggle_state = nullptr) -> bool {
        ImGui::SameLine(same_line_pos);
        if (m_window_name == "Settings") {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 6.0f);
        }
        
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0, 0, 0, 0));
        
        bool was_pinned = (toggle_state && *toggle_state);
        
        bool clicked = ImGui::Button(icon);
        
        ImGui::PopStyleColor(4);
        
        ImVec2 min_p = ImGui::GetItemRectMin();
        ImVec2 max_p = ImGui::GetItemRectMax();
        ImVec2 center = ImVec2(min_p.x + (max_p.x - min_p.x) * 0.5f, min_p.y + (max_p.y - min_p.y) * 0.5f);
        
        bool hovered = ImGui::IsItemHovered();
        bool active = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        
        ImVec4 bg_color = ImVec4(0.170f, 0.200f, 0.246f, 1.00f);
        ImVec4 border_color = ImVec4(0.120f, 0.141f, 0.174f, 1.00f);
        
        if (active) {
            bg_color = ImVec4(0.280f, 0.370f, 0.500f, 1.00f);
            border_color = ImVec4(0.210f, 0.280f, 0.380f, 1.00f);
        } else if (hovered) {
            bg_color = ImVec4(0.230f, 0.300f, 0.410f, 1.00f);
            border_color = ImVec4(0.170f, 0.220f, 0.300f, 1.00f);
        }
        
        ImU32 bg_col32 = ImGui::ColorConvertFloat4ToU32(bg_color);
        ImU32 border_col32 = ImGui::ColorConvertFloat4ToU32(border_color);
        
        ImU32 text_col32;
        if (was_pinned) {
            text_col32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.20f, 0.80f, 0.20f, 1.00f));
        } else {
            text_col32 = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]);
        }
        
        ImGui::GetWindowDrawList()->AddRectFilled(min_p, max_p, bg_col32, 2.0f);
        
        ImVec2 mid_p = ImVec2(max_p.x, min_p.y + (max_p.y - min_p.y) * 0.5f);
        ImU32 half_hl_col = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.03f));
        ImGui::GetWindowDrawList()->AddRectFilled(min_p, mid_p, half_hl_col, 2.0f, ImDrawFlags_RoundCornersTop);
        
        ImGui::GetWindowDrawList()->AddRect(min_p, max_p, border_col32, 2.0f, 0, 1.0f);
        
        if (g_font_gui) ImGui::PushFont(g_font_gui);
        ImVec2 text_size = ImGui::CalcTextSize(icon);
        ImVec2 text_pos = ImVec2(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f);
        ImGui::GetWindowDrawList()->AddText(text_pos, text_col32, icon);
        if (g_font_gui) ImGui::PopFont();
        
        if (hovered && tooltip) {
            ImGui::SetTooltip(tooltip);
        }
        
        if (clicked && toggle_state) {
            *toggle_state = !*toggle_state;
        }
        
        return clicked;
    };

    if (HasFeature(m_features, WindowFeature::Pin)) {
        DrawCustomButton(ICON_WINDOW_PINNED, right_boundary - 86.0f, m_is_pinned ? "Unpin from screen" : "Pin to screen", &m_is_pinned);
    }

    if (HasFeature(m_features, WindowFeature::Maximize)) {
        const char* icon = m_is_maximized ? ICON_WINDOW_WINDOWED : ICON_WINDOW_FULL;
        const char* tooltip = m_is_maximized ? "Restore Window Size" : "Maximize Window";
        if (DrawCustomButton(icon, right_boundary - 60.0f, tooltip)) {
            if (!m_is_maximized) {
                m_was_maximized = true;
            }
            m_is_maximized = !m_is_maximized;
        }
    }

    if (HasFeature(m_features, WindowFeature::Close)) {
        if (DrawCustomButton(ICON_WINDOW_CLOSE, right_boundary - 34.0f, "Close")) {
            Close();
        }
    }
}

void BaseWindow::Open() {
    m_is_open = true;
    GameStorage::Get().SaveState();
}

void BaseWindow::Close() {
    m_is_open = false;
    GameStorage::Get().SaveState();
}

void BaseWindow::ToggleOpen() {
    m_is_open = !m_is_open;
    GameStorage::Get().SaveState();
}

} // namespace dover::overlay::ui
