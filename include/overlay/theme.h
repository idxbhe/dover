#pragma once

struct ImFont;

namespace dover::overlay {

extern ImFont* g_font_gui;
extern ImFont* g_font_panel;
extern ImFont* g_fonts_editor[5];
extern ImFont* g_fonts_preview[5];
extern ImFont* g_fonts_preview_bold[5];
extern ImFont* g_fonts_preview_italic[5];
extern ImFont* g_fonts_preview_bold_italic[5];
extern ImFont* g_fonts_preview_h1[5];
extern ImFont* g_fonts_preview_h2[5];
extern ImFont* g_fonts_preview_h3[5];
extern ImFont* g_fonts_preview_h4[5];

void SetupImGuiTheme();

} // namespace dover::overlay
