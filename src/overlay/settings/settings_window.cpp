#include "overlay/settings/settings_window.h"
#include "overlay/icons.h"
#include <imgui.h>
#include "overlay/overlay_ui.h"
#include "overlay/notes/manager.h"
#include "overlay/notes/layout.h"
namespace dover::overlay {
    extern ImFont* g_font_gui;
    extern ImFont* g_fonts_preview_bold[5];
}

namespace dover::overlay::settings {

SettingsWindow::SettingsWindow()
    : ui::BaseWindow("Settings", ui::WindowFeature::NoPin, ImVec2(520.0f, 300.0f)) {
    m_selected_category = 0;
    m_sidebar_width = 165.0f;
}

void SettingsWindow::Initialize() {
    m_is_open = false;
}

void SettingsWindow::RenderContent(bool interactive) {
    const float win_w    = ImGui::GetContentRegionAvail().x;
    const float win_h    = ImGui::GetContentRegionAvail().y;
    const float sb_w     = 165.0f; // Fixed unresizable sidebar width
    const float split_w  = 1.0f;   // 1px static line width
    const float cont_w   = win_w - sb_w - split_w;

    // 1. Sidebar Child Window
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("SettingsSidebar", ImVec2(sb_w, win_h), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    {
        ImVec2 min_p = ImGui::GetWindowPos();
        ImVec2 max_p = ImVec2(min_p.x + ImGui::GetWindowSize().x, min_p.y + ImGui::GetWindowSize().y);
        ImU32 col_tl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.063f, 0.071f, 0.086f, m_bg_alpha));
        ImU32 col_tr = ImGui::ColorConvertFloat4ToU32(ImVec4(0.059f, 0.067f, 0.082f, m_bg_alpha));
        ImU32 col_br = ImGui::ColorConvertFloat4ToU32(ImVec4(0.055f, 0.063f, 0.078f, m_bg_alpha));
        ImU32 col_bl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.063f, 0.071f, 0.086f, m_bg_alpha));
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(min_p, max_p, col_tl, col_tr, col_br, col_bl);
    }

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));

    struct Category {
        const char* icon;
        const char* name;
    };

    Category categories[] = {
        { ICON_PANEL_SETTINGS,   "General"  },
        { ICON_SETTING_KEYBINDS, "Keybinds" },
        { ICON_SETTING_THEME,    "Theme"    },
        { ICON_SETTING_ABOUT,    "About"    }
    };

    for (int i = 0; i < 4; ++i) {
        bool is_sel = (i == m_selected_category);
        std::string id_str = "##cat_" + std::to_string(i);
        
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 min_p = ImVec2(pos.x + 6.0f, pos.y);
        ImVec2 max_p = ImVec2(pos.x + sb_w - 6.0f, pos.y + 32.0f);
        
        ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0, 0, 0, 0));
        
        ImGui::SetCursorPosX(6.0f);
        bool selected_now = ImGui::Selectable(id_str.c_str(), is_sel, ImGuiSelectableFlags_None, ImVec2(sb_w - 12.0f, 32.0f));
        
        bool is_hovered = ImGui::IsItemHovered();
        bool is_active = ImGui::IsItemActive();
        
        ImGui::PopStyleColor(3);
        
        if (is_sel || is_hovered || is_active) {
            ImVec4 highlight_color = ImVec4(0, 0, 0, 0);
            if (is_sel) {
                if (is_active) highlight_color = ImVec4(0.33f, 0.50f, 0.75f, 1.00f);
                else if (is_hovered) highlight_color = ImVec4(0.28f, 0.44f, 0.68f, 0.90f);
                else highlight_color = ImVec4(0.22f, 0.38f, 0.62f, 0.85f);
            } else {
                if (is_active) highlight_color = ImGui::GetStyle().Colors[ImGuiCol_HeaderActive];
                else if (is_hovered) highlight_color = ImGui::GetStyle().Colors[ImGuiCol_HeaderHovered];
            }
            if (highlight_color.w > 0.0f) {
                ImGui::GetWindowDrawList()->AddRectFilled(min_p, max_p, ImGui::GetColorU32(highlight_color), 4.0f);
            }
        }
        
        float text_y = pos.y + (32.0f - ImGui::GetFontSize()) * 0.5f;
        std::string label = std::string(categories[i].icon) + "  " + categories[i].name;
        
        if (g_font_gui) ImGui::PushFont(g_font_gui);
        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + 16.0f, text_y), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
        if (g_font_gui) ImGui::PopFont();
        
        if (selected_now && !is_sel) {
            m_selected_category = i;
        }
    }

    ImGui::PopStyleVar(2);
    ImGui::EndChild();

    // 2. Static Vertical Separator Line (Unresizable)
    ImGui::SameLine(0.0f, 0.0f);
    {
        ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(cursor_pos.x, cursor_pos.y),
            ImVec2(cursor_pos.x, cursor_pos.y + win_h),
            ImGui::GetColorU32(ImGuiCol_Border)
        );
    }
    ImGui::Dummy(ImVec2(split_w, win_h));
    ImGui::SameLine(0.0f, 0.0f);

    // 3. Settings Content Child Window
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.067f, 0.067f, 0.067f, m_bg_alpha));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(1.0f, 1.0f, 1.0f, 0.45f));
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 12.0f);
    
    bool content_ok = ImGui::BeginChild("SettingsContent", ImVec2(cont_w, win_h), false, ImGuiWindowFlags_AlwaysUseWindowPadding);
    
    if (content_ok) {
        // Draw the window close decoration directly inside the top-right of SettingsContent child window
        // so it captures inputs and remains fully interactive on top of the content layer!
        {
            ImVec2 orig_cursor = ImGui::GetCursorPos();
            
            float avail_x = ImGui::GetContentRegionAvail().x;
            float right_boundary = ImGui::GetCursorPosX() + avail_x + 14.0f;
            
            RenderWindowDecorations(interactive, right_boundary);
            
            ImGui::SetCursorPos(orig_cursor);
        }

        switch (m_selected_category) {
            case 0: { // General
                ImGui::Text("Application Configurations:");
                ImGui::Spacing();
                static bool vsync = true;
                ImGui::Checkbox("Enable VSync Simulation", &vsync);
                ImGui::Spacing();

                ImGui::Text("OSD (On Screen Display):");
                ImGui::Spacing();
                ImGui::Checkbox("FPS", &g_cfg_show_fps);
                ImGui::Checkbox("CLOCK", &g_cfg_show_clock);
                ImGui::Checkbox("GRAPHIC API", &g_cfg_show_api);
                ImGui::Spacing();
                
                ImGui::TextDisabled("Future game-specific saving:");
                ImGui::TextDisabled("dover/imgui_<game>.ini");
                break;
            }
            case 1: { // Keybinds
                ImGui::Text("Shortcut Configuration:");
                ImGui::Spacing();
                ImGui::TextDisabled("Toggle Overlay Menu:  [Insert] or [Ctrl+Shift+O]");
                ImGui::TextDisabled("Quick Notes:          [Alt+N]");
                ImGui::TextDisabled("Toggle Pin State:     [Alt+P]");
                break;
            }
            case 2: { // Theme
                ImGui::Text("Appearance Settings:");
                ImGui::Spacing();
                ImGui::Text("Current Theme: Steam Slate Blue");
                ImGui::Spacing();
                
                auto RenderSlimSlider = [&](const char* label, float* value_ptr, auto onChangeCallback) {
                    ImGui::TextDisabled("%s", label);
                    ImGui::SameLine(130.0f);
                    
                    float orig_y = ImGui::GetCursorPosY();
                    
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
                    ImGui::SetCursorPosY(orig_y + 3.0f);
                    
                    ImGui::PushStyleColor(ImGuiCol_FrameBg,          ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,   ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,    ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
                    ImGui::PushStyleColor(ImGuiCol_SliderGrab,       ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
                    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.00f, 0.00f, 0.00f, 0.00f));
                    
                    const float slider_width = 120.0f;
                    float frame_height = ImGui::GetFrameHeight();
                    ImVec2 slider_pos = ImGui::GetCursorScreenPos();
                    ImGui::SetNextItemWidth(slider_width);
                    
                    std::string id_str = "##slider_";
                    id_str += label;
                    
                    bool changed = ImGui::SliderFloat(id_str.c_str(), value_ptr, 0.00f, 1.00f, "");
                    
                    float grab_center_x = slider_pos.x + (*value_ptr) * slider_width;
                    float grab_center_y = slider_pos.y + frame_height * 0.5f;
                    
                    ImVec4 grab_color = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
                    if (ImGui::IsItemActive()) {
                        grab_color = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
                    } else if (ImGui::IsItemHovered()) {
                        grab_color = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
                    }
                    
                    // Draw modern slim background track
                    float track_h = 3.0f;
                    ImVec2 track_min = ImVec2(slider_pos.x, slider_pos.y + frame_height * 0.5f - track_h * 0.5f);
                    ImVec2 track_max = ImVec2(slider_pos.x + slider_width, slider_pos.y + frame_height * 0.5f + track_h * 0.5f);
                    ImGui::GetWindowDrawList()->AddRectFilled(track_min, track_max, ImGui::GetColorU32(ImVec4(1.00f, 1.00f, 1.00f, 0.15f)), 1.5f);

                    // Draw active filled track (theme accent color matching notes premium slider)
                    if (*value_ptr > 0.0f) {
                        ImVec2 active_max = ImVec2(grab_center_x, slider_pos.y + frame_height * 0.5f + track_h * 0.5f);
                        ImGui::GetWindowDrawList()->AddRectFilled(track_min, active_max, ImGui::GetColorU32(ImVec4(0.118f, 0.478f, 0.812f, 0.90f)), 1.5f);
                    }
                    
                    // Draw premium grab indicator
                    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(grab_center_x, grab_center_y), 5.5f, ImGui::GetColorU32(grab_color), 32);
                    
                    ImGui::PopStyleColor(5);
                    ImGui::PopStyleVar(1);
                    
                    ImGui::SetCursorPosY(orig_y);
                    ImGui::SameLine(130.0f + slider_width + 12.0f);
                    ImGui::TextDisabled("%.0f%%", (*value_ptr) * 100.0f);
                    
                    if (changed) {
                        onChangeCallback();
                    }
                };

                RenderSlimSlider("Window Opacity", &g_global_window_alpha, [&]() {
                    m_bg_alpha = g_global_window_alpha;
                    notes::GetNotesWindow().SetBgAlpha(g_global_window_alpha);
                });
                
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                
                RenderSlimSlider("Overlay Opacity", &g_overlay_bg_alpha, []() {});
                break;
            }
            case 3: { // About
                ImGui::Text("dOverlay");
                ImGui::TextDisabled("Version 1.0.0");
                ImGui::Spacing();
                ImGui::Text("A high-performance modular overlay application.");
                ImGui::Text("Built with C++ & Zig.");
                break;
            }
        }
        ImGui::EndChild();
    }
    
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
}

SettingsWindow& GetSettingsWindow() {
    static SettingsWindow instance;
    return instance;
}

} // namespace dover::overlay::settings
