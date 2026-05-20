#include "overlay/overlay_ui.h"

#include <windows.h>
#include <imgui.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace dover::overlay {

bool g_show_overlay = false;
WNDPROC g_original_wnd_proc = nullptr;
const char* g_active_dx_version = "Unknown API";

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

    // Block keyboard and mouse events from leaking into the game when overlay is active
    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) {
      return 1;
    }
    if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) {
      return 1;
    }
  }

  return CallWindowProcW(g_original_wnd_proc, hwnd, msg, wparam, lparam);
}

void SetupImGuiTheme() {
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 8.0f;
  style.FrameRounding = 4.0f;
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.85f);
  style.Colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.12f, 0.16f, 0.90f);
  style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.22f, 0.95f);
  style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.35f, 0.70f);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.35f, 0.50f, 0.85f);
  style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.35f, 0.45f, 0.65f, 1.00f);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.10f, 0.14f, 0.60f);
}

void RenderImGuiUI() {
  // 1. Draw Pinned Info Window (transparent corner overlay)
  ImGui::SetNextWindowPos(ImVec2(12.0f, 10.0f), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.65f);
  ImGui::Begin("Info Window", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
               ImGuiWindowFlags_NoNav);

  SYSTEMTIME time{};
  GetLocalTime(&time);
  ImGui::TextColored(ImVec4(0.20f, 1.00f, 0.70f, 1.00f), "Time: %02u:%02u:%02u", time.wHour, time.wMinute, time.wSecond);
  ImGui::TextColored(ImVec4(1.00f, 1.00f, 1.00f, 1.00f), "FPS:  %.1f", ImGui::GetIO().Framerate);
  ImGui::TextColored(ImVec4(1.00f, 0.80f, 0.20f, 1.00f), "API:  %s", g_active_dx_version);
  ImGui::End();

  // 2. Draw Interactive Overlay Panel if visible
  if (g_show_overlay) {
    // Dim the underlying game frame for premium presentation
    ImVec2 display_size = ImGui::GetIO().DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), display_size, IM_COL32(0, 0, 0, 160));

    ImGui::SetNextWindowSize(ImVec2(550.0f, 350.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Dover Overlay Panel (Press Shift+Tab to Close)", &g_show_overlay, ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Welcome to Dover Steam Overlay alternative!");
    ImGui::TextDisabled("Take notes or configure settings while gaming.");
    ImGui::Spacing();
    
    if (ImGui::BeginTabBar("DoverTabs")) {
      if (ImGui::BeginTabItem("Notes")) {
        static char notes[2048] = "Write down your notes, strategies, or game codes here...";
        ImGui::InputTextMultiline("##notes", notes, sizeof(notes), ImVec2(-FLT_MIN, -FLT_MIN));
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Settings")) {
        ImGui::Text("Configurations:");
        ImGui::Separator();
        static bool vsync = true;
        ImGui::Checkbox("Enable VSync Simulation", &vsync);
        ImGui::Spacing();
        if (ImGui::Button("Close Overlay")) {
          g_show_overlay = false;
          ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        }
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    ImGui::End();
  }
}

} // namespace dover::overlay
