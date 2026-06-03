#include "overlay/overlay_ui.h"
#include "shared/settings/app_config.h"
#include "overlay/hook_utils.h"
#include "shared/notes/layout.h"
#include "shared/notes/manager.h"
#include "shared/settings/settings_window.h"
#include "overlay/crosshair/crosshair_window.h"
#include "overlay/input/input_window.h"
#include "shared/game_storage.h"
#include "shared/icons.h"
#include "shared/fonts.h"
#include "shared/theme.h"
#include "overlay/input_hook.h"
#include "shared/storage.h"
#include "shared/config.h"

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
      ImVec2 text_pos = ImVec2(pos.x + (icon_btn_width - glyph_size.x) * 0.5f, pos.y + (icon_box_height - glyph_size.y) * 0.5f + 2.5f);
      
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
  PollGamepadToggle();

  // 1. Draw Pinned Info Window (transparent corner overlay) - Hidden when interactive overlay is active
  if (!GetOverlayState().show_overlay && (shared::GetAppConfig().show_fps || shared::GetAppConfig().show_clock || shared::GetAppConfig().show_api)) {
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
      ImGui::TextColored(ImVec4(1.00f, 0.80f, 0.20f, 1.00f), "API:  %s", GetOverlayState().active_dx_version);
    }
    
    ImGui::End();
  }

  // 2. Draw Steam-style Interactive Navigation and floating windows if visible
  if (GetOverlayState().show_overlay) {
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
      bool is_ch_open = crosshair::GetCrosshairWindow().IsOpen();
      bool is_ch_focused = crosshair::GetCrosshairWindow().IsFocused();
      NavButtonState ch_state{is_ch_open, is_ch_focused};
      RenderNavButton(
          ICON_PANEL_RETICLE, ch_state,
          ImVec4(0.90f, 0.40f, 0.30f, 0.85f),  // active shadow
          ImVec4(0.95f, 0.55f, 0.45f, 0.85f),  // active border
          icon_btn_width, icon_box_height
      );
    }
    if (ImGui::Button(ICON_PANEL_RETICLE, ImVec2(icon_btn_width, icon_box_height))) {
      if (!crosshair::GetCrosshairWindow().IsOpen()) {
        crosshair::GetCrosshairWindow().Open();
        ImGui::SetWindowFocus("Crosshairs");
      } else if (!crosshair::GetCrosshairWindow().IsFocused()) {
        ImGui::SetWindowFocus("Crosshairs");
      }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Crosshair");

    // B3. Input Map Button Rendering
    ImGui::SameLine(0.0f, button_spacing);
    {
      bool is_input_open = input::GetInputWindow().IsOpen();
      bool is_input_focused = input::GetInputWindow().IsFocused();
      NavButtonState input_state{is_input_open, is_input_focused};
      RenderNavButton(
          ICON_PANEL_INPUTMAP, input_state,
          ImVec4(0.20f, 0.85f, 0.45f, 0.85f),  // active shadow (greenish)
          ImVec4(0.40f, 0.95f, 0.55f, 0.85f),  // active border
          icon_btn_width, icon_box_height
      );
    }
    if (ImGui::Button(ICON_PANEL_INPUTMAP, ImVec2(icon_btn_width, icon_box_height))) {
      if (!input::GetInputWindow().IsOpen()) {
        input::GetInputWindow().Open();
        ImGui::SetWindowFocus("Input Mapper");
      } else if (!input::GetInputWindow().IsFocused()) {
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
      GetOverlayState().show_overlay = false;
      ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
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
  crosshair::GetCrosshairWindow().Render(GetOverlayState().show_overlay);
  input::GetInputWindow().Render(GetOverlayState().show_overlay);
  
  // Render the crosshair directly on the background draw list
  crosshair::GetCrosshairWindow().RenderCrosshairOverlay();
  
  // Render the gamepad visualizer directly on the background draw list
  input::GetInputWindow().RenderGamepadOverlay();
}

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
    crosshair::GetCrosshairWindow().Initialize();
    input::GetInputWindow().Initialize();

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
    });

    dover::shared::GameStorage::Get().RegisterConfigSave([](const std::filesystem::path& cfg) {
        shared::WriteIniBool(cfg, "osd", "show_fps",   shared::GetAppConfig().show_fps);
        shared::WriteIniBool(cfg, "osd", "show_clock", shared::GetAppConfig().show_clock);
        shared::WriteIniBool(cfg, "osd", "show_api",   shared::GetAppConfig().show_api);
        shared::WriteIniBool(cfg, "osd", "show_gamepad_hud",     shared::GetAppConfig().show_gamepad_hud);
        shared::WriteIniInt(cfg, "osd", "gamepad_hud_position", shared::GetAppConfig().gamepad_hud_position);
        shared::WriteIniFloat(cfg, "osd", "gamepad_hud_scale",    shared::GetAppConfig().gamepad_hud_scale);

        shared::WriteIniFloat(cfg, "theme", "window_alpha",  shared::GetAppConfig().global_window_alpha);
        shared::WriteIniFloat(cfg, "theme", "overlay_alpha", shared::GetAppConfig().overlay_bg_alpha);

        shared::WriteIniInt(cfg, "hotkeys", "toggle_main", shared::GetAppConfig().hotkey_toggle_main);
        shared::WriteIniInt(cfg, "hotkeys", "toggle_modifier", shared::GetAppConfig().hotkey_toggle_modifier);

        for (int i = 0; i < 18; ++i) {
            char key[32];
            snprintf(key, sizeof(key), "map_%d", i);
            shared::WriteIniInt(cfg, "input", key, shared::GetAppConfig().gamepad_to_vk_map[i].vk_code);
            
            snprintf(key, sizeof(key), "map_%d_ctrl", i);
            shared::WriteIniBool(cfg, "input", key, shared::GetAppConfig().gamepad_to_vk_map[i].modifier_ctrl);
            
            snprintf(key, sizeof(key), "map_%d_shift", i);
            shared::WriteIniBool(cfg, "input", key, shared::GetAppConfig().gamepad_to_vk_map[i].modifier_shift);
            
            snprintf(key, sizeof(key), "map_%d_alt", i);
            shared::WriteIniBool(cfg, "input", key, shared::GetAppConfig().gamepad_to_vk_map[i].modifier_alt);
        }
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

        int zoom_idx = shared::ReadIniInt(st, "notes", "zoom_idx", 2);
        shared::notes::GetNotesWindow().SetZoomIndex(zoom_idx);

        int settings_cat = shared::ReadIniInt(st, "settings", "selected_category", 0);
        shared::settings::GetSettingsWindow().SetSelectedCategory(settings_cat);

        bool notes_open = shared::ReadIniBool(st, "notes", "is_open", false);
        shared::notes::GetNotesWindow().SetOpenDirect(notes_open);

        bool settings_open = shared::ReadIniBool(st, "settings", "is_open", false);
        shared::settings::GetSettingsWindow().SetOpenDirect(settings_open);

        bool crosshair_open = shared::ReadIniBool(st, "crosshair", "is_open", false);
        crosshair::GetCrosshairWindow().SetOpenDirect(crosshair_open);
        
        bool input_open = shared::ReadIniBool(st, "inputmap", "is_open", false);
        input::GetInputWindow().SetOpenDirect(input_open);
        
        bool crosshair_active = shared::ReadIniBool(st, "crosshair", "is_active", false);
        crosshair::GetCrosshairWindow().SetCrosshairActive(crosshair_active);
        
        int crosshair_idx = shared::ReadIniInt(st, "crosshair", "selected_index", 0);
        crosshair::GetCrosshairWindow().SetSelectedIndex(crosshair_idx);
        
        float cr = shared::ReadIniFloat(st, "crosshair", "color_r", 1.0f);
        float cg = shared::ReadIniFloat(st, "crosshair", "color_g", 1.0f);
        float cb = shared::ReadIniFloat(st, "crosshair", "color_b", 1.0f);
        float ca = shared::ReadIniFloat(st, "crosshair", "color_a", 1.0f);
        crosshair::GetCrosshairWindow().SetColor(ImVec4(cr, cg, cb, ca));
        
        bool coutline = shared::ReadIniBool(st, "crosshair", "outline_enabled", false);
        crosshair::GetCrosshairWindow().SetOutlineEnabled(coutline);
        
        float ocr = shared::ReadIniFloat(st, "crosshair", "outline_r", 0.0f);
        float ocg = shared::ReadIniFloat(st, "crosshair", "outline_g", 0.0f);
        float ocb = shared::ReadIniFloat(st, "crosshair", "outline_b", 0.0f);
        float oca = shared::ReadIniFloat(st, "crosshair", "outline_a", 1.0f);
        crosshair::GetCrosshairWindow().SetOutlineColor(ImVec4(ocr, ocg, ocb, oca));
        
        float cscale = shared::ReadIniFloat(st, "crosshair", "scale", 1.0f);
        crosshair::GetCrosshairWindow().SetScale(cscale);

        float copacity = shared::ReadIniFloat(st, "crosshair", "opacity", 1.0f);
        crosshair::GetCrosshairWindow().SetOpacity(copacity);
        
        float cpos_x = shared::ReadIniFloat(st, "crosshair", "pos_x", 0.0f);
        float cpos_y = shared::ReadIniFloat(st, "crosshair", "pos_y", 0.0f);
        crosshair::GetCrosshairWindow().SetPosX(cpos_x);
        crosshair::GetCrosshairWindow().SetPosY(cpos_y);
    });

    dover::shared::GameStorage::Get().RegisterStateSave([](const std::filesystem::path& st) {
        const char* note_fn = shared::notes::GetNotesWindow().GetSelectedNoteFilename();
        shared::WriteIniString(st, "notes", "selected_note_filename", note_fn);
        shared::WriteIniInt(st, "notes", "view_mode",           shared::notes::GetNotesWindow().GetViewMode());
        shared::WriteIniInt(st, "notes", "zoom_idx",            shared::notes::GetNotesWindow().GetZoomIndex());
        shared::WriteIniInt(st, "settings", "selected_category", shared::settings::GetSettingsWindow().GetSelectedCategory());

        shared::WriteIniBool(st, "notes", "is_open",            shared::notes::GetNotesWindow().IsOpen());
        shared::WriteIniBool(st, "settings", "is_open",         shared::settings::GetSettingsWindow().IsOpen());
        shared::WriteIniBool(st, "crosshair", "is_open",        crosshair::GetCrosshairWindow().IsOpen());
        shared::WriteIniBool(st, "inputmap", "is_open",         input::GetInputWindow().IsOpen());
        
        shared::WriteIniBool(st, "crosshair", "is_active",      crosshair::GetCrosshairWindow().IsCrosshairActive());
        shared::WriteIniInt(st, "crosshair", "selected_index",  crosshair::GetCrosshairWindow().GetSelectedIndex());
        
        const ImVec4& ccolor = crosshair::GetCrosshairWindow().GetColor();
        shared::WriteIniFloat(st, "crosshair", "color_r",       ccolor.x);
        shared::WriteIniFloat(st, "crosshair", "color_g",       ccolor.y);
        shared::WriteIniFloat(st, "crosshair", "color_b",       ccolor.z);
        shared::WriteIniFloat(st, "crosshair", "color_a",       ccolor.w);
        
        shared::WriteIniBool(st, "crosshair", "outline_enabled",crosshair::GetCrosshairWindow().IsOutlineEnabled());
        const ImVec4& ocolor = crosshair::GetCrosshairWindow().GetOutlineColor();
        shared::WriteIniFloat(st, "crosshair", "outline_r",     ocolor.x);
        shared::WriteIniFloat(st, "crosshair", "outline_g",     ocolor.y);
        shared::WriteIniFloat(st, "crosshair", "outline_b",     ocolor.z);
        shared::WriteIniFloat(st, "crosshair", "outline_a",     ocolor.w);
        
        shared::WriteIniFloat(st, "crosshair", "scale",         crosshair::GetCrosshairWindow().GetScale());
        shared::WriteIniFloat(st, "crosshair", "opacity",       crosshair::GetCrosshairWindow().GetOpacity());
        shared::WriteIniFloat(st, "crosshair", "pos_x",         crosshair::GetCrosshairWindow().GetPosX());
        shared::WriteIniFloat(st, "crosshair", "pos_y",         crosshair::GetCrosshairWindow().GetPosY());
    });

    // 6. Load persistent config/state
    dover::shared::GameStorage::Get().LoadConfig();
    dover::shared::GameStorage::Get().LoadState();
}

} // namespace dover::overlay