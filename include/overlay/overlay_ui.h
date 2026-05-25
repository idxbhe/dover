#pragma once
#include <windows.h>

struct ImFont;

namespace dover::overlay {

extern bool g_show_overlay;
extern bool g_in_overlay_frame;
extern WNDPROC g_original_wnd_proc;
extern const char* g_active_dx_version;
extern float g_overlay_bg_alpha;
extern float g_global_window_alpha;


LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
void RenderImGuiUI();



} // namespace dover::overlay
