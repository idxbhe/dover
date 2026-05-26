#include "overlay/overlay_ui.h"
#include "overlay/hook_utils.h"
#include "overlay/notes/layout.h"
#include "overlay/notes/manager.h"
#include "overlay/settings/settings_window.h"
#include "overlay/icons.h"
#include "overlay/fonts.h"
#include "overlay/theme.h"
#include "overlay/input_hook.h"

#include <windows.h>
#include <psapi.h>
#include <imgui.h>
#include <misc/freetype/imgui_freetype.h>
#include <imgui_impl_win32.h>
#include <cstring>
#include <string>

#pragma comment(lib, "psapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace dover::overlay {

bool g_show_overlay = false;
bool g_in_overlay_frame = false;
WNDPROC g_original_wnd_proc = nullptr;
const char* g_active_dx_version = "Unknown API";
float g_overlay_bg_alpha = 0.63f;     // Approx 160/255
float g_global_window_alpha = 0.95f;

bool g_cfg_show_fps = true;
bool g_cfg_show_clock = true;
bool g_cfg_show_api = false;

// ---- Accurate FPS counter using QPC (bypasses ImGui's inflated averaging) ----
static LARGE_INTEGER g_fps_freq      = {};
static double        g_fps_last_time = 0.0;
static int           g_fps_frames    = 0;
static float         g_fps_value     = 0.0f;
static bool          g_fps_init      = false;

static void TickFPS() {
  if (!g_fps_init) {
    QueryPerformanceFrequency(&g_fps_freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    g_fps_last_time = static_cast<double>(now.QuadPart) / static_cast<double>(g_fps_freq.QuadPart);
    g_fps_init = true;
    return;
  }

  g_fps_frames++;

  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  double current_time = static_cast<double>(now.QuadPart) / static_cast<double>(g_fps_freq.QuadPart);
  double elapsed = current_time - g_fps_last_time;

  if (elapsed >= 0.5) { // Update every 0.5 seconds for perfect stability and responsiveness
    g_fps_value = static_cast<float>(g_fps_frames / elapsed);
    g_fps_frames = 0;
    g_fps_last_time = current_time;
  }
}


void RenderImGuiUI() {
  PollGamepadToggle();

  // 1. Draw Pinned Info Window (transparent corner overlay) - Hidden when interactive overlay is active
  if (!g_show_overlay && (g_cfg_show_fps || g_cfg_show_clock || g_cfg_show_api)) {
    ImGui::SetNextWindowPos(ImVec2(12.0f, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("Info Window", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);

    if (g_cfg_show_fps) {
      TickFPS();
    }

    if (g_cfg_show_clock) {
      SYSTEMTIME time{};
      GetLocalTime(&time);
      ImGui::TextColored(ImVec4(0.20f, 1.00f, 0.70f, 1.00f), "%02u:%02u:%02u", time.wHour, time.wMinute, time.wSecond);
    }
    
    if (g_cfg_show_fps) {
      ImGui::TextColored(ImVec4(1.00f, 1.00f, 1.00f, 1.00f), "FPS:  %.1f", g_fps_value);
    }
    
    if (g_cfg_show_api) {
      ImGui::TextColored(ImVec4(1.00f, 0.80f, 0.20f, 1.00f), "API:  %s", g_active_dx_version);
    }
    
    ImGui::End();
  }

  // 2. Draw Steam-style Interactive Navigation and floating windows if visible
  if (g_show_overlay) {
    // Dim the underlying game frame for premium presentation
    ImVec2 display_size = ImGui::GetIO().DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), display_size, IM_COL32(0, 0, 0, (int)(g_overlay_bg_alpha * 255.0f)));

    bool show_notes = notes::GetNotesWindow().IsOpen();

    // A. Top Navigation Bar (Fixed persistent toolbar at the top)
    const float bar_height = 40.0f;
    const float bar_padding_x = 14.0f;
    const float button_width = 28.0f;
    const float button_height = 28.0f;
    const float button_spacing = 8.0f;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(display_size.x, bar_height), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    bool nav_bar_ok = ImGui::Begin("Top Navigation Bar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    if (nav_bar_ok) {
      ImVec2 min_p = ImGui::GetWindowPos();
      
      // Gorgeous slanted tag for the DOVER OVERLAY brand section
      const float rect_w = 155.0f;
      const float slant_w = 25.0f;
      
      ImVec2 rect_max = ImVec2(min_p.x + rect_w, min_p.y + bar_height);
      
      // FLAT SUBTLE DARK GRADIENT (Sleek Slate/Charcoal)
      ImU32 col_tl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.13f, 0.15f, 0.19f, 0.95f)); // #212630
      ImU32 col_tr = ImGui::ColorConvertFloat4ToU32(ImVec4(0.09f, 0.11f, 0.14f, 0.95f)); // #171c24
      ImU32 col_br = ImGui::ColorConvertFloat4ToU32(ImVec4(0.07f, 0.08f, 0.11f, 0.95f)); // #12141c
      ImU32 col_bl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.11f, 0.13f, 0.16f, 0.95f)); // #1c2129
      
      // 1. Draw the gradient tag body up to x = 155.0f
      ImGui::GetWindowDrawList()->AddRectFilledMultiColor(min_p, rect_max, col_tl, col_tr, col_br, col_bl);
      
      // 2. Draw the elegant slanted edge (/) from x = 155.0f to x = 180.0f
      ImVec2 tri_a = ImVec2(min_p.x + rect_w, min_p.y);
      ImVec2 tri_b = ImVec2(min_p.x + rect_w, min_p.y + bar_height);
      ImVec2 tri_c = ImVec2(min_p.x + rect_w + slant_w, min_p.y);
      ImGui::GetWindowDrawList()->AddTriangleFilled(tri_a, tri_b, tri_c, col_tr);
      
      // 3. Draw a very subtle, flat dark gray under-border (no neon glow!)
      ImU32 border_dark = ImGui::ColorConvertFloat4ToU32(ImVec4(0.20f, 0.24f, 0.30f, 0.40f)); // Flat Slate Gray
      ImGui::GetWindowDrawList()->PathLineTo(ImVec2(min_p.x, min_p.y + bar_height));
      ImGui::GetWindowDrawList()->PathLineTo(ImVec2(min_p.x + rect_w, min_p.y + bar_height));
      ImGui::GetWindowDrawList()->PathLineTo(ImVec2(min_p.x + rect_w + slant_w, min_p.y));
      ImGui::GetWindowDrawList()->PathStroke(border_dark, false, 1.0f);
    }

    ImGui::PushFont(g_font_panel);

    const float icon_btn_width = 34.0f;
    const float button_group_width = (icon_btn_width * 2.0f) + button_spacing; // Group of 2 icon buttons
    const float brand_y = (bar_height - ImGui::GetTextLineHeight()) * 0.5f;
    const float button_y = (bar_height - button_height) * 0.5f;

    ImGui::SetCursorPos(ImVec2(bar_padding_x, brand_y));
    ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.00f, 1.00f), "DOVER OVERLAY");

    const float center_start_x = (display_size.x - button_group_width) * 0.5f;
    const float close_button_x = display_size.x - bar_padding_x - button_width;

    // Use completely transparent button styles so our custom drawing is shown
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); // Make original text transparent

    // A. Notes Button Rendering (Modern Dot / Line Indicator + Taskbar Clicking Mechanism)
    const float icon_box_height = 30.0f;
    const float icon_box_y = (bar_height - icon_box_height) * 0.5f;
    ImGui::SetCursorPos(ImVec2(center_start_x, icon_box_y));
    {
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImVec2 p_max = ImVec2(pos.x + icon_btn_width, pos.y + icon_box_height);
      bool hovered = ImGui::IsMouseHoveringRect(pos, p_max);
      bool notes_focused = show_notes && notes::GetNotesWindow().IsFocused();
      
      ImVec2 glyph_size = ImGui::CalcTextSize(ICON_PANEL_NOTES);
      ImVec2 text_pos = ImVec2(pos.x + (icon_btn_width - glyph_size.x) * 0.5f, pos.y + (icon_box_height - glyph_size.y) * 0.5f + 2.5f);
      
      ImVec4 text_color, shadow_color, highlight_color, border_color;
      if (show_notes) {
        text_color      = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        highlight_color = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);
        shadow_color    = ImVec4(0.00f, 0.45f, 0.85f, 0.85f);
        border_color    = ImVec4(0.20f, 0.65f, 1.00f, 0.85f); // Vibrant blue active border
      } else if (hovered) {
        text_color      = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        highlight_color = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
        shadow_color    = ImVec4(0.12f, 0.15f, 0.20f, 0.80f);
        border_color    = ImVec4(0.35f, 0.50f, 0.75f, 0.60f); // Subtle hovered border
      } else {
        text_color      = ImVec4(0.95f, 0.96f, 0.98f, 1.00f); // High-visibility bright off-white (no fade)
        highlight_color = ImVec4(1.00f, 1.00f, 1.00f, 0.20f);
        shadow_color    = ImVec4(0.07f, 0.08f, 0.11f, 0.70f);
        border_color    = ImVec4(0.18f, 0.20f, 0.25f, 0.40f); // Sleek dull border
      }

      // Draw premium slate/charcoal gradient box background matching the brand label
      ImU32 col_tl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.13f, 0.15f, 0.19f, 0.95f)); // #212630
      ImU32 col_tr = ImGui::ColorConvertFloat4ToU32(ImVec4(0.09f, 0.11f, 0.14f, 0.95f)); // #171c24
      ImU32 col_br = ImGui::ColorConvertFloat4ToU32(ImVec4(0.07f, 0.08f, 0.11f, 0.95f)); // #12141c
      ImU32 col_bl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.11f, 0.13f, 0.16f, 0.95f)); // #1c2129
      ImGui::GetWindowDrawList()->AddRectFilledMultiColor(pos, p_max, col_tl, col_tr, col_br, col_bl);

      // Draw premium rounded border over the box to round off the sharp corners
      ImGui::GetWindowDrawList()->AddRect(pos, p_max, ImGui::ColorConvertFloat4ToU32(border_color), 4.0f, ImDrawFlags_None, 1.0f);

      // Draw 3D double shadow behind the text glyph
      ImGui::GetWindowDrawList()->AddText(g_font_panel, g_font_panel->FontSize, ImVec2(text_pos.x + 1.0f, text_pos.y + 1.5f), ImGui::ColorConvertFloat4ToU32(shadow_color), ICON_PANEL_NOTES);
      ImGui::GetWindowDrawList()->AddText(g_font_panel, g_font_panel->FontSize, ImVec2(text_pos.x - 0.5f, text_pos.y - 0.5f), ImGui::ColorConvertFloat4ToU32(highlight_color), ICON_PANEL_NOTES);

      // Draw the crisp main icon text inside the gradient box
      ImGui::GetWindowDrawList()->AddText(g_font_panel, g_font_panel->FontSize, text_pos, ImGui::ColorConvertFloat4ToU32(text_color), ICON_PANEL_NOTES);

      // Draw active indicator (Long line if focused, small dot if open but not focused)
      if (show_notes) {
        float indicator_y = pos.y + icon_box_height + 2.0f;
        if (notes_focused) {
          ImVec2 l_start(pos.x + 6.0f, indicator_y);
          ImVec2 l_end(pos.x + icon_btn_width - 6.0f, indicator_y + 2.0f);
          ImGui::GetWindowDrawList()->AddRectFilled(l_start, l_end, ImGui::ColorConvertFloat4ToU32(ImVec4(0.20f, 0.65f, 1.00f, 1.00f)), 1.0f);
        } else {
          ImVec2 dot_center(pos.x + icon_btn_width * 0.5f, indicator_y + 1.0f);
          ImGui::GetWindowDrawList()->AddCircleFilled(dot_center, 2.0f, ImGui::ColorConvertFloat4ToU32(ImVec4(0.70f, 0.73f, 0.80f, 0.85f)), 16);
        }
      }
    }
    if (ImGui::Button(ICON_PANEL_NOTES, ImVec2(icon_btn_width, icon_box_height))) {
      if (!show_notes) {
        notes::GetNotesWindow().Open();
        ImGui::SetWindowFocus("Notes");
      } else if (!notes::GetNotesWindow().IsFocused()) {
        ImGui::SetWindowFocus("Notes");
      }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Notes");

    // B. Settings Button Rendering (Modern Dot / Line Indicator + Taskbar Clicking Mechanism)
    ImGui::SameLine(0.0f, button_spacing);
    {
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImVec2 p_max = ImVec2(pos.x + icon_btn_width, pos.y + icon_box_height);
      bool hovered = ImGui::IsMouseHoveringRect(pos, p_max);
      
      ImVec2 glyph_size = ImGui::CalcTextSize(ICON_PANEL_SETTINGS);
      ImVec2 text_pos = ImVec2(pos.x + (icon_btn_width - glyph_size.x) * 0.5f, pos.y + (icon_box_height - glyph_size.y) * 0.5f + 2.5f);
      
      ImVec4 text_color, shadow_color, highlight_color, border_color;
      bool is_settings_open = settings::GetSettingsWindow().IsOpen();
      bool is_settings_focused = settings::GetSettingsWindow().IsFocused();

      if (is_settings_open) {
        text_color      = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        highlight_color = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);
        shadow_color    = ImVec4(0.55f, 0.30f, 0.90f, 0.85f);
        border_color    = ImVec4(0.68f, 0.45f, 0.95f, 0.85f); // Vibrant purple active border
      } else if (hovered) {
        text_color      = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        highlight_color = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
        shadow_color    = ImVec4(0.12f, 0.15f, 0.20f, 0.80f);
        border_color    = ImVec4(0.55f, 0.40f, 0.80f, 0.60f); // Subtle hovered border
      } else {
        text_color      = ImVec4(0.95f, 0.96f, 0.98f, 1.00f); // High-visibility bright off-white (no fade)
        highlight_color = ImVec4(1.00f, 1.00f, 1.00f, 0.20f);
        shadow_color    = ImVec4(0.07f, 0.08f, 0.11f, 0.70f);
        border_color    = ImVec4(0.18f, 0.20f, 0.25f, 0.40f); // Sleek dull border
      }
      
      // Draw premium slate/charcoal gradient box background matching the brand label
      ImU32 col_tl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.13f, 0.15f, 0.19f, 0.95f)); // #212630
      ImU32 col_tr = ImGui::ColorConvertFloat4ToU32(ImVec4(0.09f, 0.11f, 0.14f, 0.95f)); // #171c24
      ImU32 col_br = ImGui::ColorConvertFloat4ToU32(ImVec4(0.07f, 0.08f, 0.11f, 0.95f)); // #12141c
      ImU32 col_bl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.11f, 0.13f, 0.16f, 0.95f)); // #1c2129
      ImGui::GetWindowDrawList()->AddRectFilledMultiColor(pos, p_max, col_tl, col_tr, col_br, col_bl);

      // Draw premium rounded border over the box to round off the sharp corners
      ImGui::GetWindowDrawList()->AddRect(pos, p_max, ImGui::ColorConvertFloat4ToU32(border_color), 4.0f, ImDrawFlags_None, 1.0f);

      // Draw 3D double shadow behind the text glyph
      ImGui::GetWindowDrawList()->AddText(g_font_panel, g_font_panel->FontSize, ImVec2(text_pos.x + 1.0f, text_pos.y + 1.5f), ImGui::ColorConvertFloat4ToU32(shadow_color), ICON_PANEL_SETTINGS);
      ImGui::GetWindowDrawList()->AddText(g_font_panel, g_font_panel->FontSize, ImVec2(text_pos.x - 0.5f, text_pos.y - 0.5f), ImGui::ColorConvertFloat4ToU32(highlight_color), ICON_PANEL_SETTINGS);

      // Draw the crisp main icon text inside the gradient box
      ImGui::GetWindowDrawList()->AddText(g_font_panel, g_font_panel->FontSize, text_pos, ImGui::ColorConvertFloat4ToU32(text_color), ICON_PANEL_SETTINGS);

      // Draw active indicator (Long line if focused, small dot if open but not focused)
      if (is_settings_open) {
        float indicator_y = pos.y + icon_box_height + 2.0f;
        if (is_settings_focused) {
          ImVec2 l_start(pos.x + 6.0f, indicator_y);
          ImVec2 l_end(pos.x + icon_btn_width - 6.0f, indicator_y + 2.0f);
          ImGui::GetWindowDrawList()->AddRectFilled(l_start, l_end, ImGui::ColorConvertFloat4ToU32(ImVec4(0.20f, 0.65f, 1.00f, 1.00f)), 1.0f);
        } else {
          ImVec2 dot_center(pos.x + icon_btn_width * 0.5f, indicator_y + 1.0f);
          ImGui::GetWindowDrawList()->AddCircleFilled(dot_center, 2.0f, ImGui::ColorConvertFloat4ToU32(ImVec4(0.70f, 0.73f, 0.80f, 0.85f)), 16);
        }
      }
    }
    if (ImGui::Button(ICON_PANEL_SETTINGS, ImVec2(icon_btn_width, icon_box_height))) {
      if (!settings::GetSettingsWindow().IsOpen()) {
        settings::GetSettingsWindow().Open();
        ImGui::SetWindowFocus("Settings");
      } else if (!settings::GetSettingsWindow().IsFocused()) {
        ImGui::SetWindowFocus("Settings");
      }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Settings");
    ImGui::PopStyleColor(); // Pop ImGuiCol_Text to restore icon text visibility

    // C. Close Button Rendering (Flat, Dark Slate with subtle Red hovered accent - Pixel Perfect Alignment)
    ImGui::SetCursorPos(ImVec2(close_button_x, button_y));
    {
      ImVec2 p_min = ImGui::GetCursorScreenPos();
      ImVec2 p_max = ImVec2(p_min.x + button_width, p_min.y + button_height);
      bool hovered = ImGui::IsMouseHoveringRect(p_min, p_max);
      
      ImVec4 c_tl, c_tr, c_br, c_bl;
      if (hovered) {
        // Hovered: Subtle dark red/crimson
        c_tl = ImVec4(0.38f, 0.12f, 0.12f, 0.95f);
        c_tr = ImVec4(0.30f, 0.08f, 0.08f, 0.95f);
        c_br = ImVec4(0.24f, 0.06f, 0.06f, 0.95f);
        c_bl = ImVec4(0.34f, 0.10f, 0.10f, 0.95f);
      } else {
        // Idle: Deep flat slate
        c_tl = ImVec4(0.13f, 0.15f, 0.19f, 0.95f);
        c_tr = ImVec4(0.09f, 0.11f, 0.14f, 0.95f);
        c_br = ImVec4(0.07f, 0.08f, 0.11f, 0.95f);
        c_bl = ImVec4(0.11f, 0.13f, 0.16f, 0.95f);
      }
      
      ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
          p_min, p_max,
          ImGui::ColorConvertFloat4ToU32(c_tl),
          ImGui::ColorConvertFloat4ToU32(c_tr),
          ImGui::ColorConvertFloat4ToU32(c_br),
          ImGui::ColorConvertFloat4ToU32(c_bl)
      );
      
      ImU32 border_color = ImGui::ColorConvertFloat4ToU32(
          hovered ? ImVec4(0.55f, 0.20f, 0.20f, 0.70f) : ImVec4(0.18f, 0.20f, 0.25f, 0.40f)
      );
      ImGui::GetWindowDrawList()->AddRect(p_min, p_max, border_color, 4.0f, ImDrawFlags_None, 1.0f);

      // Pixel-perfect manual rendering of the close icon
      ImVec2 glyph_size = ImGui::CalcTextSize(ICON_WINDOW_CLOSE);
      // Centered with micro-offsets to perfectly center visual weight: -1.5px X (more left), +1.5px Y (more down)
      ImVec2 text_pos = ImVec2(
          p_min.x + (button_width - glyph_size.x) * 0.5f + 0.5f,
          p_min.y + (button_height - glyph_size.y) * 0.5f + 3.2f
      );
      
      ImVec4 text_color = hovered ? ImVec4(1.00f, 1.00f, 1.00f, 1.00f) : ImVec4(0.70f, 0.73f, 0.80f, 0.85f);
      ImGui::GetWindowDrawList()->AddText(text_pos, ImGui::GetColorU32(text_color), ICON_WINDOW_CLOSE);
    }
    if (ImGui::Button("##close_nav", ImVec2(button_width, button_height))) {
      g_show_overlay = false;
      ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close");

    ImGui::PopStyleColor(3);

    ImGui::PopFont();
    ImGui::End();

    // B. Floating Feature Jendela (Modular, bebas geser, mengingat posisi via dover/imgui.ini)
    


  }
  
  // Render modular windows universally. BaseWindow handles visibility/pin logic internally.
  notes::GetNotesWindow().Render(g_show_overlay);
  settings::GetSettingsWindow().Render(g_show_overlay);

}

} // namespace dover::overlay
