#include "overlay/dx9_hook.h"
#include "overlay/overlay_ui.h"
#include "overlay/theme.h"
#include "overlay/hook_utils.h"
#include "overlay/input_hook.h"
#include "shared/log.h"

#include <d3d9.h>
#include <windows.h>

#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

#include <atomic>

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
IDirect3DDevice9* g_d3d9_device = nullptr;

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
      g_d3d9_device = device;
      
      IMGUI_CHECKVERSION();
      ImGui::CreateContext();
      OverrideImGuiClipboardFunctions();
      InitializeOverlay();

      ImGui_ImplWin32_Init(g_game_hwnd);
      ImGui_ImplDX9_Init(device);

      // Subclass WndProc using shared HookedWndProc
      GetOverlayState().original_wnd_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
          g_game_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookedWndProc)));

      GetOverlayState().active_dx_version = "DirectX 9";
      g_imgui_initialized = true;
      dover::shared::LogInfo("Dear ImGui initialized inside D3D9 EndScene hook.");
    }
  }

  if (g_imgui_initialized.load()) {
    GetOverlayState().in_overlay_frame = true;
    ImGui_ImplDX9_NewFrame();
    
    g_allow_xinput = true;
    ImGui_ImplWin32_NewFrame();
    g_allow_xinput = false;
    
    ImGui::NewFrame();

    // Render shared UI
    RenderImGuiUI();

    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    GetOverlayState().in_overlay_frame = false;
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
  if (!InitializeHookSystem()) {
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
    if (CreateAndEnableHook(vtable[kEndSceneIndex], reinterpret_cast<void*>(&HookedEndScene),
                            reinterpret_cast<void**>(&g_original_end_scene))) {
      g_end_scene_hooked = true;
    }
  }

  if (!g_reset_hooked.load()) {
    if (CreateAndEnableHook(vtable[kResetIndex], reinterpret_cast<void*>(&HookedReset),
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
    if (g_game_hwnd && GetOverlayState().original_wnd_proc) {
      SetWindowLongPtrW(g_game_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(GetOverlayState().original_wnd_proc));
    }
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_imgui_initialized = false;
  }

  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_end_scene));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_reset));

  g_end_scene_hooked = false;
  g_reset_hooked = false;
  g_d3d9_device = nullptr;
}

IDirect3DDevice9* GetDx9Device() {
  return g_d3d9_device;
}

} // namespace dover::overlay
