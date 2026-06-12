#include "shared/ui/widgets.h"
#include "shared/icons.h"
#include "shared/theme.h"
#include "shared/renderer.h"

#include <imgui.h>
#include <cstdio>

namespace dover::shared::ui {

bool ToggleCheckbox(const char* label, bool* value_ptr) {
    float avail_w = ImGui::GetContentRegionAvail().x;
    float height = 26.0f; // Premium row height matching the 28.0f icon
    
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0, 0, 0, 0));

    ImVec2 pos = ImGui::GetCursorScreenPos();
    pos.x = float(int(pos.x));
    pos.y = float(int(pos.y));
    
    // Optimal ImGui ID usage: Use static string + label hash to avoid snprintf in hot-path
    ImGui::PushID(label);
    bool clicked = ImGui::Selectable("##toggle", false, ImGuiSelectableFlags_None, ImVec2(avail_w, height));
    ImGui::PopID();

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

    // Use g_font_gui
    ImFont* icon_font = dover::shared::g_font_gui;
    float icon_font_size = icon_font ? icon_font->LegacySize : 20.0f; 

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

bool ToggleCheckbox(const char* label, std::atomic<bool>* value_ptr) {
    if (!value_ptr) return false;
    bool val = value_ptr->load(std::memory_order_relaxed);
    if (ToggleCheckbox(label, &val)) {
        value_ptr->store(val, std::memory_order_relaxed);
        return true;
    }
    return false;
}

} // namespace dover::shared::ui
