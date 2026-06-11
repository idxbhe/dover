#include "shared/theme.h"
#include "shared/fonts.h"
#include "shared/icons.h"


#include <windows.h>
#include <psapi.h>
#include <imgui.h>
#include <misc/freetype/imgui_freetype.h>
#include <string>

namespace dover::shared {

ImFont* g_font_gui = nullptr;
ImFont* g_font_panel = nullptr;
ImFont* g_font_editor = nullptr;
ImFont* g_font_preview = nullptr;
ImFont* g_font_preview_bold = nullptr;
ImFont* g_font_preview_italic = nullptr;
ImFont* g_font_preview_bold_italic = nullptr;

void SetupImGuiTheme() {
  ImGuiIO& io = ImGui::GetIO();
  ImFontAtlas* atlas = io.Fonts;
  atlas->TexMinWidth = 1024;
  atlas->TexMinHeight = 1024;

  // ── Step 4: Load fonts from DLL directory ───────────────────────────────
  HMODULE hMod = nullptr;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCWSTR)&SetupImGuiTheme, &hMod)) {
    wchar_t dll_path[MAX_PATH];
    if (GetModuleFileNameW(hMod, dll_path, MAX_PATH)) {
        std::wstring path_str = dll_path;
        size_t last_slash = path_str.find_last_of(L"\\/");
        if (last_slash != std::wstring::npos) {
          
          ImFontConfig cfg;
          cfg.FontDataOwnedByAtlas = false;
          cfg.OversampleH = 1;
          cfg.OversampleV = 1;
          cfg.PixelSnapH = true;
          cfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;

          ImFontConfig cfg_bold = cfg;
          cfg_bold.FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_Bold;

          ImFontConfig cfg_italic = cfg;
          cfg_italic.FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_Oblique;

          ImFontConfig cfg_bi = cfg;
          cfg_bi.FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_Bold | ImGuiFreeTypeLoaderFlags_Oblique;
          
          // 1. GUI Font - Default
          g_font_gui = io.Fonts->AddFontFromMemoryTTF((void*)g_font_main_ui_data, g_font_main_ui_data_size, kGuiSize, &cfg, io.Fonts->GetGlyphRangesDefault());
          if (!g_font_gui) {
            g_font_gui = io.Fonts->AddFontDefault();
          } else {
            static const ImWchar icon_ranges[] = { DI_ICON_MIN, DI_ICON_MAX, 0 };
            ImFontConfig icons_config;
            icons_config.FontDataOwnedByAtlas = false;
            icons_config.MergeMode = true;
            icons_config.PixelSnapH = true;
            icons_config.OversampleH = 3;
            icons_config.OversampleV = 2;
            icons_config.GlyphMinAdvanceX = 19.5f;
            icons_config.GlyphOffset.x = 0.0f;
            icons_config.GlyphOffset.y = 1.0f;
            io.Fonts->AddFontFromMemoryTTF((void*)g_icons_data, sizeof(g_icons_data), 19.5f, &icons_config, icon_ranges);
          }

            // 1b. GUI Font - Panel
            g_font_panel = io.Fonts->AddFontFromMemoryTTF((void*)g_font_main_ui_data, g_font_main_ui_data_size, kIconSize, &cfg, io.Fonts->GetGlyphRangesDefault());
            if (g_font_panel) {
            static const ImWchar icon_ranges[] = { DI_ICON_MIN, DI_ICON_MAX, 0 };
            ImFontConfig icons_config;
            icons_config.FontDataOwnedByAtlas = false;
            icons_config.MergeMode = true;
            icons_config.PixelSnapH = true;
            icons_config.OversampleH = 3;
            icons_config.OversampleV = 2;
            icons_config.GlyphMinAdvanceX = 28.0f;
            icons_config.GlyphOffset.x = 0.0f;
            icons_config.GlyphOffset.y = 2.0f;
            io.Fonts->AddFontFromMemoryTTF((void*)g_icons_data, sizeof(g_icons_data), 28.0f, &icons_config, icon_ranges);
            } else {
            g_font_panel = g_font_gui;
          }
          
          // Single Font Load (v1.92 Dynamic Fonts)
          g_font_editor = io.Fonts->AddFontFromMemoryTTF((void*)g_font_mono_data, g_font_mono_data_size, kEditorSizes[2], &cfg, io.Fonts->GetGlyphRangesDefault());
          if (!g_font_editor) g_font_editor = g_font_gui;

          g_font_preview = io.Fonts->AddFontFromMemoryTTF((void*)g_font_notes_read_data, g_font_notes_read_data_size, kPreviewSizes[2], &cfg, io.Fonts->GetGlyphRangesDefault());
          if (!g_font_preview) g_font_preview = g_font_gui;

          g_font_preview_bold = io.Fonts->AddFontFromMemoryTTF((void*)g_font_notes_read_data, g_font_notes_read_data_size, kPreviewSizes[2], &cfg_bold, io.Fonts->GetGlyphRangesDefault());
          if (!g_font_preview_bold) g_font_preview_bold = g_font_preview;

          g_font_preview_italic = io.Fonts->AddFontFromMemoryTTF((void*)g_font_notes_read_data, g_font_notes_read_data_size, kPreviewSizes[2], &cfg_italic, io.Fonts->GetGlyphRangesDefault());
          if (!g_font_preview_italic) g_font_preview_italic = g_font_preview;

          g_font_preview_bold_italic = io.Fonts->AddFontFromMemoryTTF((void*)g_font_notes_read_data, g_font_notes_read_data_size, kPreviewSizes[2], &cfg_bi, io.Fonts->GetGlyphRangesDefault());
          if (!g_font_preview_bold_italic) g_font_preview_bold_italic = g_font_preview;
          
        } // if (last_slash != npos)
      } // if (GetModuleFileNameW)
    } // if (GetModuleHandleExW)

    if (!g_font_gui) {
      g_font_gui = io.Fonts->AddFontDefault();
      g_font_editor = g_font_gui;
      g_font_preview = g_font_gui;
      g_font_preview_bold = g_font_gui;
      g_font_preview_italic = g_font_gui;
      g_font_preview_bold_italic = g_font_gui;
    }

  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 8.0f;
  style.PopupRounding = 4.0f; // Subtle rounded corners for dropdowns and context menus
  style.FrameRounding = 2.0f; // Minimal round corners for all boxes
  style.ItemSpacing.y = 4.0f; // Reduce vertical spacing globally
  style.Colors[ImGuiCol_Text] = ImVec4(0.960f, 0.965f, 0.973f, 1.0f); // #f5f6f7 dominant crisp white
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.090f, 0.102f, 0.130f, 1.0f); // #171a21
  style.Colors[ImGuiCol_TitleBg] = ImVec4(0.110f, 0.125f, 0.161f, 0.90f); // #1c2029 (Cool slate-blue)
  style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.133f, 0.153f, 0.196f, 0.95f); // #222732
  
  // Steam Slate-Blue cool accents
  style.Colors[ImGuiCol_Button] = ImVec4(0.227f, 0.267f, 0.329f, 0.70f); // #3a4454
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.290f, 0.380f, 0.520f, 0.85f); // #4a6185
  style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.350f, 0.460f, 0.630f, 1.00f);
  style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.290f, 0.380f, 0.520f, 0.85f);
  style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.350f, 0.460f, 0.630f, 1.00f);
  style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.290f, 0.380f, 0.520f, 1.00f);
  style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.350f, 0.460f, 0.630f, 1.00f);
  
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.090f, 0.102f, 0.130f, 0.60f); // #171a21 (Middle Layer)
  style.Colors[ImGuiCol_PopupBg] = ImVec4(0.063f, 0.071f, 0.086f, 0.98f); // #101216 (Darkest Layer)
  style.Colors[ImGuiCol_Border] = ImVec4(0.150f, 0.170f, 0.220f, 0.80f); // Cool border
}

} // namespace dover::shared
