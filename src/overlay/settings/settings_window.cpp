#include "overlay/settings/settings_window.h"
#include "overlay/input_hook.h"

#include "overlay/icons.h"

#include "overlay/game_storage.h"

#include <imgui.h>

#include "overlay/overlay_ui.h"

#include "overlay/notes/manager.h"

#include "overlay/notes/layout.h"

namespace dover::overlay {

    extern ImFont* g_font_gui;

    extern ImFont* g_font_panel;

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



namespace {

static bool ToggleCheckbox(const char* label, bool* value_ptr) {

    float avail_w = ImGui::GetContentRegionAvail().x;

    float height = 26.0f; // Premium row height matching the 28.0f icon

    

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));

    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0, 0, 0, 0));

    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));

    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0, 0, 0, 0));



    ImVec2 pos = ImGui::GetCursorScreenPos();

    pos.x = float(int(pos.x));

    pos.y = float(int(pos.y));

    

    char id_buf[128];

    snprintf(id_buf, sizeof(id_buf), "##selectable_%s", label);

    

    bool clicked = ImGui::Selectable(id_buf, false, ImGuiSelectableFlags_None, ImVec2(avail_w, height));

    bool hovered = ImGui::IsItemHovered();

    bool active = ImGui::IsItemActive();

    

    ImGui::PopStyleColor(3);

    ImGui::PopStyleVar();



    if (clicked) {

        *value_ptr = !(*value_ptr);

    }



    ImDrawList* dl = ImGui::GetWindowDrawList();

    

    // Draw premium custom rounded highlight box if hovered or active

    if (hovered || active) {

        ImVec4 highlight_color = active 

            ? ImVec4(0.28f, 0.28f, 0.35f, 0.45f) // Active / clicked state

            : ImVec4(0.15f, 0.15f, 0.20f, 0.25f); // Hover state

        

        ImVec2 min_p = pos;

        ImVec2 max_p = ImVec2(pos.x + avail_w, pos.y + height);

        // Beautiful 6.0f rounding for elegant settings content items

        dl->AddRectFilled(min_p, max_p, ImGui::GetColorU32(highlight_color), 6.0f);

    }

    

    // Label on the left - perfectly pixel-aligned

    float text_y = float(int(pos.y + (height - ImGui::GetFontSize()) * 0.5f));

    dl->AddText(ImVec2(pos.x + 8.0f, text_y), ImGui::GetColorU32(ImGuiCol_Text), label);



    // Icon on the right (Obsidian accent blue when ON, slate/gray when OFF)

    const char* icon = *value_ptr ? ICON_TOGGLE_ON : ICON_TOGGLE_OFF;

    ImVec4 icon_color = *value_ptr ? ImVec4(0.118f, 0.478f, 0.812f, 1.00f) : ImVec4(0.40f, 0.42f, 0.48f, 0.80f);

    if (hovered && !*value_ptr) {

        icon_color = ImVec4(0.60f, 0.62f, 0.68f, 1.00f);

    } else if (hovered && *value_ptr) {

        icon_color = ImVec4(0.200f, 0.569f, 0.902f, 1.00f);

    }



    // Use g_font_panel to get the larger 28.0f icons. 

    // CRITICAL FIX: Pass the native FontSize (20.0f) to AddText!

    // If we pass 28.0f, ImGui will upscale the font by 1.4x, making it 39.2px and blurry!

    // Passing its native 20.0f size ensures 1:1 pixel mapping, rendering the icon exactly at its crisp 28.0f rasterized resolution.

    ImFont* icon_font = dover::overlay::g_font_panel ? dover::overlay::g_font_panel : dover::overlay::g_font_gui;

    float icon_font_size = icon_font->FontSize; 

    

    ImVec2 icon_size = icon_font->CalcTextSizeA(icon_font_size, FLT_MAX, 0.0f, icon);

    

    // Align vertically and horizontally on strict integer boundaries to eliminate subpixel blur.

    // Apply a +2.0f vertical offset to perfectly match the visual center of the 28.0f SVG icon glyphs.

    float icon_x = float(int(pos.x + avail_w - icon_size.x - 8.0f));

    float icon_y = float(int(pos.y + (height - icon_size.y) * 0.5f)) + 2.0f;

    

    dl->AddText(icon_font, icon_font_size, 

                ImVec2(icon_x, icon_y), 

                ImGui::GetColorU32(icon_color), icon);



    return clicked;

}



static void RenderSettingsHeader(const char* title) {

    if (dover::overlay::g_fonts_preview_bold[3]) {

        ImGui::PushFont(dover::overlay::g_fonts_preview_bold[3]);

    }

    // Sleek and modern Steam Slate Blue / Obsidian light-blue accent (#8fafd6)

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.68f, 0.84f, 1.00f));

    

    ImGui::Text("%s", title);

    

    ImGui::PopStyleColor();

    if (dover::overlay::g_fonts_preview_bold[3]) {

        ImGui::PopFont();

    }

}

} // namespace



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

        char id_buf[16];

        snprintf(id_buf, sizeof(id_buf), "##cat_%d", i);

        

        ImVec2 pos = ImGui::GetCursorScreenPos();

        ImVec2 min_p = ImVec2(pos.x + 6.0f, pos.y);

        ImVec2 max_p = ImVec2(pos.x + sb_w - 6.0f, pos.y + 32.0f);

        

        ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0, 0, 0, 0));

        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));

        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0, 0, 0, 0));

        

        ImGui::SetCursorPosX(6.0f);

        bool selected_now = ImGui::Selectable(id_buf, is_sel, ImGuiSelectableFlags_None, ImVec2(sb_w - 12.0f, 32.0f));

        

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

        char label_buf[64];

        snprintf(label_buf, sizeof(label_buf), "%s  %s", categories[i].icon, categories[i].name);

        

        if (g_font_gui) ImGui::PushFont(g_font_gui);

        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + 16.0f, text_y), ImGui::GetColorU32(ImGuiCol_Text), label_buf);

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
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 3.0f));

    

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
                RenderSettingsHeader("Application Configurations");
                static bool vsync = true;
                ToggleCheckbox("Enable VSync Simulation", &vsync);

                ImGui::Dummy(ImVec2(0.0f, 10.0f)); // Elegant vertical spacing between groups

                RenderSettingsHeader("OSD (On Screen Display)");
                if (ToggleCheckbox("FPS", &GetOverlayConfig().show_fps))           GameStorage::Get().SaveConfig();
                if (ToggleCheckbox("CLOCK", &GetOverlayConfig().show_clock))        GameStorage::Get().SaveConfig();
                if (ToggleCheckbox("GRAPHIC API", &GetOverlayConfig().show_api))    GameStorage::Get().SaveConfig();
                break;
            }

            case 1: { // Keybinds
                RenderSettingsHeader("Shortcut Configuration");
                
                static bool is_recording = false;
                
                auto RenderHotkeySelector = [&](const char* label, int& main_key, int& modifier_key) {
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    float avail_w = ImGui::GetContentRegionAvail().x;
                    float height = 26.0f;
                    
                    bool hovered = ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + avail_w, pos.y + height));
                    ImVec4 bg_color = hovered ? ImVec4(1.0f, 1.0f, 1.0f, 0.04f) : ImVec4(0,0,0,0);
                    if (is_recording) bg_color = ImVec4(0.118f, 0.478f, 0.812f, 0.15f);
                    
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(pos, ImVec2(pos.x + avail_w, pos.y + height), ImGui::GetColorU32(bg_color), 6.0f);
                    
                    float text_y = float(int(pos.y + (height - ImGui::GetFontSize()) * 0.5f));
                    dl->AddText(ImVec2(pos.x + 8.0f, text_y), ImGui::GetColorU32(ImGuiCol_Text), label);
                    
                    ImGui::Dummy(ImVec2(avail_w, height));
                    if (ImGui::IsItemClicked()) {
                        is_recording = !is_recording;
                    }
                    
                    if (is_recording) {
                        for (int i = 0x08; i <= 0xFE; i++) {
                            if (i == VK_SHIFT || i == VK_CONTROL || i == VK_MENU || 
                                i == VK_LSHIFT || i == VK_RSHIFT || 
                                i == VK_LCONTROL || i == VK_RCONTROL || 
                                i == VK_LMENU || i == VK_RMENU) {
                                continue;
                            }
                            if (dover::overlay::IsHardwareKeyPressed(i)) {
                                main_key = i;
                                modifier_key = 0;
                                if (dover::overlay::IsHardwareKeyPressed(VK_SHIFT)) modifier_key = VK_SHIFT;
                                if (dover::overlay::IsHardwareKeyPressed(VK_CONTROL)) modifier_key = VK_CONTROL;
                                if (dover::overlay::IsHardwareKeyPressed(VK_MENU)) modifier_key = VK_MENU;
                                is_recording = false;
                                GameStorage::Get().SaveConfig();
                                break;
                            }
                        }
                    }
                    
                    char hotkey_str[64] = {};
                    if (is_recording) {
                        snprintf(hotkey_str, sizeof(hotkey_str), "[ Press Key... ]");
                    } else {
                        const char* mod_str = "";
                        if (modifier_key == VK_SHIFT) mod_str = "Shift + ";
                        if (modifier_key == VK_CONTROL) mod_str = "Ctrl + ";
                        if (modifier_key == VK_MENU) mod_str = "Alt + ";
                        
                        char main_char = (char)main_key;
                        const char* main_str = "Unknown";
                        if (main_key >= 'A' && main_key <= 'Z') { main_str = (const char*)&main_char; }
                        else if (main_key >= '0' && main_key <= '9') { main_str = (const char*)&main_char; }
                        else if (main_key == VK_TAB) main_str = "Tab";
                        else if (main_key == VK_INSERT) main_str = "Insert";
                        else if (main_key == VK_HOME) main_str = "Home";
                        else if (main_key == VK_END) main_str = "End";
                        else if (main_key == VK_DELETE) main_str = "Del";
                        else if (main_key == VK_F1) main_str = "F1";
                        else if (main_key == VK_F2) main_str = "F2";
                        else if (main_key == VK_F3) main_str = "F3";
                        else if (main_key == VK_F4) main_str = "F4";
                        else if (main_key == VK_F5) main_str = "F5";
                        else if (main_key == VK_F6) main_str = "F6";
                        else if (main_key == VK_F7) main_str = "F7";
                        else if (main_key == VK_F8) main_str = "F8";
                        else if (main_key == VK_F9) main_str = "F9";
                        else if (main_key == VK_F10) main_str = "F10";
                        else if (main_key == VK_F11) main_str = "F11";
                        else if (main_key == VK_F12) main_str = "F12";
                        else if (main_key == VK_SPACE) main_str = "Space";
                        else if (main_key == VK_OEM_3) main_str = "`";
                        
                        if ((main_key >= 'A' && main_key <= 'Z') || (main_key >= '0' && main_key <= '9')) {
                            snprintf(hotkey_str, sizeof(hotkey_str), "[ %s%c ]", mod_str, main_char);
                        } else {
                            snprintf(hotkey_str, sizeof(hotkey_str), "[ %s%s ]", mod_str, main_str);
                        }
                    }
                    
                    ImVec2 text_size = ImGui::CalcTextSize(hotkey_str);
                    float val_x = float(int(pos.x + avail_w - text_size.x - 8.0f));
                    dl->AddText(ImVec2(val_x, text_y), is_recording ? ImGui::GetColorU32(ImVec4(0.56f, 0.68f, 0.84f, 1.00f)) : ImGui::GetColorU32(ImGuiCol_TextDisabled), hotkey_str);
                };

                RenderHotkeySelector("Toggle Overlay Menu", GetOverlayConfig().hotkey_toggle_main, GetOverlayConfig().hotkey_toggle_modifier);
                
                ImGui::Dummy(ImVec2(0.0f, 8.0f)); // Premium small spacing before secondary keybinds
                
                ImGui::TextDisabled("Quick Notes:          [Alt+N]");
                ImGui::TextDisabled("Toggle Pin State:     [Alt+P]");
                break;
            }
            case 2: { // Theme
                RenderSettingsHeader("Appearance Settings");

                ImGui::Text("Current Theme: Steam Slate Blue");
                
                auto RenderSlimSlider = [&](const char* label, float* value_ptr, auto onChangeCallback) {
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    float avail_w = ImGui::GetContentRegionAvail().x;
                    float height = 26.0f;
                    
                    float text_y = float(int(pos.y + (height - ImGui::GetFontSize()) * 0.5f));
                    ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + 8.0f, text_y), ImGui::GetColorU32(ImGuiCol_Text), label);
                    
                    const float slider_width = 120.0f;
                    const float pct_width = 36.0f;
                    const float right_margin = 8.0f;
                    
                    float slider_x = pos.x + avail_w - slider_width - pct_width - right_margin - 8.0f;
                    float pct_x = pos.x + avail_w - pct_width - right_margin;
                    
                    ImGui::Dummy(ImVec2(avail_w, height));
                    ImGui::SetCursorScreenPos(ImVec2(slider_x, pos.y + (height - ImGui::GetFrameHeight()) * 0.5f));
                    
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBg,          ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,   ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,    ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_SliderGrab,       ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
                    
                    ImGui::SetNextItemWidth(slider_width);
                    char slider_id_buf[64];
                    snprintf(slider_id_buf, sizeof(slider_id_buf), "##slider_%s", label);
                    
                    bool changed = ImGui::SliderFloat(slider_id_buf, value_ptr, 0.00f, 1.00f, "");
                    bool active = ImGui::IsItemActive();
                    bool hovered = ImGui::IsItemHovered();
                    
                    ImGui::PopStyleColor(5);
                    ImGui::PopStyleVar();
                    
                    float frame_height = ImGui::GetFrameHeight();
                    ImVec2 slider_pos = ImVec2(slider_x, pos.y + (height - frame_height) * 0.5f);
                    
                    float grab_center_x = slider_pos.x + (*value_ptr) * slider_width;
                    float grab_center_y = slider_pos.y + frame_height * 0.5f;
                    
                    ImVec4 grab_color = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
                    if (active) {
                        grab_color = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
                    } else if (hovered) {
                        grab_color = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
                    }
                    
                    ImGui::GetWindowDrawList()->AddLine(
                        ImVec2(slider_pos.x, grab_center_y),
                        ImVec2(slider_pos.x + slider_width, grab_center_y),
                        ImGui::GetColorU32(ImVec4(1.00f, 1.00f, 1.00f, 0.12f)),
                        2.5f
                    );
                    
                    ImGui::GetWindowDrawList()->AddLine(
                        ImVec2(slider_pos.x, grab_center_y),
                        ImVec2(grab_center_x, grab_center_y),
                        ImGui::GetColorU32(ImVec4(0.118f, 0.478f, 0.812f, 1.00f)),
                        2.5f
                    );
                    
                    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(grab_center_x, grab_center_y), 5.5f, ImGui::GetColorU32(grab_color), 32);
                    
                    char pct_buf[16];
                    snprintf(pct_buf, sizeof(pct_buf), "%3.0f%%", (*value_ptr) * 100.0f);
                    float pct_y = float(int(pos.y + (height - ImGui::GetFontSize()) * 0.5f));
                    ImGui::GetWindowDrawList()->AddText(ImVec2(pct_x, pct_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), pct_buf);
                    
                    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + height + ImGui::GetStyle().ItemSpacing.y));
                    
                    if (changed) {
                        onChangeCallback();
                    }
                };

                RenderSlimSlider("Window Opacity", &GetOverlayConfig().global_window_alpha, [&]() {
                    m_bg_alpha = GetOverlayConfig().global_window_alpha;
                    notes::GetNotesWindow().SetBgAlpha(GetOverlayConfig().global_window_alpha);
                    GameStorage::Get().SaveConfig();
                });
                
                RenderSlimSlider("Overlay Opacity", &GetOverlayConfig().overlay_bg_alpha, []() {
                    GameStorage::Get().SaveConfig();
                });
                break;
            }
            case 3: { // About

                RenderSettingsHeader("dOverlay");

                ImGui::TextDisabled("Version 1.0.0");

                ImGui::Spacing();

                ImGui::Text("A high-performance modular overlay application.");

                ImGui::Text("Built with C++ & Zig.");

                break;

            }

        }

        ImGui::EndChild();

    }

    

    ImGui::PopStyleVar(4);

    ImGui::PopStyleColor(5);

}



SettingsWindow& GetSettingsWindow() {

    static SettingsWindow instance;

    return instance;

}



} // namespace dover::overlay::settings

