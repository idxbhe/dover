#include "overlay/dx9_hook.h"
#include "overlay/overlay_ui.h"
#include "shared/input_utils.h"
#include "shared/theme.h"
#include "overlay/hook_utils.h"
#include "overlay/input_hook.h"
#include "shared/log.h"
#include "shared/renderer.h"
#include "shared/settings/app_config.h"
#include "shared/engine_quirks.h"
#include "overlay_runtime.h"

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

HRESULT WINAPI HookedEndScene(IDirect3DDevice9* device);
HRESULT WINAPI HookedReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params);

EndSceneFn g_original_end_scene = nullptr;
ResetFn g_original_reset = nullptr;

std::atomic<bool> g_end_scene_hooked{false};
std::atomic<bool> g_reset_hooked{false};
std::atomic<bool> g_imgui_initialized{false};

IDirect3DDevice9* g_d3d9_device = nullptr;

shared::InjectionMethod g_active_injection_method = shared::InjectionMethod::PureVTable;
IDirect3DDevice9* g_live_d3d9_device = nullptr;
void** g_dx9_device_vtable = nullptr;

HRESULT WINAPI HookedEndScene(IDirect3DDevice9* device) {
  if (!device || IsOverlayShutdownRequested()) {
    if (g_original_end_scene) {
      return g_original_end_scene(device);
    }
    return D3D_OK;
  }

  if (g_active_injection_method == shared::InjectionMethod::PureVTable) {
    if (!g_live_d3d9_device) {
      g_live_d3d9_device = device;
    }
    if (!g_reset_hooked.load()) {
      if (CreateVTableHook(device, kResetIndex,
                           reinterpret_cast<void*>(&HookedReset),
                           reinterpret_cast<void**>(&g_original_reset))) {
        g_reset_hooked = true;
        dover::shared::LogInfo("DX9: Lazy VTable hook for Reset installed.");
      }
    }
  }

  TickInputCooldown();

  if (!g_imgui_initialized.load()) {
    D3DDEVICE_CREATION_PARAMETERS params = {};
    if (SUCCEEDED(device->GetCreationParameters(&params)) && params.hFocusWindow) {
      HWND hwnd = params.hFocusWindow;
      g_d3d9_device = device;
      
      D3DVIEWPORT9 viewport;
      if (SUCCEEDED(device->GetViewport(&viewport))) {
          GetOverlayState().game_hwnd.store(hwnd, std::memory_order_release);
          GetOverlayState().swapchain_width.store(viewport.Width, std::memory_order_relaxed);
          GetOverlayState().swapchain_height.store(viewport.Height, std::memory_order_relaxed);
          UpdateMouseScaling();

          IMGUI_CHECKVERSION();
          ImGui::CreateContext();
          OverrideImGuiClipboardFunctions();
          InitializeOverlay();

          ImGui_ImplWin32_Init(hwnd);
          ImGui_ImplDX9_Init(device);

          // Subclass WndProc using shared HookedWndProc
          WNDPROC old_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
              hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookedWndProc)));
          GetOverlayState().original_wnd_proc.store(old_proc, std::memory_order_release);

          GetOverlayState().active_dx_version.store(GraphicsAPI::DX9, std::memory_order_release);

          // Register device with shared renderer so asset texture creation works
          shared::SetDx9Device(device);

          g_imgui_initialized = true;
          dover::shared::LogInfo("Dear ImGui initialized inside D3D9 EndScene hook.");
      }
    }
  }

  if (g_imgui_initialized.load()) {
    bool should_render = GetOverlayState().show_overlay.load(std::memory_order_relaxed) || 
                        shared::GetAppConfig().show_fps || 
                        shared::GetAppConfig().show_clock || 
                        shared::GetAppConfig().show_api;

    if (should_render) {
      GetOverlayState().in_overlay_frame.store(true, std::memory_order_relaxed);
      ImGui_ImplDX9_NewFrame();
      
      shared::g_allow_xinput = true;
      shared::g_allow_input_queries = true;
      g_in_imgui_new_frame = true;
      ImGui_ImplWin32_NewFrame();
      g_in_imgui_new_frame = false;

      ImGui::NewFrame();
      RenderImGuiUI();
      ImGui::Render();

      // Applying Engine Fingerprinting Quirks for DX9
      const auto& quirks = shared::GetEngineQuirks();
      if (quirks.dx9_force_backbuffer_render) {
          IDirect3DSurface9* original_rt = nullptr;
          D3DVIEWPORT9 original_viewport;
          
          device->GetRenderTarget(0, &original_rt);
          device->GetViewport(&original_viewport);

          IDirect3DSurface9* backbuffer = nullptr;
          if (SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer))) {
              device->SetRenderTarget(0, backbuffer);
              
              D3DVIEWPORT9 target_viewport = { 0, 0, 
                  GetOverlayState().swapchain_width.load(std::memory_order_relaxed),
                  GetOverlayState().swapchain_height.load(std::memory_order_relaxed),
                  0.0f, 1.0f };
              device->SetViewport(&target_viewport);

              ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

              backbuffer->Release();
          }

          if (original_rt) {
              device->SetRenderTarget(0, original_rt);
              original_rt->Release();
          }
          device->SetViewport(&original_viewport);
      } else {
          ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
      }
      
      shared::g_allow_input_queries = false;
      shared::g_allow_xinput = false;
      GetOverlayState().in_overlay_frame.store(false, std::memory_order_relaxed);
    }
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
  g_active_injection_method = shared::GetAppConfig().injection_method.load(std::memory_order_relaxed);

  if (g_active_injection_method == shared::InjectionMethod::InlineMinHook) {
    if (!InitializeHookSystem()) {
      dover::shared::LogError("MinHook initialization failed.");
      return false;
    }
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
  g_dx9_device_vtable = vtable;
  
  if (g_active_injection_method == shared::InjectionMethod::InlineMinHook) {
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
  } else {
    if (!g_end_scene_hooked.load()) {
      if (CreateVTableHook(dummy_device, kEndSceneIndex, reinterpret_cast<void*>(&HookedEndScene),
                           reinterpret_cast<void**>(&g_original_end_scene))) {
        g_end_scene_hooked = true;
        dover::shared::LogInfo("DX9: Pure VTable EndScene hook installed successfully.");
      }
    }
  }

  dummy_device->Release();
  DestroyWindow(dummy_window);
  d3d->Release();

  if (g_end_scene_hooked.load()) {
    dover::shared::LogInfo("DX9 Hook installed via dummy device (Method: %s).",
                           g_active_injection_method == shared::InjectionMethod::InlineMinHook ? "MinHook" : "VTable");
    return true;
  }

  dover::shared::LogError("Failed to hook DX9 functions.");
  return false;
}

void ShutdownDx9Hook() {
  if (g_imgui_initialized.load()) {
    HWND hwnd = GetOverlayState().game_hwnd.load(std::memory_order_acquire);
    WNDPROC orig = GetOverlayState().original_wnd_proc.load(std::memory_order_acquire);
    if (hwnd && orig) {
      SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
    }
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_imgui_initialized = false;
  }

  if (g_active_injection_method == shared::InjectionMethod::InlineMinHook) {
    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_end_scene));
    DisableAndRemoveHook(reinterpret_cast<void*>(g_original_reset));
  } else {
    if (g_dx9_device_vtable) {
      if (g_end_scene_hooked.load()) {
        RemoveVTableHookFromAddress(g_dx9_device_vtable, kEndSceneIndex, reinterpret_cast<void*>(g_original_end_scene));
      }
      if (g_reset_hooked.load()) {
        RemoveVTableHookFromAddress(g_dx9_device_vtable, kResetIndex, reinterpret_cast<void*>(g_original_reset));
      }
    }
  }

  g_end_scene_hooked = false;
  g_reset_hooked = false;
  g_live_d3d9_device = nullptr;
  g_dx9_device_vtable = nullptr;

  // Unregister from shared renderer
  shared::SetDx9Device(nullptr);
  g_d3d9_device = nullptr;
}

IDirect3DDevice9* GetDx9Device() {
  return g_d3d9_device;
}

} // namespace dover::overlay
