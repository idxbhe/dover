#include "overlay/crosshair/crosshair_window.h"
#include "overlay/assets/asset_storage.h"
#include "overlay/dx11_hook.h"
#include "overlay/dx9_hook.h"
#include "shared/log.h"
#include "shared/icons.h"
#include "shared/theme.h"
#include "shared/game_storage.h"

#include <d3d11.h>
#include <d3d9.h>

namespace dover::overlay {

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

        ImFont* icon_font = dover::shared::g_font_panel ? dover::shared::g_font_panel : dover::shared::g_font_gui;
        float icon_font_size = icon_font ? icon_font->FontSize : 18.0f; 
        
        if (icon_font) {
            ImVec2 icon_size = icon_font->CalcTextSizeA(icon_font_size, FLT_MAX, 0.0f, icon);
            
            float icon_x = float(int(pos.x + avail_w - icon_size.x - 8.0f));
            float icon_y = float(int(pos.y + (height - icon_size.y) * 0.5f)) + 2.0f;
            
            dl->AddText(icon_font, icon_font_size, 
                        ImVec2(icon_x, icon_y), 
                        ImGui::GetColorU32(icon_color), icon);
        }

        return clicked;
    }
} // namespace

namespace dover::overlay::crosshair {

CrosshairWindow& GetCrosshairWindow() {
    static CrosshairWindow instance;
    return instance;
}

CrosshairWindow::CrosshairWindow() 
    : ui::BaseWindow("Crosshairs", ui::WindowFeature::NoPin, ImVec2(600, 450)) {
}

void CrosshairWindow::PreRender(bool /*interactive*/) {
}

void CrosshairWindow::Initialize() {
    assets::AssetStorage::Get().Initialize();
}

void CrosshairWindow::Shutdown() {
    auto& crosshairs = assets::AssetStorage::Get().GetAssets();
    for (size_t i = 0; i < crosshairs.size(); ++i) {
        if (crosshairs[i].name.rfind("gamepad/", 0) == 0) continue;
        if (crosshairs[i].texture_id) {
            if (GetDx11Device()) {
                auto* srv = static_cast<ID3D11ShaderResourceView*>(crosshairs[i].texture_id);
                srv->Release();
            } else if (GetDx9Device()) {
                auto* tex = static_cast<IDirect3DTexture9*>(crosshairs[i].texture_id);
                tex->Release();
            }
            crosshairs[i].texture_id = nullptr;
        }
    }
    m_textures_loaded = false;
}

void CrosshairWindow::RenderCrosshairOverlay() {
    if (!m_textures_loaded) {
        m_textures_loaded = true; // Strict guard: ONLY TRY ONCE!
        
        if (!assets::AssetStorage::Get().IsInitialized()) {
            return; // If PAK is missing, do not attempt to load textures
        }
        
        auto& crosshairs = assets::AssetStorage::Get().GetAssets();
        
        ID3D11Device* dx11 = GetDx11Device();
        IDirect3DDevice9* dx9 = GetDx9Device();
        
        if (dx11) {
            dover::shared::LogInfo("Loading Crosshair Textures (DX11)...");
            for (size_t i = 0; i < crosshairs.size(); ++i) {
                if (crosshairs[i].name.rfind("gamepad/", 0) == 0) continue;
                
                D3D11_TEXTURE2D_DESC desc = {};
                desc.Width = crosshairs[i].width;
                desc.Height = crosshairs[i].height;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

                D3D11_SUBRESOURCE_DATA subData = {};
                subData.pSysMem = crosshairs[i].rgba_data;
                subData.SysMemPitch = desc.Width * 4;

                ID3D11Texture2D* pTexture = nullptr;
                if (SUCCEEDED(dx11->CreateTexture2D(&desc, &subData, &pTexture))) {
                    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                    srvDesc.Texture2D.MipLevels = 1;
                    
                    ID3D11ShaderResourceView* pSRV = nullptr;
                    if (SUCCEEDED(dx11->CreateShaderResourceView(pTexture, &srvDesc, &pSRV))) {
                        crosshairs[i].texture_id = pSRV;
                    }
                    pTexture->Release();
                }
            }
        } else if (dx9) {
            dover::shared::LogInfo("Loading Crosshair Textures (DX9)...");
            for (size_t i = 0; i < crosshairs.size(); ++i) {
                if (crosshairs[i].name.rfind("gamepad/", 0) == 0) continue;
                
                IDirect3DTexture9* pTexture = nullptr;
                int w = crosshairs[i].width;
                int h = crosshairs[i].height;
                if (SUCCEEDED(dx9->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pTexture, nullptr))) {
                    D3DLOCKED_RECT rect;
                    if (SUCCEEDED(pTexture->LockRect(0, &rect, nullptr, 0))) {
                        uint8_t* dest = (uint8_t*)rect.pBits;
                        const uint8_t* src = crosshairs[i].rgba_data;
                        for (int y = 0; y < h; y++) {
                            for (int x = 0; x < w; x++) {
                                // RGBA to BGRA (DX9 standard pixel format)
                                dest[y * rect.Pitch + x * 4 + 0] = src[(y * w + x) * 4 + 2]; // B
                                dest[y * rect.Pitch + x * 4 + 1] = src[(y * w + x) * 4 + 1]; // G
                                dest[y * rect.Pitch + x * 4 + 2] = src[(y * w + x) * 4 + 0]; // R
                                dest[y * rect.Pitch + x * 4 + 3] = src[(y * w + x) * 4 + 3]; // A
                            }
                        }
                        pTexture->UnlockRect(0);
                        crosshairs[i].texture_id = pTexture;
                    } else {
                        pTexture->Release();
                    }
                }
            }
        }
    }

    if (!m_active) return;
    
    int total_crosshairs = static_cast<int>(dover::overlay::assets::AssetStorage::Get().GetCrosshairs().size());
    if (total_crosshairs == 0) return;
    if (m_selected_index < 0 || m_selected_index >= total_crosshairs) return;

    auto* asset = dover::overlay::assets::AssetStorage::Get().GetCrosshairs()[m_selected_index];
    if (!asset) return;

    void* tex_id = asset->texture_id;
    if (!tex_id) return;

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    ImVec2 center = ImGui::GetIO().DisplaySize;
    center.x *= 0.5f;
    center.y *= 0.5f;

    center.x += m_pos_x;
    center.y += m_pos_y;

    // Scaling logic: Max scale (3.0x) in UI equals the original texture size.
    float w = (float)asset->width * (m_scale / 3.0f);
    float h = (float)asset->height * (m_scale / 3.0f);

    ImVec2 p_min(center.x - w * 0.5f, center.y - h * 0.5f);
    ImVec2 p_max(center.x + w * 0.5f, center.y + h * 0.5f);

    ImVec4 final_color = m_color;
    final_color.w *= m_opacity;
    ImU32 final_color_u32 = ImGui::ColorConvertFloat4ToU32(final_color);

    if (m_outline_enabled) {
        ImVec4 final_outline = m_outline_color;
        final_outline.w *= m_opacity;
        ImU32 outline_col = ImGui::ColorConvertFloat4ToU32(final_outline);
        // Shadow Stamping (The Carmack Way): 4 directional draw calls
        draw_list->AddImage(tex_id, ImVec2(p_min.x - 1, p_min.y), ImVec2(p_max.x - 1, p_max.y), ImVec2(0, 0), ImVec2(1, 1), outline_col);
        draw_list->AddImage(tex_id, ImVec2(p_min.x + 1, p_min.y), ImVec2(p_max.x + 1, p_max.y), ImVec2(0, 0), ImVec2(1, 1), outline_col);
        draw_list->AddImage(tex_id, ImVec2(p_min.x, p_min.y - 1), ImVec2(p_max.x, p_max.y - 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
        draw_list->AddImage(tex_id, ImVec2(p_min.x, p_min.y + 1), ImVec2(p_max.x, p_max.y + 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
        // Diagonal stamps for thicker look
        draw_list->AddImage(tex_id, ImVec2(p_min.x - 1, p_min.y - 1), ImVec2(p_max.x - 1, p_max.y - 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
        draw_list->AddImage(tex_id, ImVec2(p_min.x + 1, p_min.y + 1), ImVec2(p_max.x + 1, p_max.y + 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
        draw_list->AddImage(tex_id, ImVec2(p_min.x - 1, p_min.y + 1), ImVec2(p_max.x - 1, p_max.y + 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
        draw_list->AddImage(tex_id, ImVec2(p_min.x + 1, p_min.y - 1), ImVec2(p_max.x + 1, p_max.y - 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
    }

    // Main Draw Call
    draw_list->AddImage(tex_id, p_min, p_max, ImVec2(0, 0), ImVec2(1, 1), final_color_u32);
}

void CrosshairWindow::RenderContent(bool interactive) {
    if (!interactive) return;

    int total_crosshairs = static_cast<int>(dover::overlay::assets::AssetStorage::Get().GetCrosshairs().size());
    if (total_crosshairs == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "assets.pak not found or no crosshairs.");
        ImGui::Text("Please ensure the asset pak is in the same directory as the DLL.");
        return;
    }

    ImGui::Columns(2, "CrosshairCols", false);
    ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.382f); // Fibonacci ratio: 38.2% for Preview/List, 61.8% for Options

    const float win_h = ImGui::GetContentRegionAvail().y;
    // Fibonacci Vertical Split: Preview takes smaller ratio (23.6% of height) to make grid list taller
    const float preview_h = win_h * 0.236f;

    // Live Preview Box Style
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.043f, 0.051f, 0.066f, m_bg_alpha)); // Deep obsidian background
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 2.0f);
    
    ImGui::BeginChild("PreviewBox", ImVec2(0, preview_h), true);
    if (total_crosshairs > 0 && m_selected_index >= 0 && m_selected_index < total_crosshairs) {
        auto* asset = dover::overlay::assets::AssetStorage::Get().GetCrosshairs()[m_selected_index];
        void* tex_id = asset ? asset->texture_id : nullptr;
        if (tex_id) {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImVec2 center = ImVec2(ImGui::GetCursorScreenPos().x + avail.x * 0.5f, 
                                   ImGui::GetCursorScreenPos().y + avail.y * 0.5f);
            
            // Preview Box Scaling: Max scale (3.0x) matches full preview size (64x64)
            float pw = 64.0f * (m_scale / 3.0f);
            float ph = 64.0f * (m_scale / 3.0f);
            
            // Perfect color matching for dark Obsidian theme base
            ImDrawList* box_dl = ImGui::GetWindowDrawList();
            box_dl->AddRectFilled(
                ImVec2(center.x - avail.x*0.5f, center.y - avail.y*0.5f), 
                ImVec2(center.x + avail.x*0.5f, center.y + avail.y*0.5f), 
                IM_COL32(10, 12, 16, 255)); // Obsidian dark core (#0a0c10)
                
            // Draw professional tactical grid guides behind the reticle
            ImU32 guide_col = IM_COL32(255, 255, 255, 10);
            ImU32 guide_bold = IM_COL32(255, 255, 255, 20);
            box_dl->AddLine(ImVec2(center.x - avail.x * 0.5f, center.y), ImVec2(center.x + avail.x * 0.5f, center.y), guide_bold);
            box_dl->AddLine(ImVec2(center.x, center.y - avail.y * 0.5f), ImVec2(center.x, center.y + avail.y * 0.5f), guide_bold);
            box_dl->AddCircle(center, 16.0f, guide_col, 32, 1.0f);
            box_dl->AddCircle(center, 32.0f, guide_col, 32, 1.0f);
            box_dl->AddCircle(center, 48.0f, guide_col, 32, 1.0f);

            // Tactical Label Overlay
            ImGui::SetCursorPos(ImVec2(8.0f, 6.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.45f, 0.55f, 0.60f));
            ImGui::Text("PREVIEW");
            ImGui::PopStyleColor();
                
            ImVec2 p_min(center.x - pw * 0.5f, center.y - ph * 0.5f);
            ImVec2 p_max(center.x + pw * 0.5f, center.y + ph * 0.5f);

            ImVec4 final_color = m_color;
            final_color.w *= m_opacity;
            ImU32 final_color_u32 = ImGui::ColorConvertFloat4ToU32(final_color);

            if (m_outline_enabled) {
                ImVec4 final_outline = m_outline_color;
                final_outline.w *= m_opacity;
                ImU32 outline_col = ImGui::ColorConvertFloat4ToU32(final_outline);
                box_dl->AddImage(tex_id, ImVec2(p_min.x - 1, p_min.y), ImVec2(p_max.x - 1, p_max.y), ImVec2(0, 0), ImVec2(1, 1), outline_col);
                box_dl->AddImage(tex_id, ImVec2(p_min.x + 1, p_min.y), ImVec2(p_max.x + 1, p_max.y), ImVec2(0, 0), ImVec2(1, 1), outline_col);
                box_dl->AddImage(tex_id, ImVec2(p_min.x, p_min.y - 1), ImVec2(p_max.x, p_max.y - 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
                box_dl->AddImage(tex_id, ImVec2(p_min.x, p_min.y + 1), ImVec2(p_max.x, p_max.y + 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
                box_dl->AddImage(tex_id, ImVec2(p_min.x - 1, p_min.y - 1), ImVec2(p_max.x - 1, p_max.y - 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
                box_dl->AddImage(tex_id, ImVec2(p_min.x + 1, p_min.y + 1), ImVec2(p_max.x + 1, p_max.y + 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
                box_dl->AddImage(tex_id, ImVec2(p_min.x - 1, p_min.y + 1), ImVec2(p_max.x - 1, p_max.y + 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
                box_dl->AddImage(tex_id, ImVec2(p_min.x + 1, p_min.y - 1), ImVec2(p_max.x + 1, p_max.y - 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
            }
                
            box_dl->AddImage(tex_id, p_min, p_max, ImVec2(0, 0), ImVec2(1, 1), final_color_u32);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(); // Pop PreviewBox styling
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Style variables for child window
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.063f, 0.071f, 0.086f, m_bg_alpha));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f); // Beautiful defined button borders
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f); // Tiny premium scrollbar size
    
    ImGui::BeginChild("CrosshairGrid", ImVec2(0, ImGui::GetContentRegionAvail().y - 12.0f), true);
    float avail_width = ImGui::GetContentRegionAvail().x;
    float button_width = 48.0f + ImGui::GetStyle().FramePadding.x * 2.0f;
    float spacing = 8.0f;
    
    // Deterministic wrapping columns calculation
    int columns = (int)((avail_width + spacing) / (button_width + spacing));
    if (columns < 1) columns = 1;
    
    // Calculate dynamic padding to perfectly center-align the grid rows symmetrically
    float total_occupied_w = (float)columns * button_width + (float)(columns - 1) * spacing;
    float offset_x = (avail_width - total_occupied_w) * 0.5f;
    if (offset_x < 0.0f) offset_x = 0.0f;
    
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
    for (int i = 0; i < total_crosshairs; ++i) {
        auto* asset = dover::overlay::assets::AssetStorage::Get().GetCrosshairs()[i];
        if (!asset) continue;
        
        // Shift first element of each row horizontally to distribute remainders symmetrically
        if (i % columns == 0) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);
        }
        
        bool is_selected = (m_selected_index == i);
        if (is_selected) {
            // Highlighting with Slate Blue theme accent colors & definite glowing borders
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.38f, 0.62f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.44f, 0.68f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.33f, 0.50f, 0.75f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.33f, 0.55f, 0.85f, 0.90f)); 
        } else {
            // Subtle dark transparent button for grid items
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.17f, 0.22f, 0.25f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.26f, 0.32f, 0.45f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.29f, 0.38f, 0.52f, 0.70f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.15f, 0.17f, 0.22f, 0.40f));
        }
        
        ImGui::PushID(i);
        void* tex_id = asset->texture_id;
        if (tex_id) {
            if (ImGui::ImageButton("##ch", tex_id, ImVec2(48, 48), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0,0,0,0), m_color)) {
                m_selected_index = i;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", asset->name.c_str());
            }
        } else {
            if (ImGui::Button("N/A", ImVec2(48, 48))) {
                m_selected_index = i;
            }
        }
        
        // Exact wrapping boundary based on math columns rather than pixel rounding comparisons
        if (i + 1 < total_crosshairs && (i + 1) % columns != 0) {
            ImGui::SameLine();
        }
        
        ImGui::PopID();
        ImGui::PopStyleColor(4); // Pop Custom Button background and border states
    }
    ImGui::PopStyleVar(); // Pop ItemSpacing
    ImGui::EndChild();
    ImGui::PopStyleVar(4); // Pop rounding, padding, border size, scrollbar size
    ImGui::PopStyleColor(); // Pop ChildBg

    ImGui::NextColumn();

    // --- Dynamic Monitor Preview (Interactive Drag) ---
    ImVec2 display_size = ImGui::GetIO().DisplaySize;
    float avail_w = ImGui::GetContentRegionAvail().x;
    float aspect_ratio = display_size.y / display_size.x;
    float box_h = avail_w * aspect_ratio;
    
    ImVec2 cursor_p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##MonitorPreview", ImVec2(avail_w, box_h));
    bool is_preview_hovered = ImGui::IsItemHovered();
    
    float scale_f = avail_w / display_size.x;
    
    if (ImGui::IsItemActive()) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        m_pos_x += delta.x / scale_f;
        m_pos_y += delta.y / scale_f;
    }
    
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 min_p = cursor_p;
    ImVec2 max_p = ImVec2(cursor_p.x + avail_w, cursor_p.y + box_h);
    
    // Draw monitor background with premium responsive hover borders
    ImU32 monitor_bg = is_preview_hovered ? IM_COL32(14, 16, 22, 255) : IM_COL32(10, 12, 16, 255);
    ImU32 monitor_border = is_preview_hovered ? IM_COL32(56, 120, 220, 200) : IM_COL32(45, 55, 75, 120);
    dl->AddRectFilled(min_p, max_p, monitor_bg, 2.0f);
    dl->AddRect(min_p, max_p, monitor_border, 2.0f, 0, 1.0f);
    
    // Tactical Screen and Coordinates Specs
    char monitor_label[64];
    snprintf(monitor_label, sizeof(monitor_label), "MONITOR (%.0fx%.0f)", display_size.x, display_size.y);
    dl->AddText(ImVec2(min_p.x + 8.0f, min_p.y + 6.0f), IM_COL32(255, 255, 255, 90), monitor_label);
    
    char offset_label[64];
    snprintf(offset_label, sizeof(offset_label), "X: %+.0f Y: %+.0f", m_pos_x, m_pos_y);
    dl->AddText(ImVec2(max_p.x - ImGui::CalcTextSize(offset_label).x - 8.0f, min_p.y + 6.0f), 
                 is_preview_hovered ? IM_COL32(56, 150, 250, 200) : IM_COL32(255, 255, 255, 90), 
                 offset_label);
    
    // Draw center guides
    ImVec2 box_center = ImVec2(cursor_p.x + avail_w * 0.5f, cursor_p.y + box_h * 0.5f);
    dl->AddLine(ImVec2(box_center.x, min_p.y), ImVec2(box_center.x, max_p.y), IM_COL32(255, 255, 255, 10));
    dl->AddLine(ImVec2(min_p.x, box_center.y), ImVec2(max_p.x, box_center.y), IM_COL32(255, 255, 255, 10));
    
    // Draw miniature crosshair
    if (total_crosshairs > 0 && m_selected_index >= 0 && m_selected_index < total_crosshairs) {
        auto* asset = dover::overlay::assets::AssetStorage::Get().GetCrosshairs()[m_selected_index];
        void* tex_id = asset ? asset->texture_id : nullptr;
        if (tex_id) {
            float ch_w = (float)asset->width * (m_scale / 3.0f) * scale_f;
            float ch_h = (float)asset->height * (m_scale / 3.0f) * scale_f;
            
            // Prevent it from being completely invisible
            if (ch_w < 4.0f) ch_w = 4.0f;
            if (ch_h < 4.0f) ch_h = 4.0f;
            
            float cx = box_center.x + (m_pos_x * scale_f);
            float cy = box_center.y + (m_pos_y * scale_f);
            
            ImVec2 ch_min(cx - ch_w * 0.5f, cy - ch_h * 0.5f);
            ImVec2 ch_max(cx + ch_w * 0.5f, cy + ch_h * 0.5f);
            
            ImVec4 final_color = m_color;
            final_color.w *= m_opacity;
            ImU32 final_color_u32 = ImGui::ColorConvertFloat4ToU32(final_color);

            if (m_outline_enabled) {
                ImVec4 final_outline = m_outline_color;
                final_outline.w *= m_opacity;
                ImU32 outline_col = ImGui::ColorConvertFloat4ToU32(final_outline);
                dl->AddImage(tex_id, ImVec2(ch_min.x-1, ch_min.y), ImVec2(ch_max.x-1, ch_max.y), ImVec2(0,0), ImVec2(1,1), outline_col);
                dl->AddImage(tex_id, ImVec2(ch_min.x+1, ch_min.y), ImVec2(ch_max.x+1, ch_max.y), ImVec2(0,0), ImVec2(1,1), outline_col);
                dl->AddImage(tex_id, ImVec2(ch_min.x, ch_min.y-1), ImVec2(ch_max.x, ch_max.y-1), ImVec2(0,0), ImVec2(1,1), outline_col);
                dl->AddImage(tex_id, ImVec2(ch_min.x, ch_min.y+1), ImVec2(ch_max.x, ch_max.y+1), ImVec2(0,0), ImVec2(1,1), outline_col);
            }
            dl->AddImage(tex_id, ch_min, ch_max, ImVec2(0,0), ImVec2(1,1), final_color_u32);
        }
    }
    ImGui::Spacing();
    ImGui::Spacing();

    // Position Offset Widget (X, Y Inputs & Reset Button) directly under Monitor Preview Box
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    
    // Inject elegant dark backgrounds for coordinate fields
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.10f, 0.14f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.12f, 0.15f, 0.22f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.16f, 0.20f, 0.28f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.16f, 0.20f, 0.28f, 0.80f));
    
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 0.0f));
    float input_w = 110.0f; // 110px gives ample space for numeric entry + step buttons (+/-) without stretching
    
    ImGui::AlignTextToFramePadding();
    ImGui::Text("X");
    ImGui::SameLine();
    ImGui::PushItemWidth(input_w);
    if (ImGui::InputFloat("##PosX", &m_pos_x, 1.0f, 10.0f, "%.0f")) {
        dover::shared::GameStorage::Get().SaveState();
    }
    ImGui::PopItemWidth();
    
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Y");
    ImGui::SameLine();
    ImGui::PushItemWidth(input_w);
    if (ImGui::InputFloat("##PosY", &m_pos_y, 1.0f, 10.0f, "%.0f")) {
        dover::shared::GameStorage::Get().SaveState();
    }
    ImGui::PopItemWidth();
    ImGui::PopStyleColor(4); // Pop coordinates FrameBg & Border overrides
    
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.24f, 0.32f, 0.50f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.30f, 0.40f, 0.75f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.36f, 0.48f, 1.00f));
    if (ImGui::Button("Reset", ImVec2(70.0f, 0))) {
        m_pos_x = 0.0f;
        m_pos_y = 0.0f;
        dover::shared::GameStorage::Get().SaveState();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(); // Pop ItemSpacing
    ImGui::PopStyleVar(3); // Pop FrameRounding, FramePadding & FrameBorderSize
    
    ImGui::Spacing();
    ImGui::Spacing();

    if (dover::shared::g_fonts_preview_bold[3]) {
        ImGui::PushFont(dover::shared::g_fonts_preview_bold[3]);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.68f, 0.84f, 1.00f));
    ImGui::Text("CONFIGURATION");
    ImGui::PopStyleColor();
    if (dover::shared::g_fonts_preview_bold[3]) {
        ImGui::PopFont();
    }
    ImGui::Separator();
    ImGui::Spacing();

    // Right Panel Controls (Scrollable Area below Header)
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f);
    ImGui::BeginChild("SettingsScroll", ImVec2(0, ImGui::GetContentRegionAvail().y - 12.0f), false, ImGuiWindowFlags_None);

    // Apply premium Slate Blue styles dynamically to all controls inside the Settings Area
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.10f, 0.14f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.12f, 0.15f, 0.22f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.18f, 0.24f, 0.35f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.22f, 0.50f, 0.85f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.22f, 0.38f, 0.62f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.33f, 0.50f, 0.75f, 1.00f));

    // Controls
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));

    float frame_h = ImGui::GetFrameHeight();
    
    if (ToggleCheckbox("Enable Crosshair", &m_active)) {
        dover::shared::GameStorage::Get().SaveState();
    }
    
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Crosshair Color");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - frame_h - 8.0f + ImGui::GetCursorPosX()); 
    ImGui::ColorEdit4("##Color", (float*)&m_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    if (ToggleCheckbox("Outline", &m_outline_enabled)) {
        dover::shared::GameStorage::Get().SaveState();
    }
    
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Outline Color");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - frame_h - 8.0f + ImGui::GetCursorPosX()); 
    ImGui::ColorEdit4("##Outline Color", (float*)&m_outline_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    
    
    // Custom premium slim slider for Scale option matching SettingsWindow style
    {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float col_avail_w = ImGui::GetContentRegionAvail().x;
        float height = 26.0f;
        
        float text_y = float(int(pos.y + (height - ImGui::GetFontSize()) * 0.5f));
        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + 8.0f, text_y), ImGui::GetColorU32(ImGuiCol_Text), "Scale");
        
        const float slider_width = 120.0f;
        const float val_width = 36.0f;
        const float right_margin = 8.0f;
        
        float slider_x = pos.x + col_avail_w - slider_width - val_width - right_margin - 8.0f;
        float val_x = pos.x + col_avail_w - val_width - right_margin;
        
        ImGui::Dummy(ImVec2(col_avail_w, height));
        ImGui::SetCursorScreenPos(ImVec2(slider_x, pos.y + (height - ImGui::GetFrameHeight()) * 0.5f));
        
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,          ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,   ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,    ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,       ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
        
        ImGui::SetNextItemWidth(slider_width);
        bool changed = ImGui::SliderFloat("##Scale", &m_scale, 0.1f, 3.0f, "");
        bool active = ImGui::IsItemActive();
        bool hovered = ImGui::IsItemHovered();
        
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar();
        
        float frame_height = ImGui::GetFrameHeight();
        ImVec2 slider_pos = ImVec2(slider_x, pos.y + (height - frame_height) * 0.5f);
        
        float fraction = (m_scale - 0.1f) / 2.9f;
        if (fraction < 0.0f) fraction = 0.0f;
        if (fraction > 1.0f) fraction = 1.0f;
        
        float grab_center_x = slider_pos.x + fraction * slider_width;
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
        
        char val_buf[16];
        snprintf(val_buf, sizeof(val_buf), "%.1fx", m_scale);
        float val_y = float(int(pos.y + (height - ImGui::GetFontSize()) * 0.5f));
        ImGui::GetWindowDrawList()->AddText(ImVec2(val_x, val_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), val_buf);
        
        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + height + ImGui::GetStyle().ItemSpacing.y));
        
        if (changed) {
            dover::shared::GameStorage::Get().SaveState();
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    // Custom premium slim slider for Transparency option matching SettingsWindow style
    {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float col_avail_w = ImGui::GetContentRegionAvail().x;
        float height = 26.0f;
        
        float text_y = float(int(pos.y + (height - ImGui::GetFontSize()) * 0.5f));
        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + 8.0f, text_y), ImGui::GetColorU32(ImGuiCol_Text), "Transparency");
        
        const float slider_width = 120.0f;
        const float val_width = 36.0f;
        const float right_margin = 8.0f;
        
        float slider_x = pos.x + col_avail_w - slider_width - val_width - right_margin - 8.0f;
        float val_x = pos.x + col_avail_w - val_width - right_margin;
        
        ImGui::Dummy(ImVec2(col_avail_w, height));
        ImGui::SetCursorScreenPos(ImVec2(slider_x, pos.y + (height - ImGui::GetFrameHeight()) * 0.5f));
        
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,          ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,   ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,    ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,       ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
        
        ImGui::SetNextItemWidth(slider_width);
        bool changed = ImGui::SliderFloat("##Opacity", &m_opacity, 0.0f, 1.0f, "");
        bool active = ImGui::IsItemActive();
        bool hovered = ImGui::IsItemHovered();
        
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar();
        
        float frame_height = ImGui::GetFrameHeight();
        ImVec2 slider_pos = ImVec2(slider_x, pos.y + (height - frame_height) * 0.5f);
        
        float fraction = m_opacity;
        if (fraction < 0.0f) fraction = 0.0f;
        if (fraction > 1.0f) fraction = 1.0f;
        
        float grab_center_x = slider_pos.x + fraction * slider_width;
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
        
        char val_buf[16];
        snprintf(val_buf, sizeof(val_buf), "%d%%", (int)((1.0f - m_opacity) * 100.0f + 0.5f));
        float val_y = float(int(pos.y + (height - ImGui::GetFontSize()) * 0.5f));
        ImGui::GetWindowDrawList()->AddText(ImVec2(val_x, val_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), val_buf);
        
        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + height + ImGui::GetStyle().ItemSpacing.y));
        
        if (changed) {
            dover::shared::GameStorage::Get().SaveState();
        }
    }
    
    ImGui::Dummy(ImVec2(0.0f, 16.0f)); // Premium breathing padding at the bottom of scrollable settings

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(6); // Pop active Settings area slate blue color palette
    ImGui::EndChild();
    ImGui::PopStyleVar(); // Pop scrollbar size

    ImGui::Columns(1);
}

} // namespace dover::overlay::crosshair
