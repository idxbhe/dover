#include "overlay/crosshair/crosshair_window.h"
#include "overlay/assets/asset_storage.h"
#include "overlay/dx11_hook.h"
#include "overlay/dx9_hook.h"
#include "shared/log.h"
#include "overlay/icons.h"
#include "overlay/game_storage.h"

#include <d3d11.h>
#include <d3d9.h>

namespace dover::overlay {
    extern ImFont* g_font_gui;
    extern ImFont* g_font_panel;
    extern ImFont* g_fonts_preview_bold[5];
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

        ImFont* icon_font = dover::overlay::g_font_panel ? dover::overlay::g_font_panel : dover::overlay::g_font_gui;
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
    : ui::BaseWindow("Crosshairs", ui::WindowFeature::Default, ImVec2(600, 450)) {
}

void CrosshairWindow::PreRender(bool /*interactive*/) {
    ImVec2 min_p = ImGui::GetWindowPos();
    ImVec2 max_p = ImVec2(min_p.x + ImGui::GetWindowSize().x, min_p.y + ImGui::GetWindowSize().y);
    
    // Premium custom futuristic dark Slate-Blue to Dark Nebula Purple gradient
    // Incorporates a gorgeous cyberpunk/obsidian dark-ambient aesthetic
    ImU32 col_tl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.120f, 0.140f, 0.200f, m_bg_alpha)); // Top-Left: Midnight Slate Blue (#1e2433)
    ImU32 col_tr = ImGui::ColorConvertFloat4ToU32(ImVec4(0.080f, 0.090f, 0.130f, m_bg_alpha)); // Top-Right: Deep Obsidian Core (#141721)
    ImU32 col_br = ImGui::ColorConvertFloat4ToU32(ImVec4(0.160f, 0.080f, 0.160f, m_bg_alpha)); // Bottom-Right: Rich Nebula Purple (#291429)
    ImU32 col_bl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.060f, 0.070f, 0.100f, m_bg_alpha)); // Bottom-Left: Pure Deep Velvet Black (#0f111a)
    
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(min_p, max_p, col_tl, col_tr, col_br, col_bl);
}

void CrosshairWindow::Initialize() {
    assets::AssetStorage::Get().Initialize();
}

void CrosshairWindow::Shutdown() {
    auto& crosshairs = assets::AssetStorage::Get().GetCrosshairs();
    for (size_t i = 0; i < crosshairs.size(); ++i) {
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
        
        auto& crosshairs = assets::AssetStorage::Get().GetCrosshairs();
        
        ID3D11Device* dx11 = GetDx11Device();
        IDirect3DDevice9* dx9 = GetDx9Device();
        
        if (dx11) {
            dover::shared::LogInfo("Loading Crosshair Textures (DX11)...");
            for (size_t i = 0; i < crosshairs.size(); ++i) {
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
    
    auto& crosshairs = assets::AssetStorage::Get().GetCrosshairs();
    if (crosshairs.empty()) return;
    if (m_selected_index < 0 || m_selected_index >= (int)crosshairs.size()) return;

    void* tex_id = crosshairs[m_selected_index].texture_id;
    if (!tex_id) return;

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    ImVec2 center = ImGui::GetIO().DisplaySize;
    center.x *= 0.5f;
    center.y *= 0.5f;

    center.x += m_pos_x;
    center.y += m_pos_y;

    // Scaling logic: Max scale (2.0x) equals the original texture size (64x64).
    // Therefore, 1.0x equals half the texture size (32x32).
    float w = (float)crosshairs[m_selected_index].width * (m_scale * 0.5f);
    float h = (float)crosshairs[m_selected_index].height * (m_scale * 0.5f);

    ImVec2 p_min(center.x - w * 0.5f, center.y - h * 0.5f);
    ImVec2 p_max(center.x + w * 0.5f, center.y + h * 0.5f);

    if (m_outline_enabled) {
        ImU32 outline_col = ImGui::ColorConvertFloat4ToU32(m_outline_color);
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
    draw_list->AddImage(tex_id, p_min, p_max, ImVec2(0, 0), ImVec2(1, 1), ImGui::ColorConvertFloat4ToU32(m_color));
}

void CrosshairWindow::RenderContent(bool interactive) {
    if (!interactive) return;

    auto& crosshairs = assets::AssetStorage::Get().GetCrosshairs();
    
    if (crosshairs.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "dover_assets.pak not found or empty.");
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
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    
    ImGui::BeginChild("PreviewBox", ImVec2(0, preview_h), true);
    if (!crosshairs.empty() && m_selected_index >= 0 && m_selected_index < (int)crosshairs.size()) {
        void* tex_id = crosshairs[m_selected_index].texture_id;
        if (tex_id) {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImVec2 center = ImVec2(ImGui::GetCursorScreenPos().x + avail.x * 0.5f, 
                                   ImGui::GetCursorScreenPos().y + avail.y * 0.5f);
            
            // Preview Box Scaling: Also applies the (scale * 0.5f) rule assuming original size is 64x64
            float pw = 64.0f * (m_scale * 0.5f);
            float ph = 64.0f * (m_scale * 0.5f);
            
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

            if (m_outline_enabled) {
                ImU32 outline_col = ImGui::ColorConvertFloat4ToU32(m_outline_color);
                box_dl->AddImage(tex_id, ImVec2(p_min.x - 1, p_min.y), ImVec2(p_max.x - 1, p_max.y), ImVec2(0, 0), ImVec2(1, 1), outline_col);
                box_dl->AddImage(tex_id, ImVec2(p_min.x + 1, p_min.y), ImVec2(p_max.x + 1, p_max.y), ImVec2(0, 0), ImVec2(1, 1), outline_col);
                box_dl->AddImage(tex_id, ImVec2(p_min.x, p_min.y - 1), ImVec2(p_max.x, p_max.y - 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
                box_dl->AddImage(tex_id, ImVec2(p_min.x, p_min.y + 1), ImVec2(p_max.x, p_max.y + 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
                box_dl->AddImage(tex_id, ImVec2(p_min.x - 1, p_min.y - 1), ImVec2(p_max.x - 1, p_max.y - 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
                box_dl->AddImage(tex_id, ImVec2(p_min.x + 1, p_min.y + 1), ImVec2(p_max.x + 1, p_max.y + 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
                box_dl->AddImage(tex_id, ImVec2(p_min.x - 1, p_min.y + 1), ImVec2(p_max.x - 1, p_max.y + 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
                box_dl->AddImage(tex_id, ImVec2(p_min.x + 1, p_min.y - 1), ImVec2(p_max.x + 1, p_max.y - 1), ImVec2(0, 0), ImVec2(1, 1), outline_col);
            }
                
            box_dl->AddImage(tex_id, p_min, p_max, ImVec2(0, 0), ImVec2(1, 1), ImGui::ColorConvertFloat4ToU32(m_color));
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(); // Pop PreviewBox styling
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Style variables for child window
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.063f, 0.071f, 0.086f, m_bg_alpha));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f); // Beautiful defined button borders
    
    ImGui::BeginChild("CrosshairGrid", ImVec2(0, 0), true);
    // Calculate columns based on absolute window width to prevent scrollbar flicker loops
    float grid_w = ImGui::GetWindowWidth() - 24.0f; // Account for scrollbar and padding
    int columns = (int)(grid_w / 64.0f);
    if (columns < 1) columns = 1;
    
    if (ImGui::BeginTable("CrosshairTable", columns, ImGuiTableFlags_SizingStretchSame)) {
        for (size_t i = 0; i < crosshairs.size(); ++i) {
            ImGui::TableNextColumn();
            
            // Horizontally center the ImageButton inside the stretched cell
            float cell_w = ImGui::GetContentRegionAvail().x;
            float btn_w = 48.0f + ImGui::GetStyle().FramePadding.x * 2.0f;
            if (cell_w > btn_w) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cell_w - btn_w) * 0.5f);
            }
            
            bool is_selected = (m_selected_index == (int)i);
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
            
            ImGui::PushID((int)i);
            void* tex_id = crosshairs[i].texture_id;
            if (tex_id) {
                if (ImGui::ImageButton("##ch", tex_id, ImVec2(48, 48), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0,0,0,0), m_color)) {
                    m_selected_index = (int)i;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", crosshairs[i].name.c_str());
                }
            } else {
                if (ImGui::Button("N/A", ImVec2(48, 48))) {
                    m_selected_index = (int)i;
                }
            }
            ImGui::PopID();
            ImGui::PopStyleColor(4); // Pop Custom Button background and border states
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(3); // Pop rounding, padding, and border size
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
    dl->AddRectFilled(min_p, max_p, monitor_bg, 6.0f);
    dl->AddRect(min_p, max_p, monitor_border, 6.0f, 0, 1.0f);
    
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
    if (!crosshairs.empty() && m_selected_index >= 0 && m_selected_index < (int)crosshairs.size()) {
        void* tex_id = crosshairs[m_selected_index].texture_id;
        if (tex_id) {
            float ch_w = (float)crosshairs[m_selected_index].width * (m_scale * 0.5f) * scale_f;
            float ch_h = (float)crosshairs[m_selected_index].height * (m_scale * 0.5f) * scale_f;
            
            // Prevent it from being completely invisible
            if (ch_w < 4.0f) ch_w = 4.0f;
            if (ch_h < 4.0f) ch_h = 4.0f;
            
            float cx = box_center.x + (m_pos_x * scale_f);
            float cy = box_center.y + (m_pos_y * scale_f);
            
            ImVec2 ch_min(cx - ch_w * 0.5f, cy - ch_h * 0.5f);
            ImVec2 ch_max(cx + ch_w * 0.5f, cy + ch_h * 0.5f);
            
            if (m_outline_enabled) {
                ImU32 outline_col = ImGui::ColorConvertFloat4ToU32(m_outline_color);
                dl->AddImage(tex_id, ImVec2(ch_min.x-1, ch_min.y), ImVec2(ch_max.x-1, ch_max.y), ImVec2(0,0), ImVec2(1,1), outline_col);
                dl->AddImage(tex_id, ImVec2(ch_min.x+1, ch_min.y), ImVec2(ch_max.x+1, ch_max.y), ImVec2(0,0), ImVec2(1,1), outline_col);
                dl->AddImage(tex_id, ImVec2(ch_min.x, ch_min.y-1), ImVec2(ch_max.x, ch_max.y-1), ImVec2(0,0), ImVec2(1,1), outline_col);
                dl->AddImage(tex_id, ImVec2(ch_min.x, ch_min.y+1), ImVec2(ch_max.x, ch_max.y+1), ImVec2(0,0), ImVec2(1,1), outline_col);
            }
            dl->AddImage(tex_id, ch_min, ch_max, ImVec2(0,0), ImVec2(1,1), ImGui::ColorConvertFloat4ToU32(m_color));
        }
    }
    ImGui::Spacing();
    ImGui::Spacing();

    // Position Offset Widget (X, Y Inputs & Reset Button) directly under Monitor Preview Box
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
    
    // Inject elegant dark backgrounds for coordinate fields
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.10f, 0.14f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.12f, 0.15f, 0.22f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.16f, 0.20f, 0.28f, 1.00f));

    ImGui::TextDisabled("Position Offset (px)");
    
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 0.0f));
    // Divide availability: X takes 38%, Y takes 38%, Reset takes remaining 24%
    float input_w = (avail_w - 12.0f) * 0.38f;
    
    ImGui::PushItemWidth(input_w);
    if (ImGui::InputFloat("X##PosX", &m_pos_x, 1.0f, 10.0f, "%.0f")) {
        dover::overlay::GameStorage::Get().SaveState();
    }
    ImGui::SameLine();
    if (ImGui::InputFloat("Y##PosY", &m_pos_y, 1.0f, 10.0f, "%.0f")) {
        dover::overlay::GameStorage::Get().SaveState();
    }
    ImGui::PopItemWidth();
    ImGui::PopStyleColor(3); // Pop coordinates FrameBg overrides
    
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.24f, 0.32f, 0.50f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.30f, 0.40f, 0.75f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.36f, 0.48f, 1.00f));
    if (ImGui::Button("Reset", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
        m_pos_x = 0.0f;
        m_pos_y = 0.0f;
        dover::overlay::GameStorage::Get().SaveState();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(); // Pop ItemSpacing
    ImGui::PopStyleVar(2); // Pop FrameRounding & FramePadding
    
    ImGui::Spacing();
    ImGui::Spacing();

    // Right Panel Header & Controls (Scrollable Area)
    ImGui::BeginChild("SettingsScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);

    // Apply premium Slate Blue styles dynamically to all controls inside the Settings Area
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.10f, 0.14f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.12f, 0.15f, 0.22f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.18f, 0.24f, 0.35f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.22f, 0.50f, 0.85f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.22f, 0.38f, 0.62f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.33f, 0.50f, 0.75f, 1.00f));

    if (dover::overlay::g_fonts_preview_bold[3]) {
        ImGui::PushFont(dover::overlay::g_fonts_preview_bold[3]);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.68f, 0.84f, 1.00f));
    ImGui::Text("SETTINGS & CONFIGURATION");
    ImGui::PopStyleColor();
    if (dover::overlay::g_fonts_preview_bold[3]) {
        ImGui::PopFont();
    }
    ImGui::Separator();
    ImGui::Spacing();

    // Controls
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));

    if (ToggleCheckbox("Enable Crosshair Overlay", &m_active)) {
        dover::overlay::GameStorage::Get().SaveState();
    }
    
    if (m_active) {
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::TextDisabled("Crosshair Color");
        ImGui::SameLine(ImGui::GetWindowWidth() * 0.618f - 40.0f); // Position color picker cleanly on the right
        ImGui::ColorEdit4("##Color", (float*)&m_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    }
    
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    if (ToggleCheckbox("Enable Outline Shadow", &m_outline_enabled)) {
        dover::overlay::GameStorage::Get().SaveState();
    }
    
    if (m_outline_enabled) {
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::TextDisabled("Outline Shadow Color");
        ImGui::SameLine(ImGui::GetWindowWidth() * 0.618f - 40.0f); // Position outline color picker cleanly on the right
        ImGui::ColorEdit4("##Outline Color", (float*)&m_outline_color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    }
    
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::SliderFloat("Scale", &m_scale, 0.3f, 3.0f, "%.1fx");

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(6); // Pop active Settings area slate blue color palette
    ImGui::EndChild();

    ImGui::Columns(1);
}

} // namespace dover::overlay::crosshair
