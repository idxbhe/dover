#pragma once

struct ImFont;

namespace dover::shared {

extern ImFont* g_font_gui;
extern ImFont* g_font_panel;
extern ImFont* g_font_editor;
extern ImFont* g_font_preview;
extern ImFont* g_font_preview_bold;
extern ImFont* g_font_preview_italic;
extern ImFont* g_font_preview_bold_italic;

void SetupImGuiTheme();

} // namespace dover::shared
