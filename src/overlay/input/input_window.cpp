#include "overlay/input/input_window.h"
#include "overlay/input_hook.h"
#include "shared/icons.h"
#include "shared/theme.h"
#include "shared/game_storage.h"
#include "overlay/overlay_ui.h"

#include <imgui.h>
#include <cstdio>
#include <string>
#include <Xinput.h>
#include <d3d11.h>
#include <d3d9.h>
#include "overlay/assets/asset_storage.h"
#include "overlay/dx11_hook.h"
#include "overlay/dx9_hook.h"

namespace dover::overlay {

}

namespace dover::overlay::input {

struct ButtonOffset {
    const char* name;
    float x_pct;
    float y_pct;
    float w_pct;
    float h_pct;
};

const ButtonOffset g_button_offsets[] = {
    { "btn_a", 0.7667f, 0.5250f, 0.0758f, 0.1137f },
    { "btn_b", 0.8400f, 0.4250f, 0.0758f, 0.1137f },
    { "btn_dpad_down", 0.3658f, 0.7362f, 0.0632f, 0.0731f },
    { "btn_dpad_left", 0.3150f, 0.6700f, 0.0578f, 0.0975f },
    { "btn_dpad_right", 0.4167f, 0.6700f, 0.0578f, 0.0975f },
    { "btn_dpad_up", 0.3650f, 0.5938f, 0.0650f, 0.0840f },
    { "btn_guide", 0.5008f, 0.2838f, 0.0921f, 0.1381f },
    { "btn_lb", 0.2425f, 0.1600f, 0.2546f, 0.1408f },
    { "btn_lstick", 0.2367f, 0.4400f, 0.1264f, 0.1896f },
    { "btn_lt", 0.2133f, 0.0725f, 0.1119f, 0.1408f },
    { "btn_lthumb", 0.2367f, 0.4400f, 0.0975f, 0.1462f },
    { "btn_menu", 0.5758f, 0.4363f, 0.0524f, 0.0785f },
    { "btn_rb", 0.7575f, 0.1600f, 0.2546f, 0.1408f },
    { "btn_rstick", 0.6367f, 0.6600f, 0.1264f, 0.1896f },
    { "btn_rt", 0.7842f, 0.0725f, 0.1101f, 0.1408f },
    { "btn_rthumb", 0.6367f, 0.6600f, 0.0975f, 0.1462f },
    { "btn_share", 0.5000f, 0.4975f, 0.0614f, 0.0542f },
    { "btn_view", 0.4250f, 0.4363f, 0.0506f, 0.0785f },
    { "btn_x", 0.6967f, 0.4325f, 0.0758f, 0.1137f },
    { "btn_y", 0.7700f, 0.3337f, 0.0758f, 0.1165f },
};

InputWindow::InputWindow()
    : ui::BaseWindow("Input Mapper", ui::WindowFeature::NoPin, ImVec2(480.0f, 400.0f)) {
}

void InputWindow::Initialize() {
    m_is_open = false;
    m_recording_index = -1;
    m_textures_loaded = false;
    m_visualizer_button_count = 0;
}

void InputWindow::RenderContent(bool interactive) {
    const float win_w = ImGui::GetContentRegionAvail().x;
    const float win_h = ImGui::GetContentRegionAvail().y;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.067f, 0.067f, 0.067f, m_bg_alpha));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(1.0f, 1.0f, 1.0f, 0.45f));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 12.0f);

    bool content_ok = ImGui::BeginChild("InputContent", ImVec2(win_w, win_h), false, ImGuiWindowFlags_AlwaysUseWindowPadding);

    if (content_ok) {
        ImGui::SetCursorPosY(10.0f);
        // Draw the window close decoration
        {
            ImVec2 orig_cursor = ImGui::GetCursorPos();
            float avail_x = ImGui::GetContentRegionAvail().x;
            float right_boundary = ImGui::GetCursorPosX() + avail_x + 14.0f;
            RenderWindowDecorations(interactive, right_boundary, 4.0f);
            ImGui::SetCursorPos(orig_cursor);
        }

        // Draw the icon in the panel font (which has icons merged)
        if (dover::shared::g_font_panel) {
            ImGui::PushFont(dover::shared::g_font_panel);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.68f, 0.84f, 1.00f));
        ImGui::Text("%s", ICON_PANEL_INPUTMAP);
        ImGui::PopStyleColor();
        if (dover::shared::g_font_panel) {
            ImGui::PopFont();
        }

        ImGui::SameLine(0.0f, 6.0f);

        // Draw the title text in the preview bold font
        if (dover::shared::g_fonts_preview_bold[3]) {
            ImGui::PushFont(dover::shared::g_fonts_preview_bold[3]);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.68f, 0.84f, 1.00f));
        ImGui::Text("Controller");
        ImGui::PopStyleColor();
        if (dover::shared::g_fonts_preview_bold[3]) {
            ImGui::PopFont();
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 2.0f);
        ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.290f, 0.380f, 0.520f, 0.15f));
        ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.227f, 0.267f, 0.329f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_TabUnfocused, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, ImVec4(0.227f, 0.267f, 0.329f, 0.70f));

        if (ImGui::BeginTabBar("InputTabs")) {
            if (ImGui::BeginTabItem("Remapper")) {
                RenderRemapper(interactive);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Visualizer")) {
                RenderVisualizer();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar();
    }
    
    ImGui::EndChild();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(5);
}

void InputWindow::RenderRemapper(bool interactive) {
    (void)interactive;
    ImGui::TextDisabled("Map gamepad buttons to keyboard keys. Press ESC to unbind.");
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        struct BtnInfo {
            const char* icon;
            const char* name;
        };

        const BtnInfo btn_names[18] = {
            { ICON_GAMEPAD_DPAD_UP, "D-Pad Up" },
            { ICON_GAMEPAD_DPAD_DOWN, "D-Pad Down" },
            { ICON_GAMEPAD_DPAD_LEFT, "D-Pad Left" },
            { ICON_GAMEPAD_DPAD_RIGHT, "D-Pad Right" },
            { ICON_GAMEPAD_MENU, "Start" },
            { ICON_GAMEPAD_VIEW, "Back" },
            { ICON_GAMEPAD_LSTICK_CLICK, "Left Thumb" },
            { ICON_GAMEPAD_RSTICK_CLICK, "Right Thumb" },
            { ICON_GAMEPAD_LBUMPER, "Left Shoulder" },
            { ICON_GAMEPAD_RBUMPER, "Right Shoulder" },
            { ICON_GAMEPAD_HOME, "Guide" },
            { nullptr, "Unknown" },
            { ICON_GAMEPAD_A, "A Button" },
            { ICON_GAMEPAD_B, "B Button" },
            { ICON_GAMEPAD_X, "X Button" },
            { ICON_GAMEPAD_Y, "Y Button" },
            { ICON_GAMEPAD_LTRIGGER, "Left Trigger" },
            { ICON_GAMEPAD_RTRIGGER, "Right Trigger" }
        };

        // Use two columns for compactness
        ImGui::BeginTable("GamepadMapTable", 2, ImGuiTableFlags_None);
        for (int i = 0; i < 18; ++i) {
            if (i == 10 || i == 11) continue;
            
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            
            // Draw premium row background
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float avail_w = ImGui::GetContentRegionAvail().x;
            float row_height = 32.0f;
            
            bool hovered = ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + avail_w, pos.y + row_height));
            ImVec4 bg_color = hovered ? ImVec4(1.0f, 1.0f, 1.0f, 0.04f) : ImVec4(0,0,0,0);
            if (m_recording_index == i) bg_color = ImVec4(0.118f, 0.478f, 0.812f, 0.15f);
            
            ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + avail_w - 4.0f, pos.y + row_height), ImGui::GetColorU32(bg_color), 4.0f);
            
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
            
            // Icon + Label
            ImGui::Text("%s  %s", btn_names[i].icon, btn_names[i].name);
            
            ImGui::SameLine();
            ImGui::SetCursorPosX(pos.x - ImGui::GetWindowPos().x + avail_w - 150.0f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f);
            
            char btn_label[64];
            if (m_recording_index == i) {
                snprintf(btn_label, sizeof(btn_label), "[ Press Key ]");
                // Check hardware input
                for (int k = 0x08; k <= 0xFE; k++) {
                    if (k == VK_SHIFT || k == VK_CONTROL || k == VK_MENU || 
                        k == VK_LSHIFT || k == VK_RSHIFT || 
                        k == VK_LCONTROL || k == VK_RCONTROL || 
                        k == VK_LMENU || k == VK_RMENU) {
                        continue;
                    }
                    if (dover::overlay::IsHardwareKeyPressed(k)) {
                        auto& map = GetOverlayConfig().gamepad_to_vk_map[i];
                        if (k == VK_ESCAPE) {
                            map.vk_code = 0; // Clear mapping
                            map.modifier_ctrl = false;
                            map.modifier_shift = false;
                            map.modifier_alt = false;
                        } else {
                            map.vk_code = static_cast<uint8_t>(k);
                            map.modifier_ctrl = dover::overlay::IsHardwareKeyPressed(VK_CONTROL);
                            map.modifier_shift = dover::overlay::IsHardwareKeyPressed(VK_SHIFT);
                            map.modifier_alt = dover::overlay::IsHardwareKeyPressed(VK_MENU);
                        }
                        m_recording_index = -1;
                        dover::shared::GameStorage::Get().SaveConfig();
                        break;
                    }
                }
            } else {
                auto& map = GetOverlayConfig().gamepad_to_vk_map[i];
                uint8_t vk = map.vk_code;
                if (vk == 0) {
                    snprintf(btn_label, sizeof(btn_label), "Unbound");
                } else {
                    char label[64] = "";
                    int len = 0;
                    constexpr int max_len = sizeof(label);

                    auto AppendStr = [&](const char* str) {
                        if (len >= max_len - 1) return;
                        int written = snprintf(label + len, max_len - len, "%s", str);
                        if (written > 0) {
                            len += (written < (max_len - len)) ? written : (max_len - len - 1);
                        }
                    };

                    auto AppendChar = [&](char c) {
                        if (len >= max_len - 1) return;
                        int written = snprintf(label + len, max_len - len, "%c", c);
                        if (written > 0) {
                            len += (written < (max_len - len)) ? written : (max_len - len - 1);
                        }
                    };

                    auto AppendHex = [&](uint8_t val) {
                        if (len >= max_len - 1) return;
                        int written = snprintf(label + len, max_len - len, "0x%02X", val);
                        if (written > 0) {
                            len += (written < (max_len - len)) ? written : (max_len - len - 1);
                        }
                    };

                    if (map.modifier_ctrl)  AppendStr("Ctrl + ");
                    if (map.modifier_shift) AppendStr("Shift + ");
                    if (map.modifier_alt)   AppendStr("Alt + ");

                    if (vk >= 'A' && vk <= 'Z')       AppendChar((char)vk);
                    else if (vk >= '0' && vk <= '9')  AppendChar((char)vk);
                    else if (vk == VK_SPACE)          AppendStr("Space");
                    else if (vk == VK_RETURN)         AppendStr("Enter");
                    else if (vk == VK_TAB)            AppendStr("Tab");
                    else                              AppendHex(vk);

                    snprintf(btn_label, sizeof(btn_label), "%s", label);
                }
            }
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
            if (m_recording_index == i) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.68f, 0.84f, 1.00f));
            } else if (GetOverlayConfig().gamepad_to_vk_map[i].vk_code == 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            }
            
            if (ImGui::Button(btn_label, ImVec2(136.0f, 24.0f))) {
                m_recording_index = i;
            }
            
            ImGui::PopStyleColor(4);
            
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
            
            ImGui::PopID();
        }
        ImGui::EndTable();
}

void InputWindow::LoadGamepadTextures() {
    if (m_textures_loaded) return;
    m_textures_loaded = true;

    if (!assets::AssetStorage::Get().IsInitialized()) return;
    auto& assets = assets::AssetStorage::Get().GetAssets();
    
    ID3D11Device* dx11 = GetDx11Device();
    IDirect3DDevice9* dx9 = GetDx9Device();
    if (!dx11 && !dx9) return;

    for (auto& asset : assets) {
        if (asset.name.rfind("gamepad/", 0) != 0) continue; // Only load gamepad textures
        if (asset.texture_id) continue;

        if (dx11) {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = asset.width;
            desc.Height = asset.height;
            desc.MipLevels = 0; // Allocate full mipmap chain
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; // Required for GenerateMips
            desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

            ID3D11Texture2D* pTexture = nullptr;
            if (SUCCEEDED(dx11->CreateTexture2D(&desc, nullptr, &pTexture))) {
                ID3D11DeviceContext* context = GetDx11Context();
                if (context) {
                    // Upload level 0 data
                    context->UpdateSubresource(pTexture, 0, nullptr, asset.rgba_data, asset.width * 4, 0);

                    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                    srvDesc.Texture2D.MipLevels = static_cast<UINT>(-1); // Expose all mip levels
                    
                    ID3D11ShaderResourceView* pSRV = nullptr;
                    if (SUCCEEDED(dx11->CreateShaderResourceView(pTexture, &srvDesc, &pSRV))) {
                        context->GenerateMips(pSRV);
                        asset.texture_id = pSRV;
                    }
                }
                pTexture->Release();
            }
        } else if (dx9) {
            IDirect3DTexture9* pTexture = nullptr;
            // Use 0 for mip levels and AUTOGENMIPMAP flag
            if (SUCCEEDED(dx9->CreateTexture(asset.width, asset.height, 0, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pTexture, nullptr))) {
                D3DLOCKED_RECT rect;
                if (SUCCEEDED(pTexture->LockRect(0, &rect, nullptr, 0))) {
                    const uint8_t* src = asset.rgba_data;
                    uint8_t* dest = static_cast<uint8_t*>(rect.pBits);
                    for (uint32_t y = 0; y < asset.height; ++y) {
                        for (uint32_t x = 0; x < asset.width; ++x) {
                            dest[x * 4 + 0] = src[x * 4 + 2]; // B
                            dest[x * 4 + 1] = src[x * 4 + 1]; // G
                            dest[x * 4 + 2] = src[x * 4 + 0]; // R
                            dest[x * 4 + 3] = src[x * 4 + 3]; // A
                        }
                        src += asset.width * 4;
                        dest += rect.Pitch;
                    }
                    pTexture->UnlockRect(0);
                    asset.texture_id = pTexture;
                }
            }
        }
    }
    
    InitializeVisualizerButtons();
}

void InputWindow::InitializeVisualizerButtons() {
    m_visualizer_button_count = 0;
    
    auto GetXInputFlag = [](const char* name, bool& is_trigger) -> unsigned int {
        is_trigger = false;
        if (strcmp(name, "btn_a") == 0) return XINPUT_GAMEPAD_A;
        if (strcmp(name, "btn_b") == 0) return XINPUT_GAMEPAD_B;
        if (strcmp(name, "btn_x") == 0) return XINPUT_GAMEPAD_X;
        if (strcmp(name, "btn_y") == 0) return XINPUT_GAMEPAD_Y;
        if (strcmp(name, "btn_dpad_up") == 0) return XINPUT_GAMEPAD_DPAD_UP;
        if (strcmp(name, "btn_dpad_down") == 0) return XINPUT_GAMEPAD_DPAD_DOWN;
        if (strcmp(name, "btn_dpad_left") == 0) return XINPUT_GAMEPAD_DPAD_LEFT;
        if (strcmp(name, "btn_dpad_right") == 0) return XINPUT_GAMEPAD_DPAD_RIGHT;
        if (strcmp(name, "btn_lb") == 0) return XINPUT_GAMEPAD_LEFT_SHOULDER;
        if (strcmp(name, "btn_rb") == 0) return XINPUT_GAMEPAD_RIGHT_SHOULDER;
        if (strcmp(name, "btn_menu") == 0) return XINPUT_GAMEPAD_START;
        if (strcmp(name, "btn_view") == 0) return XINPUT_GAMEPAD_BACK;
        if (strcmp(name, "btn_lthumb") == 0) return XINPUT_GAMEPAD_LEFT_THUMB;
        if (strcmp(name, "btn_rthumb") == 0) return XINPUT_GAMEPAD_RIGHT_THUMB;
        if (strcmp(name, "btn_lt") == 0) { is_trigger = true; return 0; }
        if (strcmp(name, "btn_rt") == 0) { is_trigger = true; return 0; }
        return 0;
    };
    
    for (int i = 0; i < sizeof(g_button_offsets)/sizeof(g_button_offsets[0]); ++i) {
        const auto& offset = g_button_offsets[i];
        
        char asset_name[64];
        snprintf(asset_name, sizeof(asset_name), "gamepad/%s", offset.name);
        auto* btn_asset = assets::AssetStorage::Get().GetAsset(asset_name);
        
        if (btn_asset && btn_asset->texture_id) {
            ButtonRenderData data = {};
            data.name = offset.name;
            data.x_pct = offset.x_pct;
            data.y_pct = offset.y_pct;
            data.w_pct = offset.w_pct;
            data.h_pct = offset.h_pct;
            data.tex_id = btn_asset->texture_id;
            data.xinput_flag = GetXInputFlag(offset.name, data.is_trigger);
            
            m_visualizer_buttons[m_visualizer_button_count++] = data;
        }
    }
}

void InputWindow::RenderVisualizer() {
    auto& cfg = GetOverlayConfig();
    if (cfg.gamepad_hud_scale > 1.0f) cfg.gamepad_hud_scale = 1.0f;
    
    // Top control bar
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Show In-Game");
    ImGui::SameLine(0.0f, 8.0f);
    
    ImFont* icon_font = dover::shared::g_font_panel ? dover::shared::g_font_panel : dover::shared::g_font_gui;
    const char* toggle_icon = cfg.show_gamepad_hud ? ICON_TOGGLE_ON : ICON_TOGGLE_OFF;
    ImVec4 icon_color = cfg.show_gamepad_hud ? ImVec4(0.118f, 0.478f, 0.812f, 1.00f) : ImVec4(0.40f, 0.42f, 0.48f, 0.80f);
    
    ImGui::PushFont(icon_font);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0,0,0,0));
    
    ImVec2 toggle_cursor_pos = ImGui::GetCursorPos();
    toggle_cursor_pos.y += 5.0f;
    ImGui::SetCursorPos(toggle_cursor_pos);
    
    ImVec2 cursor_screen = ImGui::GetCursorScreenPos();
    ImVec2 icon_size = icon_font->CalcTextSizeA(icon_font->FontSize, FLT_MAX, 0.0f, toggle_icon);
    
    bool clicked = ImGui::Button("##hud_toggle", ImVec2(icon_size.x + 8.0f, icon_size.y));
    bool hovered = ImGui::IsItemHovered();
    
    if (hovered) {
        icon_color = cfg.show_gamepad_hud ? ImVec4(0.200f, 0.569f, 0.902f, 1.00f) : ImVec4(0.60f, 0.62f, 0.68f, 1.00f);
    }
    
    ImGui::GetWindowDrawList()->AddText(icon_font, icon_font->FontSize, 
        ImVec2(cursor_screen.x + 4.0f, cursor_screen.y + 2.0f), 
        ImGui::GetColorU32(icon_color), toggle_icon);
        
    ImGui::PopStyleColor(3);
    ImGui::PopFont();
    
    if (clicked) {
        cfg.show_gamepad_hud = !cfg.show_gamepad_hud;
        dover::shared::GameStorage::Get().SaveConfig();
    }
    
    if (cfg.show_gamepad_hud) {
        ImGui::SameLine(0.0f, 16.0f);
        
        // Position Dropdown (Notes toolbar style)
        ImGui::TextDisabled("Position:");
        ImGui::SameLine(0.0f, 8.0f);
        
        ImGui::SetNextItemWidth(110.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.20f, 0.20f, 0.25f, 0.60f));
        
        const char* pos_names[] = { "Top Left", "Top Center", "Top Right", "Bottom Left", "Bottom Center", "Bottom Right" };
        if (ImGui::BeginCombo("##hud_pos", pos_names[cfg.gamepad_hud_position], ImGuiComboFlags_NoArrowButton)) {
            for (int i = 0; i < 6; ++i) {
                bool is_selected = (cfg.gamepad_hud_position == i);
                if (ImGui::Selectable(pos_names[i], is_selected)) {
                    cfg.gamepad_hud_position = i;
                    dover::shared::GameStorage::Get().SaveConfig();
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(2);
        
        // Size Slim Slider
        ImGui::SameLine(0.0f, 20.0f);
        ImGui::TextDisabled("Size:");
        ImGui::SameLine(0.0f, 8.0f);
        
        ImVec2 slider_cursor_pos = ImGui::GetCursorPos();
        slider_cursor_pos.y += 2.0f;
        ImGui::SetCursorPos(slider_cursor_pos);
        
        float slider_width = 80.0f;
        ImVec2 slider_pos = ImGui::GetCursorScreenPos();
        
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,          ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,   ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,    ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,       ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
        
        ImGui::SetNextItemWidth(slider_width);
        float scale_val = cfg.gamepad_hud_scale;
        if (ImGui::SliderFloat("##hud_scale", &scale_val, 0.2f, 1.0f, "")) {
            cfg.gamepad_hud_scale = scale_val;
            dover::shared::GameStorage::Get().SaveConfig();
        }
        bool slider_active = ImGui::IsItemActive();
        bool slider_hovered = ImGui::IsItemHovered();
        
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar();
        
        float frame_height = ImGui::GetFrameHeight();
        float grab_center_y = slider_pos.y + frame_height * 0.5f;
        float norm_val = (cfg.gamepad_hud_scale - 0.2f) / (1.0f - 0.2f);
        float grab_center_x = slider_pos.x + norm_val * slider_width;
        
        ImVec4 grab_color = ImVec4(1.00f, 1.00f, 1.00f, 0.90f);
        if (slider_active) grab_color = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        else if (slider_hovered) grab_color = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
        
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
        
        ImGui::SameLine(0.0f, 8.0f);
        ImVec2 text_cursor_pos = ImGui::GetCursorPos();
        text_cursor_pos.y += 2.0f;
        ImGui::SetCursorPos(text_cursor_pos);
        ImGui::TextDisabled("%.0f%%", cfg.gamepad_hud_scale * 100.0f);
    }
    
    ImGui::Separator();
    
    LoadGamepadTextures();
    
    auto* chassis_asset = assets::AssetStorage::Get().GetAsset("gamepad/chassis");
    if (!chassis_asset || !chassis_asset->texture_id) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Chassis texture not found in assets.pak");
        return;
    }
    
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float avail_w = ImGui::GetContentRegionAvail().x;
    float avail_h = ImGui::GetContentRegionAvail().y;
    if (avail_w < 50.0f) avail_w = 50.0f;
    if (avail_h < 50.0f) avail_h = 50.0f;

    float aspect = (float)chassis_asset->width / (float)chassis_asset->height;
    
    // Max width is 75% of original resolution to keep it extremely crisp
    float max_w = (float)chassis_asset->width * 0.75f;
    float max_h = max_w / aspect;
    
    float render_w = avail_w;
    float render_h = avail_w / aspect;
    
    if (render_h > avail_h) {
        render_h = avail_h;
        render_w = render_h * aspect;
    }
    
    if (render_w > max_w) {
        render_w = max_w;
        render_h = max_h;
    }
    
    // Center it horizontally and vertically
    if (avail_w > render_w) {
        pos.x += (avail_w - render_w) * 0.5f;
    }
    if (avail_h > render_h) {
        pos.y += (avail_h - render_h) * 0.5f;
    }
    
    // UI Color Palette
    constexpr ImU32 COLOR_CHASSIS = IM_COL32(30, 32, 38, 230); // Dark Charcoal frosted glass
    constexpr ImU32 COLOR_IDLE    = IM_COL32(180, 185, 195, 255); // Silver/Gray idle buttons
    constexpr ImU32 COLOR_PRESSED = IM_COL32(50, 150, 255, 255); // Proven Neon Blue
    
    ImGui::GetWindowDrawList()->AddImage(chassis_asset->texture_id, pos, ImVec2(pos.x + render_w, pos.y + render_h), ImVec2(0,0), ImVec2(1,1), COLOR_CHASSIS);
    
    g_allow_xinput = true;
    XINPUT_STATE state = {};
    for (DWORD i = 0; i < 4; ++i) {
        if (XInputGetState(i, &state) == ERROR_SUCCESS) {
            break;
        }
    }
    g_allow_xinput = false;
    
    WORD b = state.Gamepad.wButtons;

    for (int i = 0; i < m_visualizer_button_count; ++i) {
        const auto& btn = m_visualizer_buttons[i];
        
        bool pressed = false;
        if (btn.is_trigger) {
            if (strcmp(btn.name, "btn_lt") == 0) pressed = state.Gamepad.bLeftTrigger > 30;
            else if (strcmp(btn.name, "btn_rt") == 0) pressed = state.Gamepad.bRightTrigger > 30;
        } else {
            pressed = (b & btn.xinput_flag) != 0;
        }
        
        float btn_w = btn.w_pct * render_w;
        float btn_h = btn.h_pct * render_h;
        float btn_x = pos.x + (btn.x_pct * render_w) - (btn_w * 0.5f);
        float btn_y = pos.y + (btn.y_pct * render_h) - (btn_h * 0.5f);
        
        // Deflection for sticks applies to the THUMB CAP
        if (strcmp(btn.name, "btn_lthumb") == 0) {
            float dx = (float)state.Gamepad.sThumbLX / 32767.0f;
            float dy = (float)state.Gamepad.sThumbLY / -32767.0f;
            btn_x += dx * (btn_w * 0.35f);
            btn_y += dy * (btn_h * 0.35f);
        } else if (strcmp(btn.name, "btn_rthumb") == 0) {
            float dx = (float)state.Gamepad.sThumbRX / 32767.0f;
            float dy = (float)state.Gamepad.sThumbRY / -32767.0f;
            btn_x += dx * (btn_w * 0.35f);
            btn_y += dy * (btn_h * 0.35f);
        }
        
        ImU32 tint = pressed ? COLOR_PRESSED : COLOR_IDLE;
        if (pressed) {
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(btn_x + btn_w/2, btn_y + btn_h/2), btn_w * 0.6f, (tint & 0x00FFFFFF) | 0x40000000, 24);
        }
        
        ImGui::GetWindowDrawList()->AddImage(btn.tex_id, ImVec2(btn_x, btn_y), ImVec2(btn_x + btn_w, btn_y + btn_h), ImVec2(0,0), ImVec2(1,1), tint);
    }
    
    // Reserve space for layout
    ImGui::Dummy(ImVec2(avail_w, avail_h));
}

void InputWindow::RenderGamepadOverlay() {
    auto& cfg = GetOverlayConfig();
    if (!cfg.show_gamepad_hud) return;
    
    float scale = cfg.gamepad_hud_scale;
    if (scale > 1.0f) scale = 1.0f;
    if (scale < 0.2f) scale = 0.2f;
    
    LoadGamepadTextures();
    auto* chassis = assets::AssetStorage::Get().GetAsset("gamepad/chassis");
    if (!chassis || !chassis->texture_id) return;
    
    // Default base width for HUD is 500px, scaled by config
    float max_w = (float)chassis->width * 0.75f;
    float render_w = 400.0f * scale;
    if (render_w > max_w) render_w = max_w;
    
    float aspect = (float)chassis->width / (float)chassis->height;
    float render_h = render_w / aspect;
    
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImVec2 pos(20.0f, 20.0f); // Default padding
    
    switch (cfg.gamepad_hud_position) {
        case 0: pos.x = 20.0f; pos.y = 20.0f; break; // Top Left
        case 1: pos.x = (display.x - render_w) * 0.5f; pos.y = 20.0f; break; // Top Center
        case 2: pos.x = display.x - render_w - 20.0f; pos.y = 20.0f; break; // Top Right
        case 3: pos.x = 20.0f; pos.y = display.y - render_h - 20.0f; break; // Bottom Left
        case 4: pos.x = (display.x - render_w) * 0.5f; pos.y = display.y - render_h - 20.0f; break; // Bottom Center
        case 5: pos.x = display.x - render_w - 20.0f; pos.y = display.y - render_h - 20.0f; break; // Bottom Right
    }
    
    auto* dl = ImGui::GetBackgroundDrawList();
    constexpr ImU32 COLOR_CHASSIS = IM_COL32(30, 32, 38, 230);
    constexpr ImU32 COLOR_IDLE    = IM_COL32(180, 185, 195, 200); // Slightly more transparent for HUD
    constexpr ImU32 COLOR_PRESSED = IM_COL32(50, 150, 255, 255);
    
    dl->AddImage(chassis->texture_id, pos, ImVec2(pos.x + render_w, pos.y + render_h), ImVec2(0,0), ImVec2(1,1), COLOR_CHASSIS);
    
    g_allow_xinput = true;
    XINPUT_STATE state = {};
    static DWORD s_active_index = 0;
    static int s_poll_timer = 0;
    
    bool connected = (XInputGetState(s_active_index, &state) == ERROR_SUCCESS);
    if (!connected) {
        // Throttle disconnected port polling to prevent massive 2-5ms frame-drops per port
        if (++s_poll_timer > 60) {
            s_poll_timer = 0;
            for (DWORD i = 0; i < 4; ++i) {
                if (i != s_active_index && XInputGetState(i, &state) == ERROR_SUCCESS) {
                    s_active_index = i;
                    break;
                }
            }
        }
    } else {
        s_poll_timer = 0;
    }
    g_allow_xinput = false;
    
    WORD b = state.Gamepad.wButtons;
    for (int i = 0; i < m_visualizer_button_count; ++i) {
        const auto& btn = m_visualizer_buttons[i];
        bool pressed = btn.is_trigger ? (strcmp(btn.name, "btn_lt") == 0 ? state.Gamepad.bLeftTrigger > 30 : state.Gamepad.bRightTrigger > 30) : ((b & btn.xinput_flag) != 0);
        
        float btn_w = btn.w_pct * render_w;
        float btn_h = btn.h_pct * render_h;
        float btn_x = pos.x + (btn.x_pct * render_w) - (btn_w * 0.5f);
        float btn_y = pos.y + (btn.y_pct * render_h) - (btn_h * 0.5f);
        
        if (strcmp(btn.name, "btn_lthumb") == 0) {
            btn_x += ((float)state.Gamepad.sThumbLX / 32767.0f) * (btn_w * 0.35f);
            btn_y += ((float)state.Gamepad.sThumbLY / -32767.0f) * (btn_h * 0.35f);
        } else if (strcmp(btn.name, "btn_rthumb") == 0) {
            btn_x += ((float)state.Gamepad.sThumbRX / 32767.0f) * (btn_w * 0.35f);
            btn_y += ((float)state.Gamepad.sThumbRY / -32767.0f) * (btn_h * 0.35f);
        }
        
        ImU32 tint = pressed ? COLOR_PRESSED : COLOR_IDLE;
        if (pressed) dl->AddCircleFilled(ImVec2(btn_x + btn_w/2, btn_y + btn_h/2), btn_w * 0.6f, (tint & 0x00FFFFFF) | 0x40000000, 24);
        dl->AddImage(btn.tex_id, ImVec2(btn_x, btn_y), ImVec2(btn_x + btn_w, btn_y + btn_h), ImVec2(0,0), ImVec2(1,1), tint);
    }
}

InputWindow& GetInputWindow() {
    static InputWindow instance;
    return instance;
}

} // namespace dover::overlay::input
