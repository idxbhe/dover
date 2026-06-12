#include "shared/settings/settings_window.h"
#include "shared/settings/app_config.h"
#include "overlay/input_hook.h"
#include "shared/input_utils.h"

#include "shared/icons.h"
#include "shared/theme.h"
#include "shared/ui/widgets.h"

#include "shared/game_storage.h"

#include <imgui.h>

#include "overlay/overlay_ui.h"

#include "shared/notes/manager.h"

#include "shared/notes/layout.h"

namespace dover::overlay {

}

namespace dover::shared::settings {

SettingsWindow::SettingsWindow()
    : shared::ui::BaseWindow(shared::ui::RenderContext::Overlay, "Settings", shared::ui::WindowFeature::NoPin, ImVec2(500.0f, 400.0f), ImVec2(165.0f, 250.0f)) {
    m_selected_category = 0;
    m_sidebar_width = 165.0f;
}

void SettingsWindow::Initialize() {
    m_is_open = false;
}

namespace {

static void RenderSettingsHeader(const char* title) {
    ImGui::PushFont(dover::shared::g_font_preview_bold, dover::shared::kTitleSize);
    // Sleek and modern Steam Slate Blue / Obsidian light-blue accent (#8fafd6)
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.68f, 0.84f, 1.00f));

    ImGui::Text("%s", title);

    ImGui::PopStyleColor();
    ImGui::PopFont();
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
        { ICON_TEXT_FORMAT_CODE, "Advanced" },
        { ICON_SETTING_ABOUT,    "About"    }
    };

    for (int i = 0; i < 5; ++i) {
        bool is_sel = (i == m_selected_category);
        ImGui::PushID(i);
        
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 min_p = ImVec2(pos.x + 6.0f, pos.y);
        ImVec2 max_p = ImVec2(pos.x + sb_w - 6.0f, pos.y + 32.0f);
        
        ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0, 0, 0, 0));
        
        ImGui::SetCursorPosX(6.0f);
        bool selected_now = ImGui::Selectable("##cat", is_sel, ImGuiSelectableFlags_None, ImVec2(sb_w - 12.0f, 32.0f));
        
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
        
        if (dover::shared::g_font_gui) ImGui::PushFont(dover::shared::g_font_gui, dover::shared::kGuiSize);
        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + 16.0f, text_y), ImGui::GetColorU32(ImGuiCol_Text), label_buf);
        if (dover::shared::g_font_gui) ImGui::PopFont();
        
        if (selected_now && !is_sel) {
            m_selected_category = i;
        }

        ImGui::PopID();
    }

    ImGui::PopStyleVar(2);
    
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && m_selected_category > 0) {
            m_selected_category--;
        } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && m_selected_category < 4) {
            m_selected_category++;
        }
    }

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

    bool content_ok = ImGui::BeginChild("SettingsContent", ImVec2(cont_w, win_h), ImGuiChildFlags_AlwaysUseWindowPadding, 0);
    
    if (content_ok) {
        // Draw the window close decoration directly inside the top-right of SettingsContent child window
        // so it captures inputs and remains fully interactive on top of the content layer!
        {
            ImVec2 orig_cursor = ImGui::GetCursorPos();
            
            float avail_x = ImGui::GetContentRegionAvail().x;
            float right_boundary = ImGui::GetCursorPosX() + avail_x + 14.0f;
            
            RenderWindowDecorations(interactive, right_boundary, 10.0f);
            
            ImGui::SetCursorPos(orig_cursor);
        }

        switch (m_selected_category) {
            case 0: { // General
                RenderSettingsHeader("Application Configurations");
                
                ImGui::Dummy(ImVec2(0.0f, 10.0f)); // Elegant vertical spacing between groups

                RenderSettingsHeader("OSD (On Screen Display)");
                if (shared::ui::ToggleCheckbox("FPS", &shared::GetAppConfig().show_fps))           dover::shared::GameStorage::Get().SaveConfig();
                if (shared::ui::ToggleCheckbox("CLOCK", &shared::GetAppConfig().show_clock))        dover::shared::GameStorage::Get().SaveConfig();
                if (shared::ui::ToggleCheckbox("GRAPHIC API", &shared::GetAppConfig().show_api))    dover::shared::GameStorage::Get().SaveConfig();
                break;
            }

            case 1: { // Keybinds
                RenderSettingsHeader("Shortcut Configuration");
                
                static bool is_recording = false;
                
                auto RenderHotkeySelector = [&](const char* label, std::atomic<int>& main_key, std::atomic<int>& modifier_key) {
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
                        shared::g_is_recording_keybind.store(is_recording, std::memory_order_relaxed);
                    }
                    
                    if (is_recording) {
                        for (int i = 0x08; i <= 0xFE; i++) {
                            if (i == VK_SHIFT || i == VK_CONTROL || i == VK_MENU || 
                                i == VK_LSHIFT || i == VK_RSHIFT || 
                                i == VK_LCONTROL || i == VK_RCONTROL || 
                                i == VK_LMENU || i == VK_RMENU) {
                                continue;
                            }
                            if (shared::IsHardwareKeyPressed(i)) {
                                main_key.store(i, std::memory_order_relaxed);
                                modifier_key.store(0, std::memory_order_relaxed);
                                if (shared::IsHardwareKeyPressed(VK_SHIFT)) modifier_key.store(VK_SHIFT, std::memory_order_relaxed);
                                if (shared::IsHardwareKeyPressed(VK_CONTROL)) modifier_key.store(VK_CONTROL, std::memory_order_relaxed);
                                if (shared::IsHardwareKeyPressed(VK_MENU)) modifier_key.store(VK_MENU, std::memory_order_relaxed);
                                is_recording = false;
                                shared::g_is_recording_keybind.store(false, std::memory_order_relaxed);
                                dover::shared::GameStorage::Get().SaveConfig();
                                break;
                            }
                        }
                    }
                    
                    char hotkey_str[64] = {};
                    int mk = main_key.load(std::memory_order_relaxed);
                    int modk = modifier_key.load(std::memory_order_relaxed);
                    if (is_recording) {
                        snprintf(hotkey_str, sizeof(hotkey_str), "[ Press Key... ]");
                    } else {
                        const char* mod_str = "";
                        if (modk == VK_SHIFT) mod_str = "Shift + ";
                        if (modk == VK_CONTROL) mod_str = "Ctrl + ";
                        if (modk == VK_MENU) mod_str = "Alt + ";
                        
                        char main_char = (char)mk;
                        const char* main_str = "Unknown";
                        if (mk >= 'A' && mk <= 'Z') { main_str = (const char*)&main_char; }
                        else if (mk >= '0' && mk <= '9') { main_str = (const char*)&main_char; }
                        else if (mk == VK_TAB) main_str = "Tab";
                        else if (mk == VK_INSERT) main_str = "Insert";
                        else if (mk == VK_HOME) main_str = "Home";
                        else if (mk == VK_END) main_str = "End";
                        else if (mk == VK_DELETE) main_str = "Del";
                        else if (mk == VK_F1) main_str = "F1";
                        else if (mk == VK_F2) main_str = "F2";
                        else if (mk == VK_F3) main_str = "F3";
                        else if (mk == VK_F4) main_str = "F4";
                        else if (mk == VK_F5) main_str = "F5";
                        else if (mk == VK_F6) main_str = "F6";
                        else if (mk == VK_F7) main_str = "F7";
                        else if (mk == VK_F8) main_str = "F8";
                        else if (mk == VK_F9) main_str = "F9";
                        else if (mk == VK_F10) main_str = "F10";
                        else if (mk == VK_F11) main_str = "F11";
                        else if (mk == VK_F12) main_str = "F12";
                        else if (mk == VK_SPACE) main_str = "Space";
                        else if (mk == VK_OEM_3) main_str = "`";
                        
                        if ((mk >= 'A' && mk <= 'Z') || (mk >= '0' && mk <= '9')) {
                            snprintf(hotkey_str, sizeof(hotkey_str), "[ %s%c ]", mod_str, main_char);
                        } else {
                            snprintf(hotkey_str, sizeof(hotkey_str), "[ %s%s ]", mod_str, main_str);
                        }
                    }
                    
                    ImVec2 text_size = ImGui::CalcTextSize(hotkey_str);
                    float val_x = float(int(pos.x + avail_w - text_size.x - 8.0f));
                    dl->AddText(ImVec2(val_x, text_y), is_recording ? ImGui::GetColorU32(ImVec4(0.56f, 0.68f, 0.84f, 1.00f)) : ImGui::GetColorU32(ImGuiCol_TextDisabled), hotkey_str);
                };

                RenderHotkeySelector("Toggle Overlay Menu", shared::GetAppConfig().hotkey_toggle_main, shared::GetAppConfig().hotkey_toggle_modifier);
                
                if (dover::shared::g_font_gui) ImGui::PushFont(dover::shared::g_font_gui, dover::shared::kPreviewSizes[0]);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.75f, 0.50f, 0.80f)); // Subtle Warning Gold
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
                ImGui::TextWrapped("Note: This hotkey is captured by the overlay and will be inaccessible to the game during gameplay.");
                ImGui::PopStyleColor();
                if (dover::shared::g_font_gui) ImGui::PopFont();

                ImGui::Dummy(ImVec2(0.0f, 8.0f)); // Premium small spacing before secondary keybinds
                
                ImGui::TextDisabled("Quick Notes:          [Alt+N]");
                ImGui::TextDisabled("Toggle Pin State:     [Alt+P]");
                
                break;
            }
            case 2: { // Theme
                RenderSettingsHeader("Appearance Settings");

                ImGui::Text("Current Theme: Steam Slate Blue");
                
                auto RenderSlimSlider = [&](const char* label, std::atomic<float>& value_ref, auto onChangeCallback) {
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
                    
                    float val = value_ref.load(std::memory_order_relaxed);
                    bool changed = ImGui::SliderFloat(slider_id_buf, &val, 0.00f, 1.00f, "");
                    if (changed) value_ref.store(val, std::memory_order_relaxed);

                    bool active = ImGui::IsItemActive();
                    bool hovered = ImGui::IsItemHovered();
                    
                    ImGui::PopStyleColor(5);
                    ImGui::PopStyleVar();
                    
                    float frame_height = ImGui::GetFrameHeight();
                    ImVec2 slider_pos = ImVec2(slider_x, pos.y + (height - frame_height) * 0.5f);
                    
                    float grab_center_x = slider_pos.x + val * slider_width;
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
                    snprintf(pct_buf, sizeof(pct_buf), "%3.0f%%", val * 100.0f);
                    float pct_y = float(int(pos.y + (height - ImGui::GetFontSize()) * 0.5f));
                    ImGui::GetWindowDrawList()->AddText(ImVec2(pct_x, pct_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), pct_buf);
                    
                    // Return cursor to the bottom of the reserved area.
                    // We don't add spacing manually here; ImGui will add it after the Dummy(0,0) or naturally.
                    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + height));
                    ImGui::Dummy(ImVec2(0.0f, 0.0f));
                    
                    if (changed) {
                        onChangeCallback();
                    }
                };

                RenderSlimSlider("Window Opacity", shared::GetAppConfig().global_window_alpha, [&]() {
                    m_bg_alpha = shared::GetAppConfig().global_window_alpha.load();
                    notes::GetNotesWindow().SetBgAlpha(shared::GetAppConfig().global_window_alpha.load());
                    dover::shared::GameStorage::Get().SaveConfig();
                });
                
                RenderSlimSlider("Overlay Opacity", shared::GetAppConfig().overlay_bg_alpha, []() {
                    dover::shared::GameStorage::Get().SaveConfig();
                });
                break;
            }
            case 3: { // Advanced
                RenderSettingsHeader("Advanced Settings");

                ImGui::Dummy(ImVec2(0.0f, 8.0f));

                // Injection Method selector
                {
                    auto& cfg = shared::GetAppConfig();
                    int current_method = static_cast<int>(cfg.injection_method.load(std::memory_order_relaxed));

                    const char* method_names[] = {
                        "Pure VTable  (Recommended)",
                        "Inline Hook  (MinHook)"
                    };

                    float avail_w = ImGui::GetContentRegionAvail().x;
                    ImGui::Text("Injection Method");
                    ImGui::SameLine();
                    // Right-align help marker
                    ImGui::SetCursorPosX(avail_w - 14.0f);
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Pure VTable: Safer against anti-cheat, modifies COM vtable pointer only.\n"
                            "Inline Hook: Uses MinHook to patch function bytes directly in memory.\n\n"
                            "Requires game restart to take effect."
                        );
                    }

                    ImGui::SetNextItemWidth(avail_w);
                    if (ImGui::Combo("##injection_method", &current_method, method_names, 2)) {
                        cfg.injection_method.store(
                            static_cast<dover::shared::InjectionMethod>(current_method),
                            std::memory_order_relaxed);
                        dover::shared::GameStorage::Get().SaveConfig();
                    }

                    ImGui::Spacing();
                    if (dover::shared::g_font_gui) ImGui::PushFont(dover::shared::g_font_gui, dover::shared::kPreviewSizes[0]);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.75f, 0.50f, 0.80f)); // Subtle Warning Gold
                    ImGui::TextWrapped("Note: Changes to the injection method require a full game restart to take effect.");
                    ImGui::PopStyleColor();
                    if (dover::shared::g_font_gui) ImGui::PopFont();
                }
                break;
            }
            case 4: { // About
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

} // namespace dover::shared::settings
