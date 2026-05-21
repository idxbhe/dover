#pragma once
#include <windows.h>

struct ImFont;

namespace dover::overlay {

extern bool g_show_overlay;
extern bool g_in_overlay_frame;
extern WNDPROC g_original_wnd_proc;
extern const char* g_active_dx_version;

extern ImFont* g_font_gui;
extern ImFont* g_font_editor;
extern ImFont* g_font_preview;
extern ImFont* g_font_preview_bold;
extern ImFont* g_font_preview_italic;
extern ImFont* g_font_preview_bold_italic;
extern ImFont* g_font_preview_h1;
extern ImFont* g_font_preview_h2;
extern ImFont* g_font_preview_h3;

LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
void SetupImGuiTheme();
void RenderImGuiUI();

bool InitializeInputHooks();
void ShutdownInputHooks();

} // namespace dover::overlay
