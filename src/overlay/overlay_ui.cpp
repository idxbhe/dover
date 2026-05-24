#include "overlay/overlay_ui.h"
#include "overlay/hook_utils.h"
#include "overlay/notes/layout.h"
#include "overlay/notes/manager.h"
#include "overlay/icons.h"
#include "overlay/fonts.h"

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

namespace {
using GetAsyncKeyStateFn = SHORT(WINAPI*)(int);
using GetKeyStateFn = SHORT(WINAPI*)(int);
using GetKeyboardStateFn = BOOL(WINAPI*)(PBYTE);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
using GetCursorPosFn = BOOL(WINAPI*)(LPPOINT);
using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using PeekMessageWFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using PeekMessageAFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using GetMessageWFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);
using GetMessageAFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);

GetAsyncKeyStateFn g_original_get_async_key_state = nullptr;
GetKeyStateFn g_original_get_key_state = nullptr;
GetKeyboardStateFn g_original_get_keyboard_state = nullptr;
ClipCursorFn g_original_clip_cursor = nullptr;
GetCursorPosFn g_original_get_cursor_pos = nullptr;
SetCursorPosFn g_original_set_cursor_pos = nullptr;
PeekMessageWFn g_original_peek_message_w = nullptr;
PeekMessageAFn g_original_peek_message_a = nullptr;
GetMessageWFn g_original_get_message_w = nullptr;
GetMessageAFn g_original_get_message_a = nullptr;

SHORT WINAPI HookedGetAsyncKeyState(int vKey) {
  if (g_show_overlay) {
    if (vKey == VK_TAB || vKey == VK_SHIFT) {
      if (g_original_get_async_key_state) {
        return g_original_get_async_key_state(vKey);
      }
    }
    return 0;
  }
  if (g_original_get_async_key_state) {
    return g_original_get_async_key_state(vKey);
  }
  return 0;
}

SHORT WINAPI HookedGetKeyState(int nVirtKey) {
  if (g_show_overlay) {
    if (nVirtKey == VK_TAB || nVirtKey == VK_SHIFT) {
      if (g_original_get_key_state) {
        return g_original_get_key_state(nVirtKey);
      }
    }
    return 0;
  }
  if (g_original_get_key_state) {
    return g_original_get_key_state(nVirtKey);
  }
  return 0;
}

BOOL WINAPI HookedGetKeyboardState(PBYTE lpKeyState) {
  if (g_show_overlay && lpKeyState) {
    std::memset(lpKeyState, 0, 256);
    return TRUE;
  }
  if (g_original_get_keyboard_state) {
    return g_original_get_keyboard_state(lpKeyState);
  }
  return FALSE;
}

BOOL WINAPI HookedClipCursor(const RECT* lpRect) {
  if (g_show_overlay) {
    return TRUE;
  }
  if (g_original_clip_cursor) {
    return g_original_clip_cursor(lpRect);
  }
  return ClipCursor(lpRect);
}

BOOL WINAPI HookedGetCursorPos(LPPOINT lpPoint) {
  if (g_show_overlay && !g_in_overlay_frame) {
    HWND active_wnd = GetActiveWindow();
    if (active_wnd) {
      RECT rect = {};
      if (GetClientRect(active_wnd, &rect)) {
        POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
        ClientToScreen(active_wnd, &center);
        if (lpPoint) {
          *lpPoint = center;
          return TRUE;
        }
      }
    }
  }
  if (g_original_get_cursor_pos) {
    return g_original_get_cursor_pos(lpPoint);
  }
  return GetCursorPos(lpPoint);
}

BOOL WINAPI HookedSetCursorPos(int x, int y) {
  if (g_show_overlay && !g_in_overlay_frame) {
    return TRUE;
  }
  if (g_original_set_cursor_pos) {
    return g_original_set_cursor_pos(x, y);
  }
  return SetCursorPos(x, y);
}

BOOL WINAPI HookedPeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
  BOOL result = FALSE;
  if (g_original_peek_message_w) {
    result = g_original_peek_message_w(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
  } else {
    result = PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
  }

  if (result && g_show_overlay && lpMsg) {
    if ((lpMsg->message >= WM_MOUSEFIRST && lpMsg->message <= WM_MOUSELAST) ||
        (lpMsg->message >= WM_KEYFIRST && lpMsg->message <= WM_KEYLAST) ||
        lpMsg->message == WM_INPUT) {
      bool is_toggle = (lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000));
      if (!is_toggle) {
        TranslateMessage(lpMsg);
        DispatchMessageW(lpMsg);
        lpMsg->message = WM_NULL;
      }
    }
  }
  return result;
}

BOOL WINAPI HookedPeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
  BOOL result = FALSE;
  if (g_original_peek_message_a) {
    result = g_original_peek_message_a(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
  } else {
    result = PeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
  }

  if (result && g_show_overlay && lpMsg) {
    if ((lpMsg->message >= WM_MOUSEFIRST && lpMsg->message <= WM_MOUSELAST) ||
        (lpMsg->message >= WM_KEYFIRST && lpMsg->message <= WM_KEYLAST) ||
        lpMsg->message == WM_INPUT) {
      bool is_toggle = (lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000));
      if (!is_toggle) {
        TranslateMessage(lpMsg);
        DispatchMessageA(lpMsg);
        lpMsg->message = WM_NULL;
      }
    }
  }
  return result;
}

BOOL WINAPI HookedGetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
  BOOL result = FALSE;
  if (g_original_get_message_w) {
    result = g_original_get_message_w(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
  } else {
    result = GetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
  }

  if (result && g_show_overlay && lpMsg) {
    if ((lpMsg->message >= WM_MOUSEFIRST && lpMsg->message <= WM_MOUSELAST) ||
        (lpMsg->message >= WM_KEYFIRST && lpMsg->message <= WM_KEYLAST) ||
        lpMsg->message == WM_INPUT) {
      bool is_toggle = (lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000));
      if (!is_toggle) {
        TranslateMessage(lpMsg);
        DispatchMessageW(lpMsg);
        lpMsg->message = WM_NULL;
      }
    }
  }
  return result;
}

BOOL WINAPI HookedGetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
  BOOL result = FALSE;
  if (g_original_get_message_a) {
    result = g_original_get_message_a(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
  } else {
    result = GetMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
  }

  if (result && g_show_overlay && lpMsg) {
    if ((lpMsg->message >= WM_MOUSEFIRST && lpMsg->message <= WM_MOUSELAST) ||
        (lpMsg->message >= WM_KEYFIRST && lpMsg->message <= WM_KEYLAST) ||
        lpMsg->message == WM_INPUT) {
      bool is_toggle = (lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000));
      if (!is_toggle) {
        TranslateMessage(lpMsg);
        DispatchMessageA(lpMsg);
        lpMsg->message = WM_NULL;
      }
    }
  }
  return result;
}
} // namespace

LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  // Toggle overlay on Shift + Tab
  if (msg == WM_KEYDOWN && wparam == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000)) {
    g_show_overlay = !g_show_overlay;
    
    // Update ImGui cursor visibility state
    ImGuiIO& io = ImGui::GetIO();
    if (g_show_overlay) {
      io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    } else {
      io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }
    
    return 1; // Block input to game
  }

  if (g_show_overlay) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
      return true; // ImGui processed, block from game
    }

    // Block keyboard, mouse and raw input events from leaking into the game when overlay is active
    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) {
      return 1;
    }
    if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) {
      return 1;
    }
    if (msg == WM_INPUT) {
      return 1;
    }
  }

  return CallWindowProcW(g_original_wnd_proc, hwnd, msg, wparam, lparam);
}

static std::string g_ini_path_utf8;

void SetupImGuiTheme() {
  // Setup robust global INI file path in user LOCALAPPDATA directory
  wchar_t local_app_data[MAX_PATH];
  if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH)) {
    std::wstring dover_dir = std::wstring(local_app_data) + L"\\dover";
    CreateDirectoryW(dover_dir.c_str(), NULL);
    std::wstring ini_path = dover_dir + L"\\imgui.ini";
    
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, ini_path.c_str(), -1, NULL, 0, NULL, NULL);
    g_ini_path_utf8.resize(size_needed);
    WideCharToMultiByte(CP_UTF8, 0, ini_path.c_str(), -1, &g_ini_path_utf8[0], size_needed, NULL, NULL);
    
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = g_ini_path_utf8.c_str();

    // ---- Load Custom Fonts (3 Roles) ----
    HMODULE hMod = NULL;
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
            static const ImWchar icon_ranges[] = { 0xf000, 0xffff, 0 };
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
            static const ImWchar icon_ranges[] = { 0xf000, 0xffff, 0 };
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
          float h3_sizes[5]      = { 15.0f, 18.0f, 21.0f, 26.0f, 30.0f };
          float h2_sizes[5]      = { 18.0f, 21.0f, 25.0f, 31.0f, 36.0f };
          float h1_sizes[5]      = { 23.0f, 26.0f, 32.0f, 39.0f, 46.0f };

          for (int i = 0; i < 5; ++i) {
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
      }
    }

    // Determine game name from the current process executable
    char exe_name[MAX_PATH] = {};
    GetModuleBaseNameA(GetCurrentProcess(), nullptr, exe_name, MAX_PATH);
    notes::InitializeNotesManager(std::wstring(local_app_data), std::string(exe_name));
    notes::InitializeNotesUI();
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

void RenderImGuiUI() {
  // 1. Draw Pinned Info Window (transparent corner overlay)
  ImGui::SetNextWindowPos(ImVec2(12.0f, 10.0f), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.0f);
  ImGui::Begin("Info Window", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
               ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground);

  TickFPS();

  SYSTEMTIME time{};
  GetLocalTime(&time);
  ImGui::TextColored(ImVec4(0.20f, 1.00f, 0.70f, 1.00f), "%02u:%02u:%02u", time.wHour, time.wMinute, time.wSecond);
  ImGui::TextColored(ImVec4(1.00f, 1.00f, 1.00f, 1.00f), "FPS:  %.1f", g_fps_value);
  // Pinned API info hidden as requested:
  // ImGui::TextColored(ImVec4(1.00f, 0.80f, 0.20f, 1.00f), "API:  %s", g_active_dx_version);
  ImGui::End();

  // 2. Draw Steam-style Interactive Navigation and floating windows if visible
  if (g_show_overlay) {
    // Dim the underlying game frame for premium presentation
    ImVec2 display_size = ImGui::GetIO().DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), display_size, IM_COL32(0, 0, 0, 160));

    static bool show_notes = true;
    static bool show_settings = false;

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

    // A. Notes Button Rendering (Boxless, Dynamic 3D Raised White Gradient)
    ImGui::SetCursorPos(ImVec2(center_start_x, button_y));
    {
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImVec2 p_max = ImVec2(pos.x + icon_btn_width, pos.y + button_height);
      bool hovered = ImGui::IsMouseHoveringRect(pos, p_max);
      
      ImVec2 glyph_size = ImGui::CalcTextSize(ICON_PANEL_NOTES);
      ImVec2 text_pos = ImVec2(pos.x + (icon_btn_width - glyph_size.x) * 0.5f, pos.y + (button_height - glyph_size.y) * 0.5f);
      
      ImVec4 main_color, shadow_color, highlight_color;
      if (show_notes) {
        // Selected: Brilliant pure white with cyan-blue shadow for 3D raised depth
        main_color      = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        highlight_color = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);
        shadow_color    = ImVec4(0.00f, 0.45f, 0.85f, 0.85f);
      } else if (hovered) {
        // Hovered: Bright white with subtle slate shadow
        main_color      = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        highlight_color = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
        shadow_color    = ImVec4(0.12f, 0.15f, 0.20f, 0.80f);
      } else {
        // Idle: Soft slate-white with dark charcoal shadow
        main_color      = ImVec4(0.70f, 0.73f, 0.80f, 0.85f);
        highlight_color = ImVec4(1.00f, 1.00f, 1.00f, 0.20f);
        shadow_color    = ImVec4(0.07f, 0.08f, 0.11f, 0.70f);
      }
      
      // Draw 3D layers directly to the glyph (shadow, highlight, main)
      ImGui::GetWindowDrawList()->AddText(g_font_panel, g_font_panel->FontSize, ImVec2(text_pos.x + 1.0f, text_pos.y + 1.5f), ImGui::ColorConvertFloat4ToU32(shadow_color), ICON_PANEL_NOTES);
      ImGui::GetWindowDrawList()->AddText(g_font_panel, g_font_panel->FontSize, ImVec2(text_pos.x - 0.5f, text_pos.y - 0.5f), ImGui::ColorConvertFloat4ToU32(highlight_color), ICON_PANEL_NOTES);
      ImGui::GetWindowDrawList()->AddText(g_font_panel, g_font_panel->FontSize, text_pos, ImGui::ColorConvertFloat4ToU32(main_color), ICON_PANEL_NOTES);
    }
    if (ImGui::Button(ICON_PANEL_NOTES, ImVec2(icon_btn_width, button_height))) {
      show_notes = !show_notes;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Notes");

    // B. Settings Button Rendering (Boxless, Dynamic 3D Raised White Gradient)
    ImGui::SameLine(0.0f, button_spacing);
    {
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImVec2 p_max = ImVec2(pos.x + icon_btn_width, pos.y + button_height);
      bool hovered = ImGui::IsMouseHoveringRect(pos, p_max);
      
      ImVec2 glyph_size = ImGui::CalcTextSize(ICON_PANEL_SETTINGS);
      ImVec2 text_pos = ImVec2(pos.x + (icon_btn_width - glyph_size.x) * 0.5f, pos.y + (button_height - glyph_size.y) * 0.5f);
      
      ImVec4 main_color, shadow_color, highlight_color;
      if (show_settings) {
        // Selected: Brilliant pure white with purple shadow for 3D raised depth
        main_color      = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        highlight_color = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);
        shadow_color    = ImVec4(0.55f, 0.30f, 0.90f, 0.85f);
      } else if (hovered) {
        // Hovered: Bright white with subtle slate shadow
        main_color      = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        highlight_color = ImVec4(1.00f, 1.00f, 1.00f, 0.30f);
        shadow_color    = ImVec4(0.12f, 0.15f, 0.20f, 0.80f);
      } else {
        // Idle: Soft slate-white with dark charcoal shadow
        main_color      = ImVec4(0.70f, 0.73f, 0.80f, 0.85f);
        highlight_color = ImVec4(1.00f, 1.00f, 1.00f, 0.20f);
        shadow_color    = ImVec4(0.07f, 0.08f, 0.11f, 0.70f);
      }
      
      // Draw 3D layers directly to the glyph (shadow, highlight, main)
      ImGui::GetWindowDrawList()->AddText(g_font_panel, g_font_panel->FontSize, ImVec2(text_pos.x + 1.0f, text_pos.y + 1.5f), ImGui::ColorConvertFloat4ToU32(shadow_color), ICON_PANEL_SETTINGS);
      ImGui::GetWindowDrawList()->AddText(g_font_panel, g_font_panel->FontSize, ImVec2(text_pos.x - 0.5f, text_pos.y - 0.5f), ImGui::ColorConvertFloat4ToU32(highlight_color), ICON_PANEL_SETTINGS);
      ImGui::GetWindowDrawList()->AddText(g_font_panel, g_font_panel->FontSize, text_pos, ImGui::ColorConvertFloat4ToU32(main_color), ICON_PANEL_SETTINGS);
    }
    if (ImGui::Button(ICON_PANEL_SETTINGS, ImVec2(icon_btn_width, button_height))) {
      show_settings = !show_settings;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Settings");

    ImGui::PopStyleColor(4);

    // C. Close Button Rendering (Flat, Dark Slate with subtle Red hovered accent)
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
    }
    if (ImGui::Button(ICON_WINDOW_CLOSE, ImVec2(button_width, button_height))) {
      g_show_overlay = false;
      ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close");

    ImGui::PopStyleColor(3);

    ImGui::PopFont();
    ImGui::End();

    // B. Floating Feature Jendela (Modular, bebas geser, mengingat posisi via dover/imgui.ini)
    
    // Notes Jendela (modular — handled by notes_ui module)
    if (show_notes) {
      notes::RenderNotesWindow(&show_notes);
    }

    // Settings Jendela
    if (show_settings) {
      ImGui::SetNextWindowSize(ImVec2(320.0f, 200.0f), ImGuiCond_FirstUseEver);
      ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
      bool settings_ok = ImGui::Begin("Settings", &show_settings, ImGuiWindowFlags_NoCollapse);
      ImGui::PopStyleColor();
      if (settings_ok) {
        ImVec2 min_p = ImGui::GetWindowPos();
        ImVec2 max_p = ImVec2(min_p.x + ImGui::GetWindowSize().x, min_p.y + ImGui::GetWindowSize().y);
        float alpha = ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w;
        ImU32 col_tl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.110f, 0.125f, 0.161f, alpha)); // #1c2029 (Cool slate settings)
        ImU32 col_tr = ImGui::ColorConvertFloat4ToU32(ImVec4(0.090f, 0.102f, 0.130f, alpha)); // #171a21
        ImU32 col_br = ImGui::ColorConvertFloat4ToU32(ImVec4(0.071f, 0.082f, 0.106f, alpha)); // #12151b
        ImU32 col_bl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.094f, 0.106f, 0.137f, alpha)); // #181b23
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(min_p, max_p, col_tl, col_tr, col_br, col_bl);

        ImGui::Text("Configurations:");
        ImGui::Separator();
        static bool vsync = true;
        ImGui::Checkbox("Enable VSync Simulation", &vsync);
        ImGui::Spacing();
        ImGui::TextDisabled("Future game-specific saving:");
        ImGui::TextDisabled("dover/imgui_<game>.ini");
      }
      ImGui::End();
    }
  }
}

bool InitializeInputHooks() {
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (!user32) {
    return false;
  }

  void* get_async_key_state_addr = GetProcAddress(user32, "GetAsyncKeyState");
  void* get_key_state_addr = GetProcAddress(user32, "GetKeyState");
  void* get_keyboard_state_addr = GetProcAddress(user32, "GetKeyboardState");
  void* clip_cursor_addr = GetProcAddress(user32, "ClipCursor");
  void* get_cursor_pos_addr = GetProcAddress(user32, "GetCursorPos");
  void* set_cursor_pos_addr = GetProcAddress(user32, "SetCursorPos");
  void* peek_message_w_addr = GetProcAddress(user32, "PeekMessageW");
  void* peek_message_a_addr = GetProcAddress(user32, "PeekMessageA");
  void* get_message_w_addr = GetProcAddress(user32, "GetMessageW");
  void* get_message_a_addr = GetProcAddress(user32, "GetMessageA");

  bool success = true;

  if (get_async_key_state_addr) {
    success &= CreateAndEnableHook(
        get_async_key_state_addr,
        reinterpret_cast<void*>(&HookedGetAsyncKeyState),
        reinterpret_cast<void**>(&g_original_get_async_key_state));
  }
  if (get_key_state_addr) {
    success &= CreateAndEnableHook(
        get_key_state_addr,
        reinterpret_cast<void*>(&HookedGetKeyState),
        reinterpret_cast<void**>(&g_original_get_key_state));
  }
  if (get_keyboard_state_addr) {
    success &= CreateAndEnableHook(
        get_keyboard_state_addr,
        reinterpret_cast<void*>(&HookedGetKeyboardState),
        reinterpret_cast<void**>(&g_original_get_keyboard_state));
  }
  if (clip_cursor_addr) {
    success &= CreateAndEnableHook(
        clip_cursor_addr,
        reinterpret_cast<void*>(&HookedClipCursor),
        reinterpret_cast<void**>(&g_original_clip_cursor));
  }
  if (get_cursor_pos_addr) {
    success &= CreateAndEnableHook(
        get_cursor_pos_addr,
        reinterpret_cast<void*>(&HookedGetCursorPos),
        reinterpret_cast<void**>(&g_original_get_cursor_pos));
  }
  if (set_cursor_pos_addr) {
    success &= CreateAndEnableHook(
        set_cursor_pos_addr,
        reinterpret_cast<void*>(&HookedSetCursorPos),
        reinterpret_cast<void**>(&g_original_set_cursor_pos));
  }
  if (peek_message_w_addr) {
    success &= CreateAndEnableHook(
        peek_message_w_addr,
        reinterpret_cast<void*>(&HookedPeekMessageW),
        reinterpret_cast<void**>(&g_original_peek_message_w));
  }
  if (peek_message_a_addr) {
    success &= CreateAndEnableHook(
        peek_message_a_addr,
        reinterpret_cast<void*>(&HookedPeekMessageA),
        reinterpret_cast<void**>(&g_original_peek_message_a));
  }
  if (get_message_w_addr) {
    success &= CreateAndEnableHook(
        get_message_w_addr,
        reinterpret_cast<void*>(&HookedGetMessageW),
        reinterpret_cast<void**>(&g_original_get_message_w));
  }
  if (get_message_a_addr) {
    success &= CreateAndEnableHook(
        get_message_a_addr,
        reinterpret_cast<void*>(&HookedGetMessageA),
        reinterpret_cast<void**>(&g_original_get_message_a));
  }

  return success;
}

void ShutdownInputHooks() {
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_async_key_state));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_key_state));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_keyboard_state));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_clip_cursor));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_cursor_pos));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_set_cursor_pos));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_peek_message_w));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_peek_message_a));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_message_w));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_get_message_a));

  g_original_get_async_key_state = nullptr;
  g_original_get_key_state = nullptr;
  g_original_get_keyboard_state = nullptr;
  g_original_clip_cursor = nullptr;
  g_original_get_cursor_pos = nullptr;
  g_original_set_cursor_pos = nullptr;
  g_original_peek_message_w = nullptr;
  g_original_peek_message_a = nullptr;
  g_original_get_message_w = nullptr;
  g_original_get_message_a = nullptr;
}

} // namespace dover::overlay
