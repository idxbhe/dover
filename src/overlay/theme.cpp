#include "overlay/theme.h"
#include "overlay/fonts.h"
#include "overlay/icons.h"


#include <windows.h>
#include <psapi.h>
#include <imgui.h>
#include <misc/freetype/imgui_freetype.h>
#include <string>

namespace dover::overlay {

ImFont* g_font_gui = nullptr;
ImFont* g_font_panel = nullptr;
ImFont* g_fonts_editor[5] = {};
ImFont* g_fonts_preview[5] = {};
ImFont* g_fonts_preview_bold[5] = {};
ImFont* g_fonts_preview_italic[5] = {};
ImFont* g_fonts_preview_bold_italic[5] = {};
ImFont* g_fonts_preview_h1[5] = {};
ImFont* g_fonts_preview_h2[5] = {};
ImFont* g_fonts_preview_h3[5] = {};
ImFont* g_fonts_preview_h4[5] = {};



void SetupImGuiTheme() {
  ImGuiIO& io = ImGui::GetIO();

  // ── Step 4: Load fonts from DLL directory ───────────────────────────────
  HMODULE hMod = nullptr;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCWSTR)&SetupImGuiTheme, &hMod)) {
    wchar_t dll_path[MAX_PATH];
    if (GetModuleFileNameW(hMod, dll_path, MAX_PATH)) {
        std::wstring path_str = dll_path;
        size_t last_slash = path_str.find_last_of(L"\\/");
        if (last_slash != std::wstring::npos) {
          std::wstring base_dir = path_str.substr(0, last_slash);
          
          ImFontConfig cfg;
          cfg.FontDataOwnedByAtlas = false;
          cfg.OversampleH = 1;
          cfg.OversampleV = 1;
          cfg.PixelSnapH = true;
          cfg.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;

          ImFontConfig cfg_bold = cfg;
          cfg_bold.FontBuilderFlags |= ImGuiFreeTypeBuilderFlags_Bold;

          ImFontConfig cfg_italic = cfg;
          cfg_italic.FontBuilderFlags |= ImGuiFreeTypeBuilderFlags_Oblique;

          ImFontConfig cfg_bi = cfg;
          cfg_bi.FontBuilderFlags |= ImGuiFreeTypeBuilderFlags_Bold | ImGuiFreeTypeBuilderFlags_Oblique;
          
          // 1. GUI Font - Default (Enlarged)
          g_font_gui = io.Fonts->AddFontFromMemoryTTF((void*)g_font_main_ui_data, g_font_main_ui_data_size, 18.0f, &cfg, io.Fonts->GetGlyphRangesDefault());
          if (!g_font_gui) {
            g_font_gui = io.Fonts->AddFontDefault();
          } else {
            static const ImWchar icon_ranges[] = { DI_ICON_MIN, DI_ICON_MAX, 0 };
            ImFontConfig icons_config;
            icons_config.FontDataOwnedByAtlas = false;
            icons_config.MergeMode = true;
            icons_config.PixelSnapH = true;
            icons_config.OversampleH = 2;
            icons_config.OversampleV = 1;
            icons_config.GlyphOffset.y = 3.0f; // Vertically align slightly larger icons with Mona Sans CapHeight
            io.Fonts->AddFontFromMemoryTTF((void*)g_icons_data, sizeof(g_icons_data), 19.5f, &icons_config, icon_ranges);
          }

          // 1b. GUI Font - Panel (Crisp, native larger sizes)
          g_font_panel = io.Fonts->AddFontFromMemoryTTF((void*)g_font_main_ui_data, g_font_main_ui_data_size, 20.0f, &cfg, io.Fonts->GetGlyphRangesDefault());
          if (g_font_panel) {
            static const ImWchar icon_ranges[] = { DI_ICON_MIN, DI_ICON_MAX, 0 };
            ImFontConfig icons_config;
            icons_config.FontDataOwnedByAtlas = false;
            icons_config.MergeMode = true;
            icons_config.PixelSnapH = true;
            icons_config.OversampleH = 3;
            icons_config.OversampleV = 2;
            icons_config.GlyphOffset.x = 0.0f;
            icons_config.GlyphOffset.y = 2.0f; // Vertically align for larger 20.0f font
            io.Fonts->AddFontFromMemoryTTF((void*)g_icons_data, sizeof(g_icons_data), 28.0f, &icons_config, icon_ranges);
          } else {
            g_font_panel = g_font_gui;
          }
          
          // 2. Define 5 sizes for editor and preview styles
          float editor_sizes[5]  = { 12.0f, 14.0f, 17.0f, 21.0f, 25.0f };
          float preview_sizes[5] = { 13.0f, 15.0f, 18.0f, 22.0f, 26.0f };
          float h1_sizes[5], h2_sizes[5], h3_sizes[5], h4_sizes[5];

          for (int i = 0; i < 5; ++i) {
            h1_sizes[i] = preview_sizes[i] * 2.00f;
            h2_sizes[i] = preview_sizes[i] * 1.65f;
            h3_sizes[i] = preview_sizes[i] * 1.35f;
            h4_sizes[i] = preview_sizes[i] * 1.15f;
            // Editor Font (JetBrainsMono - Monospace for code/typing)
            g_fonts_editor[i] = io.Fonts->AddFontFromMemoryTTF((void*)g_font_mono_data, g_font_mono_data_size, editor_sizes[i], &cfg, io.Fonts->GetGlyphRangesDefault());
            if (!g_fonts_editor[i]) g_fonts_editor[i] = g_font_gui;

            // Preview Font (Mona Sans Regular - Proportional premium sans-serif)
            g_fonts_preview[i] = io.Fonts->AddFontFromMemoryTTF((void*)g_font_notes_read_data, g_font_notes_read_data_size, preview_sizes[i], &cfg, io.Fonts->GetGlyphRangesDefault());
            if (!g_fonts_preview[i]) g_fonts_preview[i] = g_font_gui;

            // Preview Bold (Mona Sans Bold)
            g_fonts_preview_bold[i] = io.Fonts->AddFontFromMemoryTTF((void*)g_font_notes_read_data, g_font_notes_read_data_size, preview_sizes[i], &cfg_bold, io.Fonts->GetGlyphRangesDefault());
            if (!g_fonts_preview_bold[i]) g_fonts_preview_bold[i] = g_fonts_preview[i];

            // Preview Italic (Mona Sans Italic)
            g_fonts_preview_italic[i] = io.Fonts->AddFontFromMemoryTTF((void*)g_font_notes_read_data, g_font_notes_read_data_size, preview_sizes[i], &cfg_italic, io.Fonts->GetGlyphRangesDefault());
            if (!g_fonts_preview_italic[i]) g_fonts_preview_italic[i] = g_fonts_preview[i];

            // Preview Bold Italic (Mona Sans Bold Italic)
            g_fonts_preview_bold_italic[i] = io.Fonts->AddFontFromMemoryTTF((void*)g_font_notes_read_data, g_font_notes_read_data_size, preview_sizes[i], &cfg_bi, io.Fonts->GetGlyphRangesDefault());
            if (!g_fonts_preview_bold_italic[i]) g_fonts_preview_bold_italic[i] = g_fonts_preview[i];

            // Headings (Mona Sans Headings)
            g_fonts_preview_h1[i] = io.Fonts->AddFontFromMemoryTTF((void*)g_font_notes_read_data, g_font_notes_read_data_size, h1_sizes[i], &cfg_bold, io.Fonts->GetGlyphRangesDefault());
            if (!g_fonts_preview_h1[i]) g_fonts_preview_h1[i] = g_fonts_preview[i];

            g_fonts_preview_h2[i] = io.Fonts->AddFontFromMemoryTTF((void*)g_font_notes_read_data, g_font_notes_read_data_size, h2_sizes[i], &cfg_bold, io.Fonts->GetGlyphRangesDefault());
            if (!g_fonts_preview_h2[i]) g_fonts_preview_h2[i] = g_fonts_preview[i];

            g_fonts_preview_h3[i] = io.Fonts->AddFontFromMemoryTTF((void*)g_font_notes_read_data, g_font_notes_read_data_size, h3_sizes[i], &cfg_bold, io.Fonts->GetGlyphRangesDefault());
            if (!g_fonts_preview_h3[i]) g_fonts_preview_h3[i] = g_fonts_preview[i];

            g_fonts_preview_h4[i] = io.Fonts->AddFontFromMemoryTTF((void*)g_font_notes_read_data, g_font_notes_read_data_size, h4_sizes[i], &cfg_bold, io.Fonts->GetGlyphRangesDefault());
            if (!g_fonts_preview_h4[i]) g_fonts_preview_h4[i] = g_fonts_preview[i];
          }
        }
      }
    }

    if (!g_font_gui) {
      g_font_gui = io.Fonts->AddFontDefault();
      for (int i = 0; i < 5; ++i) {
        g_fonts_editor[i] = g_font_gui;
        g_fonts_preview[i] = g_font_gui;
        g_fonts_preview_bold[i] = g_font_gui;
        g_fonts_preview_italic[i] = g_font_gui;
        g_fonts_preview_bold_italic[i] = g_font_gui;
        g_fonts_preview_h1[i] = g_font_gui;
        g_fonts_preview_h2[i] = g_font_gui;
        g_fonts_preview_h3[i] = g_font_gui;
        g_fonts_preview_h4[i] = g_font_gui;
      }
    }

  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 8.0f;
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

} // namespace dover::overlay
