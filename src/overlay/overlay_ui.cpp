#include "overlay/overlay_ui.h"
#include "shared/settings/app_config.h"
#include "overlay/hook_utils.h"
#include "shared/notes/layout.h"
#include "shared/notes/manager.h"
#include "shared/settings/settings_window.h"
#include "shared/crosshair/crosshair_window.h"
#include "shared/input/input_window.h"
#include "shared/game_storage.h"
#include "shared/icons.h"
#include "shared/fonts.h"
#include "shared/theme.h"
#include "overlay/input_hook.h"
#include "shared/storage.h"
#include "shared/config.h"
#include "shared/input_utils.h"

#include <windows.h>
#include <psapi.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/freetype/imgui_freetype.h>
#include <imgui_impl_win32.h>
#include <cstring>
#include <string>

#pragma comment(lib, "psapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace dover::overlay {

OverlayState& GetOverlayState() {
    static OverlayState s;
    return s;
}



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


struct NavButtonState {
    bool is_open;
    bool is_focused;
};

static void RenderNavButton(
    const char* icon,
    const NavButtonState& state,
    const ImVec4& active_shadow_color,
    const ImVec4& active_border_color,
    float icon_btn_width,
    float icon_box_height
) {
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImVec2 p_max = ImVec2(pos.x + icon_btn_width, pos.y + icon_box_height);
      bool hovered = ImGui::IsMouseHoveringRect(pos, p_max);
      
      ImVec2 glyph_size = ImGui::CalcTextSize(icon);
      ImVec2 text_pos = ImVec2(pos.x + (icon_btn_width - glyph_size.x) * 0.5f, pos.y + (icon_box_height - glyph_size.y) * 0.5f);
      
      ImVec4 text_color, shadow_color, highlight_color, border_color;
      if (state.is_open) {
        text_color      = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        highlight_color = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);
        shadow_color    = active_shadow_color;
        border_color    = active_border_color;
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
      ImGui::GetWindowDrawList()->AddText(dover::shared::g_font_panel, dover::shared::g_font_panel->FontSize, ImVec2(text_pos.x + 1.0f, text_pos.y + 1.5f), ImGui::ColorConvertFloat4ToU32(shadow_color), icon);
      ImGui::GetWindowDrawList()->AddText(dover::shared::g_font_panel, dover::shared::g_font_panel->FontSize, ImVec2(text_pos.x - 0.5f, text_pos.y - 0.5f), ImGui::ColorConvertFloat4ToU32(highlight_color), icon);

      // Draw the crisp main icon text inside the gradient box
      ImGui::GetWindowDrawList()->AddText(dover::shared::g_font_panel, dover::shared::g_font_panel->FontSize, text_pos, ImGui::ColorConvertFloat4ToU32(text_color), icon);

      // Draw active indicator (Long line if focused, small dot if open but not focused)
      if (state.is_open) {
        float indicator_y = pos.y + icon_box_height + 2.0f;
        if (state.is_focused) {
          ImVec2 l_start(pos.x + 6.0f, indicator_y);
          ImVec2 l_end(pos.x + icon_btn_width - 6.0f, indicator_y + 2.0f);
          ImGui::GetWindowDrawList()->AddRectFilled(l_start, l_end, ImGui::ColorConvertFloat4ToU32(ImVec4(0.20f, 0.65f, 1.00f, 1.00f)), 1.0f);
        } else {
          ImVec2 dot_center(pos.x + icon_btn_width * 0.5f, indicator_y + 1.0f);
          ImGui::GetWindowDrawList()->AddCircleFilled(dot_center, 2.0f, ImGui::ColorConvertFloat4ToU32(ImVec4(0.70f, 0.73f, 0.80f, 0.85f)), 16);
        }
      }
}

void RenderImGuiUI() {
  if (!dover::shared::GameStorage::Get().IsConfigFlushReady()) {
      if (dover::shared::GameStorage::Get().TestAndClearConfigCaptureRequested()) {
          dover::shared::GameStorage::Get().ExecuteConfigCapture();
          dover::shared::GameStorage::Get().SetConfigFlushReady();
      }
  }
  if (!dover::shared::GameStorage::Get().IsStateFlushReady()) {
      if (dover::shared::GameStorage::Get().TestAndClearStateCaptureRequested()) {
          dover::shared::GameStorage::Get().ExecuteStateCapture();
          dover::shared::GameStorage::Get().SetStateFlushReady();
      }
  }

  static bool s_last_show_overlay = false;
  bool curr_show_overlay = GetOverlayState().show_overlay.load(std::memory_order_acquire);
  if (curr_show_overlay != s_last_show_overlay) {
    ImGuiIO& io = ImGui::GetIO();
    if (curr_show_overlay) {
      io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
      io.ConfigFlags |= (ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard);
    } else {
      io.ClearInputKeys();
      io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
      io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard);
      
      // Interactive overlay was just closed:
      // 1. Force-flush active ImGui text input widgets so latest edits are written to our buffer
      ImGui::ClearActiveID();
      // 2. Flush notes editor buffer to note content
      shared::notes::GetNotesWindow().FlushEditBuffer();
      // 3. Save all dirty notes immediately
      shared::notes::AutoSaveAll();
    }
    s_last_show_overlay = curr_show_overlay;
  }

  // 1. Draw Pinned Info Window (transparent corner overlay) - Hidden when interactive overlay is active
  if (!curr_show_overlay && (shared::GetAppConfig().show_fps || shared::GetAppConfig().show_clock || shared::GetAppConfig().show_api)) {
    ImGui::SetNextWindowPos(ImVec2(12.0f, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("Info Window", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);

    if (shared::GetAppConfig().show_fps) {
      TickFPS();
    }

    if (shared::GetAppConfig().show_clock) {
      SYSTEMTIME time{};
      GetLocalTime(&time);
      ImGui::TextColored(ImVec4(0.20f, 1.00f, 0.70f, 1.00f), "%02u:%02u:%02u", time.wHour, time.wMinute, time.wSecond);
    }
    
    if (shared::GetAppConfig().show_fps) {
      ImGui::TextColored(ImVec4(1.00f, 1.00f, 1.00f, 1.00f), "FPS:  %.1f", g_fps_value);
    }
    
    if (shared::GetAppConfig().show_api) {
      ImGui::TextColored(ImVec4(1.00f, 0.80f, 0.20f, 1.00f), "API:  %s", GetOverlayState().active_dx_version.load(std::memory_order_acquire));
    }
    
    ImGui::End();
  }

  // 2. Draw Steam-style Interactive Navigation and floating windows if visible
  if (curr_show_overlay) {
    // Dim the underlying game frame for premium presentation
    ImVec2 display_size = ImGui::GetIO().DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), display_size, IM_COL32(0, 0, 0, (int)(shared::GetAppConfig().overlay_bg_alpha * 255.0f)));

    bool show_notes = shared::notes::GetNotesWindow().IsOpen();

    // A. Top Navigation Bar (Fixed persistent toolbar at the top)
    const float bar_height = 40.0f;
    const float bar_padding_x = 14.0f;
    const float button_width = 28.0f;
    const float button_height = 28.0f;
    const float button_spacing = 8.0f;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(display_size.x, bar_height), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    
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

    ImGui::PushFont(dover::shared::g_font_panel);

    const float icon_btn_width = 34.0f;
    const float button_group_width = (icon_btn_width * 4.0f) + (button_spacing * 3.0f); // Group of 4 icon buttons
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
      NavButtonState notes_state{show_notes, show_notes && shared::notes::GetNotesWindow().IsFocused()};
      RenderNavButton(
          ICON_PANEL_NOTES, notes_state,
          ImVec4(0.00f, 0.45f, 0.85f, 0.85f),  // active shadow
          ImVec4(0.20f, 0.65f, 1.00f, 0.85f),  // active border
          icon_btn_width, icon_box_height
      );
    }
    if (ImGui::Button(ICON_PANEL_NOTES, ImVec2(icon_btn_width, icon_box_height))) {
      if (!show_notes) {
        shared::notes::GetNotesWindow().Open();
        ImGui::SetWindowFocus("Notes");
      } else if (!shared::notes::GetNotesWindow().IsFocused()) {
        ImGui::SetWindowFocus("Notes");
      }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Notes");

    // B. Settings Button Rendering (Modern Dot / Line Indicator + Taskbar Clicking Mechanism)
    ImGui::SameLine(0.0f, button_spacing);
    {
      bool is_settings_open = shared::settings::GetSettingsWindow().IsOpen();
      bool is_settings_focused = shared::settings::GetSettingsWindow().IsFocused();
      NavButtonState settings_state{is_settings_open, is_settings_focused};
      RenderNavButton(
          ICON_PANEL_SETTINGS, settings_state,
          ImVec4(0.55f, 0.30f, 0.90f, 0.85f),  // active shadow
          ImVec4(0.68f, 0.45f, 0.95f, 0.85f),  // active border
          icon_btn_width, icon_box_height
      );
    }
    if (ImGui::Button(ICON_PANEL_SETTINGS, ImVec2(icon_btn_width, icon_box_height))) {
      if (!shared::settings::GetSettingsWindow().IsOpen()) {
        shared::settings::GetSettingsWindow().Open();
        ImGui::SetWindowFocus("Settings");
      } else if (!shared::settings::GetSettingsWindow().IsFocused()) {
        ImGui::SetWindowFocus("Settings");
      }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Settings");
    
    // B2. Crosshair Button Rendering
    ImGui::SameLine(0.0f, button_spacing);
    {
      bool is_ch_open = shared::crosshair::GetCrosshairWindow().IsOpen();
      bool is_ch_focused = shared::crosshair::GetCrosshairWindow().IsFocused();
      NavButtonState ch_state{is_ch_open, is_ch_focused};
      RenderNavButton(
          ICON_PANEL_RETICLE, ch_state,
          ImVec4(0.90f, 0.40f, 0.30f, 0.85f),  // active shadow
          ImVec4(0.95f, 0.55f, 0.45f, 0.85f),  // active border
          icon_btn_width, icon_box_height
      );
    }
    if (ImGui::Button(ICON_PANEL_RETICLE, ImVec2(icon_btn_width, icon_box_height))) {
      if (!shared::crosshair::GetCrosshairWindow().IsOpen()) {
        shared::crosshair::GetCrosshairWindow().Open();
        ImGui::SetWindowFocus("Crosshairs");
      } else if (!shared::crosshair::GetCrosshairWindow().IsFocused()) {
        ImGui::SetWindowFocus("Crosshairs");
      }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Crosshair");

    // B3. Input Map Button Rendering
    ImGui::SameLine(0.0f, button_spacing);
    {
      bool is_input_open = shared::input::GetInputWindow().IsOpen();
      bool is_input_focused = shared::input::GetInputWindow().IsFocused();
      NavButtonState input_state{is_input_open, is_input_focused};
      RenderNavButton(
          ICON_PANEL_INPUTMAP, input_state,
          ImVec4(0.20f, 0.85f, 0.45f, 0.85f),  // active shadow (greenish)
          ImVec4(0.40f, 0.95f, 0.55f, 0.85f),  // active border
          icon_btn_width, icon_box_height
      );
    }
    if (ImGui::Button(ICON_PANEL_INPUTMAP, ImVec2(icon_btn_width, icon_box_height))) {
      if (!shared::input::GetInputWindow().IsOpen()) {
        shared::input::GetInputWindow().Open();
        ImGui::SetWindowFocus("Input Mapper");
      } else if (!shared::input::GetInputWindow().IsFocused()) {
        ImGui::SetWindowFocus("Input Mapper");
      }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Input Mapper");

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
      SetOverlayVisible(false);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close");

    ImGui::PopStyleColor(3);

    ImGui::PopFont();
    ImGui::End();

    // B. Floating Feature Jendela (Modular, bebas geser, mengingat posisi via dover/imgui.ini)
    


  }
  
  // Render modular windows universally. BaseWindow handles visibility/pin logic internally.
  shared::notes::GetNotesWindow().Render(GetOverlayState().show_overlay);
  shared::settings::GetSettingsWindow().Render(GetOverlayState().show_overlay);
  shared::crosshair::GetCrosshairWindow().Render(GetOverlayState().show_overlay);
  shared::input::GetInputWindow().Render(GetOverlayState().show_overlay);
  
  // Render the crosshair directly on the background draw list
  shared::crosshair::GetCrosshairWindow().RenderCrosshairOverlay();
  
  // Render the gamepad visualizer directly on the background draw list
  shared::input::GetInputWindow().RenderGamepadOverlay();
}

namespace {
struct ConfigSnapshot {
    shared::AppConfig app;
    bool window_notes_maximized;
    bool window_notes_fullscreen;
    bool window_notes_pinned;
    bool window_settings_maximized;
    bool window_settings_fullscreen;
    bool window_settings_pinned;
    bool window_crosshair_maximized;
    bool window_crosshair_fullscreen;
    bool window_crosshair_pinned;
    bool window_input_maximized;
    bool window_input_fullscreen;
    bool window_input_pinned;
};
static ConfigSnapshot s_cfg_snap;

struct StateSnapshot {
    char selected_note_filename[256];
    int notes_view_mode;
    int notes_zoom_idx;
    int notes_sort_criteria;
    bool notes_sort_ascending;
    bool notes_is_open;
    
    int settings_selected_category;
    bool settings_is_open;
    
    bool crosshair_is_open;
    bool crosshair_is_active;
    int crosshair_selected_index;
    ImVec4 crosshair_color;
    bool crosshair_outline_enabled;
    ImVec4 crosshair_outline_color;
    float crosshair_scale;
    float crosshair_opacity;
    float crosshair_pos_x;
    float crosshair_pos_y;
    
    bool inputmap_is_open;
};
static StateSnapshot s_st_snap;
} // namespace

void InitializeOverlay() {
    // 1. Resolve game exe name
    wchar_t exe_name_w[MAX_PATH] = {};
    GetModuleBaseNameW(GetCurrentProcess(), nullptr, exe_name_w, MAX_PATH);
    std::wstring exe_name(exe_name_w);

    // 2. Init GameStorage
    dover::shared::GameStorage::Get().Initialize(exe_name);

    // 3. Set ImGui INI path
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = dover::shared::GameStorage::Get().GetLayoutPathCStr();

    // 4. Setup theme (font + colors)
    dover::shared::SetupImGuiTheme();

    // 5. Init subsystems
    shared::notes::InitializeNotesManager(dover::shared::GameStorage::Get().GetNotesDir());
    shared::notes::GetNotesWindow().Initialize();
    shared::settings::GetSettingsWindow().Initialize();
    shared::crosshair::GetCrosshairWindow().Initialize();
    shared::input::GetInputWindow().Initialize();

    // Register callbacks
    dover::shared::GameStorage::Get().RegisterConfigLoad([](const std::filesystem::path& cfg) {
        shared::GetAppConfig().show_fps   = shared::ReadIniBool(cfg, "osd", "show_fps",   true);
        shared::GetAppConfig().show_clock = shared::ReadIniBool(cfg, "osd", "show_clock", true);
        shared::GetAppConfig().show_api   = shared::ReadIniBool(cfg, "osd", "show_api",   false);
        shared::GetAppConfig().show_gamepad_hud     = shared::ReadIniBool(cfg, "osd", "show_gamepad_hud", false);
        shared::GetAppConfig().gamepad_hud_position = shared::ReadIniInt(cfg, "osd", "gamepad_hud_position", 2);
        shared::GetAppConfig().gamepad_hud_scale    = shared::ReadIniFloat(cfg, "osd", "gamepad_hud_scale", 1.0f);

        shared::GetAppConfig().global_window_alpha = shared::ReadIniFloat(cfg, "theme", "window_alpha", 0.95f);
        shared::GetAppConfig().overlay_bg_alpha    = shared::ReadIniFloat(cfg, "theme", "overlay_alpha", 0.63f);

        shared::GetAppConfig().hotkey_toggle_main = shared::ReadIniInt(cfg, "hotkeys", "toggle_main", VK_TAB);
        shared::GetAppConfig().hotkey_toggle_modifier = shared::ReadIniInt(cfg, "hotkeys", "toggle_modifier", VK_SHIFT);

        for (int i = 0; i < 18; ++i) {
            char key[32];
            snprintf(key, sizeof(key), "map_%d", i);
            shared::GetAppConfig().gamepad_to_vk_map[i].vk_code = static_cast<uint8_t>(shared::ReadIniInt(cfg, "input", key, 0));
            
            snprintf(key, sizeof(key), "map_%d_ctrl", i);
            shared::GetAppConfig().gamepad_to_vk_map[i].modifier_ctrl = shared::ReadIniBool(cfg, "input", key, false);
            
            snprintf(key, sizeof(key), "map_%d_shift", i);
            shared::GetAppConfig().gamepad_to_vk_map[i].modifier_shift = shared::ReadIniBool(cfg, "input", key, false);
            
            snprintf(key, sizeof(key), "map_%d_alt", i);
            shared::GetAppConfig().gamepad_to_vk_map[i].modifier_alt = shared::ReadIniBool(cfg, "input", key, false);
        }

        shared::notes::GetNotesWindow().SetBgAlpha(shared::GetAppConfig().global_window_alpha);
        shared::settings::GetSettingsWindow().SetBgAlpha(shared::GetAppConfig().global_window_alpha);

        auto load_window_state = [&](const char* id, shared::ui::BaseWindow& wnd) {
            wnd.SetMaximized(shared::ReadIniBool(cfg, id, "maximized", false));
            wnd.SetFullscreen(shared::ReadIniBool(cfg, id, "fullscreen", false));
            wnd.SetPinned(shared::ReadIniBool(cfg, id, "pinned", false));
        };
        load_window_state("window_notes", shared::notes::GetNotesWindow());
        load_window_state("window_settings", shared::settings::GetSettingsWindow());
        load_window_state("window_crosshair", shared::crosshair::GetCrosshairWindow());
        load_window_state("window_input", shared::input::GetInputWindow());
    });

    dover::shared::GameStorage::Get().RegisterConfigCapture([]() {
        s_cfg_snap.app = shared::GetAppConfig();
        s_cfg_snap.window_notes_maximized = shared::notes::GetNotesWindow().IsMaximized();
        s_cfg_snap.window_notes_fullscreen = shared::notes::GetNotesWindow().IsFullscreen();
        s_cfg_snap.window_notes_pinned = shared::notes::GetNotesWindow().IsPinned();
        s_cfg_snap.window_settings_maximized = shared::settings::GetSettingsWindow().IsMaximized();
        s_cfg_snap.window_settings_fullscreen = shared::settings::GetSettingsWindow().IsFullscreen();
        s_cfg_snap.window_settings_pinned = shared::settings::GetSettingsWindow().IsPinned();
        s_cfg_snap.window_crosshair_maximized = shared::crosshair::GetCrosshairWindow().IsMaximized();
        s_cfg_snap.window_crosshair_fullscreen = shared::crosshair::GetCrosshairWindow().IsFullscreen();
        s_cfg_snap.window_crosshair_pinned = shared::crosshair::GetCrosshairWindow().IsPinned();
        s_cfg_snap.window_input_maximized = shared::input::GetInputWindow().IsMaximized();
        s_cfg_snap.window_input_fullscreen = shared::input::GetInputWindow().IsFullscreen();
        s_cfg_snap.window_input_pinned = shared::input::GetInputWindow().IsPinned();
    });

    dover::shared::GameStorage::Get().RegisterStateCapture([]() {
        std::string fn = shared::notes::GetNotesWindow().GetSelectedNoteFilename();
        strncpy_s(s_st_snap.selected_note_filename, sizeof(s_st_snap.selected_note_filename), fn.c_str(), _TRUNCATE);
        s_st_snap.notes_view_mode = shared::notes::GetNotesWindow().GetViewMode();
        s_st_snap.notes_zoom_idx = shared::notes::GetNotesWindow().GetZoomIndex();
        s_st_snap.notes_sort_criteria = static_cast<int>(shared::notes::GetSortCriteria());
        s_st_snap.notes_sort_ascending = shared::notes::IsSortAscending();
        s_st_snap.notes_is_open = shared::notes::GetNotesWindow().IsOpen();
        
        s_st_snap.settings_selected_category = shared::settings::GetSettingsWindow().GetSelectedCategory();
        s_st_snap.settings_is_open = shared::settings::GetSettingsWindow().IsOpen();
        
        s_st_snap.crosshair_is_open = shared::crosshair::GetCrosshairWindow().IsOpen();
        s_st_snap.crosshair_is_active = shared::crosshair::GetCrosshairWindow().IsCrosshairActive();
        s_st_snap.crosshair_selected_index = shared::crosshair::GetCrosshairWindow().GetSelectedIndex();
        s_st_snap.crosshair_color = shared::crosshair::GetCrosshairWindow().GetColor();
        s_st_snap.crosshair_outline_enabled = shared::crosshair::GetCrosshairWindow().IsOutlineEnabled();
        s_st_snap.crosshair_outline_color = shared::crosshair::GetCrosshairWindow().GetOutlineColor();
        s_st_snap.crosshair_scale = shared::crosshair::GetCrosshairWindow().GetScale();
        s_st_snap.crosshair_opacity = shared::crosshair::GetCrosshairWindow().GetOpacity();
        s_st_snap.crosshair_pos_x = shared::crosshair::GetCrosshairWindow().GetPosX();
        s_st_snap.crosshair_pos_y = shared::crosshair::GetCrosshairWindow().GetPosY();
        
        s_st_snap.inputmap_is_open = shared::input::GetInputWindow().IsOpen();
    });

    dover::shared::GameStorage::Get().RegisterConfigSave([](const std::filesystem::path& cfg) {
        shared::WriteIniBool(cfg, "osd", "show_fps",   s_cfg_snap.app.show_fps);
        shared::WriteIniBool(cfg, "osd", "show_clock", s_cfg_snap.app.show_clock);
        shared::WriteIniBool(cfg, "osd", "show_api",   s_cfg_snap.app.show_api);
        shared::WriteIniBool(cfg, "osd", "show_gamepad_hud",     s_cfg_snap.app.show_gamepad_hud);
        shared::WriteIniInt(cfg, "osd", "gamepad_hud_position", s_cfg_snap.app.gamepad_hud_position);
        shared::WriteIniFloat(cfg, "osd", "gamepad_hud_scale",    s_cfg_snap.app.gamepad_hud_scale);

        shared::WriteIniFloat(cfg, "theme", "window_alpha",  s_cfg_snap.app.global_window_alpha);
        shared::WriteIniFloat(cfg, "theme", "overlay_alpha", s_cfg_snap.app.overlay_bg_alpha);

        shared::WriteIniInt(cfg, "hotkeys", "toggle_main", s_cfg_snap.app.hotkey_toggle_main);
        shared::WriteIniInt(cfg, "hotkeys", "toggle_modifier", s_cfg_snap.app.hotkey_toggle_modifier);

        for (int i = 0; i < 18; ++i) {
            char key[32];
            snprintf(key, sizeof(key), "map_%d", i);
            shared::WriteIniInt(cfg, "input", key, s_cfg_snap.app.gamepad_to_vk_map[i].vk_code);
            
            snprintf(key, sizeof(key), "map_%d_ctrl", i);
            shared::WriteIniBool(cfg, "input", key, s_cfg_snap.app.gamepad_to_vk_map[i].modifier_ctrl);
            
            snprintf(key, sizeof(key), "map_%d_shift", i);
            shared::WriteIniBool(cfg, "input", key, s_cfg_snap.app.gamepad_to_vk_map[i].modifier_shift);
            
            snprintf(key, sizeof(key), "map_%d_alt", i);
            shared::WriteIniBool(cfg, "input", key, s_cfg_snap.app.gamepad_to_vk_map[i].modifier_alt);
        }

        auto save_window_state = [&](const char* id, bool is_max, bool is_full, bool is_pin) {
            shared::WriteIniBool(cfg, id, "maximized", is_max);
            shared::WriteIniBool(cfg, id, "fullscreen", is_full);
            shared::WriteIniBool(cfg, id, "pinned", is_pin);
        };
        save_window_state("window_notes", s_cfg_snap.window_notes_maximized, s_cfg_snap.window_notes_fullscreen, s_cfg_snap.window_notes_pinned);
        save_window_state("window_settings", s_cfg_snap.window_settings_maximized, s_cfg_snap.window_settings_fullscreen, s_cfg_snap.window_settings_pinned);
        save_window_state("window_crosshair", s_cfg_snap.window_crosshair_maximized, s_cfg_snap.window_crosshair_fullscreen, s_cfg_snap.window_crosshair_pinned);
        save_window_state("window_input", s_cfg_snap.window_input_maximized, s_cfg_snap.window_input_fullscreen, s_cfg_snap.window_input_pinned);
    });

    dover::shared::GameStorage::Get().RegisterStateLoad([](const std::filesystem::path& st) {
        char note_file[64] = {};
        shared::ReadIniString(st, "notes", "selected_note_filename", "", note_file, sizeof(note_file));
        
        int view_mode = shared::ReadIniInt(st, "notes", "view_mode", 1);
        if (note_file[0] != '\0') {
            shared::notes::GetNotesWindow().SelectNoteByFilename(note_file);
        } else {
            shared::notes::GetNotesWindow().SelectNote(0, false);
        }
        shared::notes::GetNotesWindow().SetViewMode(view_mode);
        
        int sort_crit = shared::ReadIniInt(st, "notes", "sort_criteria", 0);
        bool sort_asc = shared::ReadIniBool(st, "notes", "sort_ascending", true);
        shared::notes::SetSortMode(static_cast<shared::notes::NoteSortCriteria>(sort_crit), sort_asc);

        int zoom_idx = shared::ReadIniInt(st, "notes", "zoom_idx", 2);
        shared::notes::GetNotesWindow().SetZoomIndex(zoom_idx);

        int settings_cat = shared::ReadIniInt(st, "settings", "selected_category", 0);
        shared::settings::GetSettingsWindow().SetSelectedCategory(settings_cat);

        bool notes_open = shared::ReadIniBool(st, "notes", "is_open", false);
        shared::notes::GetNotesWindow().SetOpenDirect(notes_open);

        bool settings_open = shared::ReadIniBool(st, "settings", "is_open", false);
        shared::settings::GetSettingsWindow().SetOpenDirect(settings_open);

        bool crosshair_open = shared::ReadIniBool(st, "crosshair", "is_open", false);
        shared::crosshair::GetCrosshairWindow().SetOpenDirect(crosshair_open);
        
        bool input_open = shared::ReadIniBool(st, "inputmap", "is_open", false);
        shared::input::GetInputWindow().SetOpenDirect(input_open);
        
        bool crosshair_active = shared::ReadIniBool(st, "crosshair", "is_active", false);
        shared::crosshair::GetCrosshairWindow().SetCrosshairActive(crosshair_active);
        
        int crosshair_idx = shared::ReadIniInt(st, "crosshair", "selected_index", 0);
        shared::crosshair::GetCrosshairWindow().SetSelectedIndex(crosshair_idx);
        
        float cr = shared::ReadIniFloat(st, "crosshair", "color_r", 1.0f);
        float cg = shared::ReadIniFloat(st, "crosshair", "color_g", 1.0f);
        float cb = shared::ReadIniFloat(st, "crosshair", "color_b", 1.0f);
        float ca = shared::ReadIniFloat(st, "crosshair", "color_a", 1.0f);
        shared::crosshair::GetCrosshairWindow().SetColor(ImVec4(cr, cg, cb, ca));
        
        bool coutline = shared::ReadIniBool(st, "crosshair", "outline_enabled", false);
        shared::crosshair::GetCrosshairWindow().SetOutlineEnabled(coutline);
        
        float ocr = shared::ReadIniFloat(st, "crosshair", "outline_r", 0.0f);
        float ocg = shared::ReadIniFloat(st, "crosshair", "outline_g", 0.0f);
        float ocb = shared::ReadIniFloat(st, "crosshair", "outline_b", 0.0f);
        float oca = shared::ReadIniFloat(st, "crosshair", "outline_a", 1.0f);
        shared::crosshair::GetCrosshairWindow().SetOutlineColor(ImVec4(ocr, ocg, ocb, oca));
        
        float cscale = shared::ReadIniFloat(st, "crosshair", "scale", 1.0f);
        shared::crosshair::GetCrosshairWindow().SetScale(cscale);

        float copacity = shared::ReadIniFloat(st, "crosshair", "opacity", 1.0f);
        shared::crosshair::GetCrosshairWindow().SetOpacity(copacity);
        
        float cpos_x = shared::ReadIniFloat(st, "crosshair", "pos_x", 0.0f);
        float cpos_y = shared::ReadIniFloat(st, "crosshair", "pos_y", 0.0f);
        shared::crosshair::GetCrosshairWindow().SetPosX(cpos_x);
        shared::crosshair::GetCrosshairWindow().SetPosY(cpos_y);
    });

    dover::shared::GameStorage::Get().RegisterStateSave([](const std::filesystem::path& st) {
        shared::WriteIniString(st, "notes", "selected_note_filename", s_st_snap.selected_note_filename);
        shared::WriteIniInt(st, "notes", "view_mode",           s_st_snap.notes_view_mode);
        shared::WriteIniInt(st, "notes", "zoom_idx",            s_st_snap.notes_zoom_idx);
        shared::WriteIniInt(st, "notes", "sort_criteria",       s_st_snap.notes_sort_criteria);
        shared::WriteIniBool(st, "notes", "sort_ascending",     s_st_snap.notes_sort_ascending);
        shared::WriteIniInt(st, "settings", "selected_category", s_st_snap.settings_selected_category);

        shared::WriteIniBool(st, "notes", "is_open",            s_st_snap.notes_is_open);
        shared::WriteIniBool(st, "settings", "is_open",         s_st_snap.settings_is_open);
        shared::WriteIniBool(st, "crosshair", "is_open",        s_st_snap.crosshair_is_open);
        shared::WriteIniBool(st, "inputmap", "is_open",         s_st_snap.inputmap_is_open);
        
        shared::WriteIniBool(st, "crosshair", "is_active",      s_st_snap.crosshair_is_active);
        shared::WriteIniInt(st, "crosshair", "selected_index",  s_st_snap.crosshair_selected_index);
        
        shared::WriteIniFloat(st, "crosshair", "color_r",       s_st_snap.crosshair_color.x);
        shared::WriteIniFloat(st, "crosshair", "color_g",       s_st_snap.crosshair_color.y);
        shared::WriteIniFloat(st, "crosshair", "color_b",       s_st_snap.crosshair_color.z);
        shared::WriteIniFloat(st, "crosshair", "color_a",       s_st_snap.crosshair_color.w);
        
        shared::WriteIniBool(st, "crosshair", "outline_enabled",s_st_snap.crosshair_outline_enabled);
        shared::WriteIniFloat(st, "crosshair", "outline_r",     s_st_snap.crosshair_outline_color.x);
        shared::WriteIniFloat(st, "crosshair", "outline_g",     s_st_snap.crosshair_outline_color.y);
        shared::WriteIniFloat(st, "crosshair", "outline_b",     s_st_snap.crosshair_outline_color.z);
        shared::WriteIniFloat(st, "crosshair", "outline_a",     s_st_snap.crosshair_outline_color.w);
        
        shared::WriteIniFloat(st, "crosshair", "scale",         s_st_snap.crosshair_scale);
        shared::WriteIniFloat(st, "crosshair", "opacity",       s_st_snap.crosshair_opacity);
        shared::WriteIniFloat(st, "crosshair", "pos_x",         s_st_snap.crosshair_pos_x);
        shared::WriteIniFloat(st, "crosshair", "pos_y",         s_st_snap.crosshair_pos_y);
    });

    // 6. Load persistent config/state
    dover::shared::GameStorage::Get().LoadConfig();
    dover::shared::GameStorage::Get().LoadState();
}

} // namespace dover::overlay
