#pragma once

struct ImFont;

namespace dover::shared {

extern ImFont* g_font_gui;
extern ImFont* g_font_editor;
extern ImFont* g_font_preview;
extern ImFont* g_font_preview_bold;
extern ImFont* g_font_preview_italic;
extern ImFont* g_font_preview_bold_italic;

// Standard font sizes for the dynamic font system (v1.92+)
inline constexpr float kPreviewSizes[5] = { 13.0f, 15.0f, 18.0f, 22.0f, 26.0f };
inline constexpr float kEditorSizes[5]  = { 12.0f, 14.0f, 17.0f, 21.0f, 25.0f };

inline constexpr float kIconSize = 20.0f;
inline constexpr float kGuiSize  = 18.0f;
inline constexpr float kTitleSize = 22.0f; // Standardized title size (aligns with kPreviewSizes[3])

void SetupImGuiTheme();

} // namespace dover::shared
