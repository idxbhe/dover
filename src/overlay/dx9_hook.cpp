#include "overlay/dx9_hook.h"

#include "shared/log.h"

#include <MinHook.h>
#include <d3d9.h>
#include <windows.h>

#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

#include <atomic>
#include <array>
#include <mutex>
#include <string>

// Forward declare the ImGui Win32 handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace dover::overlay {

namespace {
constexpr int kResetIndex = 16;
constexpr int kEndSceneIndex = 42;

using EndSceneFn = HRESULT(WINAPI*)(IDirect3DDevice9*);
using ResetFn = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

EndSceneFn g_original_end_scene = nullptr;
ResetFn g_original_reset = nullptr;

std::atomic<bool> g_end_scene_hooked{false};
std::atomic<bool> g_reset_hooked{false};
std::atomic<bool> g_imgui_initialized{false};

HWND g_game_hwnd = nullptr;
WNDPROC g_original_wnd_proc = nullptr;
bool g_show_overlay = false;

std::once_flag g_mh_once;
MH_STATUS g_mh_status = MH_OK;
bool g_mh_ready = false;

bool EnsureMinHookInitialized() {
  std::call_once(g_mh_once, []() {
    g_mh_status = MH_Initialize();
    g_mh_ready = g_mh_status == MH_OK || g_mh_status == MH_ERROR_ALREADY_INITIALIZED;
  });
  return g_mh_ready;
}

bool EnableHook(void* target, void* detour, void** original) {
  if (!target || !detour) {
    return false;
  }

  MH_STATUS status = MH_CreateHook(target, detour, original);
  if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
    return false;
  }

  status = MH_EnableHook(target);
  return status == MH_OK || status == MH_ERROR_ENABLED;
}

void DisableHook(void* target) {
  if (target) {
    (void)MH_DisableHook(target);
    (void)MH_RemoveHook(target);
  }
}

// Subclassed WndProc to capture mouse & keyboard inputs for ImGui
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

HRESULT WINAPI HookedEndScene(IDirect3DDevice9* device) {
  if (!device) {
    if (g_original_end_scene) {
      return g_original_end_scene(device);
    }
    return D3D_OK;
  }

  if (!g_imgui_initialized.load()) {
    D3DDEVICE_CREATION_PARAMETERS params = {};
    if (SUCCEEDED(device->GetCreationParameters(&params)) && params.hFocusWindow) {
      g_game_hwnd = params.hFocusWindow;
      
      IMGUI_CHECKVERSION();
      ImGui::CreateContext();
      ImGui::StyleColorsDark();
      
      // Customize theme to feel premium (sleek dark glass aesthetic)
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

      ImGui_ImplWin32_Init(g_game_hwnd);
      ImGui_ImplDX9_Init(device);

      // Intercept mouse/keyboard inputs via WndProc
      g_original_wnd_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
          g_game_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookedWndProc)));

      g_imgui_initialized = true;
      dover::shared::LogInfo("Dear ImGui initialized inside D3D9 EndScene hook.");
    }
  }

  if (g_imgui_initialized.load()) {
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

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

    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
  }

  if (g_original_end_scene) {
    return g_original_end_scene(device);
  }
  return D3D_OK;
}

HRESULT WINAPI HookedReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) {
  if (g_imgui_initialized.load()) {
    ImGui_ImplDX9_InvalidateDeviceObjects();
  }

  HRESULT hr = D3D_OK;
  if (g_original_reset) {
    hr = g_original_reset(device, params);
  }

  if (SUCCEEDED(hr) && g_imgui_initialized.load()) {
    ImGui_ImplDX9_CreateDeviceObjects();
  }

  return hr;
}

} // namespace

bool InitializeDx9Hook() {
  if (!EnsureMinHookInitialized()) {
    dover::shared::LogError("MinHook initialization failed.");
    return false;
  }

  HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll");
  if (!d3d9) {
    return false;
  }

  using Direct3DCreate9Fn = IDirect3D9* (WINAPI*)(UINT);
  auto create9 = reinterpret_cast<Direct3DCreate9Fn>(GetProcAddress(d3d9, "Direct3DCreate9"));
  if (!create9) {
    dover::shared::LogError("Failed to locate Direct3DCreate9.");
    return false;
  }

  IDirect3D9* d3d = create9(D3D_SDK_VERSION);
  if (!d3d) {
    dover::shared::LogError("Failed to create IDirect3D9 object.");
    return false;
  }

  HWND dummy_window = CreateWindowExA(0, "STATIC", "dummy", 0, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
  if (!dummy_window) {
    dover::shared::LogError("Failed to create dummy window.");
    d3d->Release();
    return false;
  }

  D3DPRESENT_PARAMETERS params{};
  params.Windowed = TRUE;
  params.SwapEffect = D3DSWAPEFFECT_DISCARD;
  params.hDeviceWindow = dummy_window;

  IDirect3DDevice9* dummy_device = nullptr;
  HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, dummy_window,
                                 D3DCREATE_SOFTWARE_VERTEXPROCESSING, &params, &dummy_device);
  
  if (FAILED(hr) || !dummy_device) {
    dover::shared::LogError("Failed to create dummy device.");
    DestroyWindow(dummy_window);
    d3d->Release();
    return false;
  }

  void** vtable = *reinterpret_cast<void***>(dummy_device);
  
  if (!g_end_scene_hooked.load()) {
    if (EnableHook(vtable[kEndSceneIndex], reinterpret_cast<void*>(&HookedEndScene),
                   reinterpret_cast<void**>(&g_original_end_scene))) {
      g_end_scene_hooked = true;
    }
  }

  if (!g_reset_hooked.load()) {
    if (EnableHook(vtable[kResetIndex], reinterpret_cast<void*>(&HookedReset),
                   reinterpret_cast<void**>(&g_original_reset))) {
      g_reset_hooked = true;
    }
  }

  dummy_device->Release();
  DestroyWindow(dummy_window);
  d3d->Release();

  if (g_end_scene_hooked.load()) {
    dover::shared::LogInfo("DX9 Hook installed via dummy device.");
    return true;
  }

  dover::shared::LogError("Failed to hook DX9 functions.");
  return false;
}

void ShutdownDx9Hook() {
  if (g_imgui_initialized.load()) {
    if (g_game_hwnd && g_original_wnd_proc) {
      SetWindowLongPtrW(g_game_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_original_wnd_proc));
    }
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_imgui_initialized = false;
  }

  DisableHook(reinterpret_cast<void*>(g_original_end_scene));
  DisableHook(reinterpret_cast<void*>(g_original_reset));

  g_original_end_scene = nullptr;
  g_original_reset = nullptr;
  g_end_scene_hooked = false;
  g_reset_hooked = false;

  if (g_mh_ready) {
    (void)MH_DisableHook(MH_ALL_HOOKS);
    (void)MH_Uninitialize();
    g_mh_ready = false;
  }
}

} // namespace dover::overlay
