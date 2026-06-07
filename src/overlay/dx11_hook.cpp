#include "overlay/dx11_hook.h"
#include "overlay/overlay_ui.h"
#include "shared/input_utils.h"
#include "shared/theme.h"
#include "overlay/hook_utils.h"
#include "overlay/input_hook.h"
#include "shared/log.h"
#include "shared/renderer.h"
#include "overlay_runtime.h"

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <atomic>

namespace dover::overlay {

namespace {
constexpr int kPresentIndex = 8;
constexpr int kResizeBuffersIndex = 13;

using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

PresentFn g_original_present = nullptr;
ResizeBuffersFn g_original_resize_buffers = nullptr;

std::atomic<bool> g_present_hooked{false};
std::atomic<bool> g_resize_hooked{false};
std::atomic<bool> g_imgui_initialized{false};

HWND g_game_hwnd = nullptr;
ID3D11Device* g_d3d11_device = nullptr;
ID3D11DeviceContext* g_d3d11_context = nullptr;
ID3D11RenderTargetView* g_render_target_view = nullptr;

void CleanupRenderTargetView() {
  if (g_render_target_view) {
    g_render_target_view->Release();
    g_render_target_view = nullptr;
  }
}

void CreateRenderTargetView(IDXGISwapChain* swapchain) {
  ID3D11Texture2D* backbuffer = nullptr;
  if (SUCCEEDED(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)))) {
    g_d3d11_device->CreateRenderTargetView(backbuffer, nullptr, &g_render_target_view);
    backbuffer->Release();
  }
}

HRESULT WINAPI HookedPresent(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags) {
  if (!swapchain || IsOverlayShutdownRequested()) {
    if (g_original_present) {
      return g_original_present(swapchain, sync_interval, flags);
    }
    return S_OK;
  }

  TickInputCooldown();

  if (!g_imgui_initialized.load()) {
    if (SUCCEEDED(swapchain->GetDevice(IID_PPV_ARGS(&g_d3d11_device)))) {
      g_d3d11_device->GetImmediateContext(&g_d3d11_context);

      DXGI_SWAP_CHAIN_DESC desc = {};
      swapchain->GetDesc(&desc);
      g_game_hwnd = desc.OutputWindow;
      GetOverlayState().swapchain_width = desc.BufferDesc.Width;
      GetOverlayState().swapchain_height = desc.BufferDesc.Height;

      IMGUI_CHECKVERSION();
      ImGui::CreateContext();
      OverrideImGuiClipboardFunctions();
      InitializeOverlay();

      ImGui_ImplWin32_Init(g_game_hwnd);
      ImGui_ImplDX11_Init(g_d3d11_device, g_d3d11_context);

      // Subclass WndProc using shared HookedWndProc
      GetOverlayState().original_wnd_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
          g_game_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookedWndProc)));

      GetOverlayState().active_dx_version = "DirectX 11";
      CreateRenderTargetView(swapchain);

      // Register device with shared renderer so asset texture creation works
      shared::SetDx11Device(g_d3d11_device);
      shared::SetDx11Context(g_d3d11_context);

      g_imgui_initialized = true;
      dover::shared::LogInfo("Dear ImGui initialized inside D3D11 Present hook.");
    }
  }

  if (g_imgui_initialized.load() && g_render_target_view) {
    // Early-out: only run ImGui lifecycle if overlay is visible or OSD features are enabled
    bool should_render = GetOverlayState().show_overlay || 
                        shared::GetAppConfig().show_fps || 
                        shared::GetAppConfig().show_clock || 
                        shared::GetAppConfig().show_api;

    if (should_render) {
      GetOverlayState().in_overlay_frame = true;
      ImGui_ImplDX11_NewFrame();
      
      shared::g_allow_xinput = true;
      shared::g_allow_input_queries = true;
      g_in_imgui_new_frame = true;
      ImGui_ImplWin32_NewFrame();
      g_in_imgui_new_frame = false;
      shared::g_allow_input_queries = false;
      shared::g_allow_xinput = false;
      
      // Fix for blurry UI & cursor mismatch when game does not resize swapchain
      ImGuiIO& io = ImGui::GetIO();
      uint32_t swap_w = GetOverlayState().swapchain_width;
      uint32_t swap_h = GetOverlayState().swapchain_height;
      if (swap_w > 0 && swap_h > 0) {
          io.DisplaySize = ImVec2(static_cast<float>(swap_w), static_cast<float>(swap_h));
      }

      ImGui::NewFrame();

      // Render shared UI
      RenderImGuiUI();

      ImGui::Render();

      // Bind RenderTargetView before rendering ImGui
      g_d3d11_context->OMSetRenderTargets(1, &g_render_target_view, nullptr);
      ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
      GetOverlayState().in_overlay_frame = false;
    }
  }

  if (g_original_present) {
    return g_original_present(swapchain, sync_interval, flags);
  }
  return S_OK;
}

HRESULT WINAPI HookedResizeBuffers(IDXGISwapChain* swapchain, UINT buffer_count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) {
  CleanupRenderTargetView();
  if (g_imgui_initialized.load()) {
    ImGui_ImplDX11_InvalidateDeviceObjects();
  }

  HRESULT hr = S_OK;
  if (g_original_resize_buffers) {
    hr = g_original_resize_buffers(swapchain, buffer_count, width, height, format, flags);
  }

  if (SUCCEEDED(hr) && g_imgui_initialized.load()) {
    DXGI_SWAP_CHAIN_DESC desc = {};
    if (SUCCEEDED(swapchain->GetDesc(&desc))) {
        GetOverlayState().swapchain_width = desc.BufferDesc.Width;
        GetOverlayState().swapchain_height = desc.BufferDesc.Height;
    }
    ImGui_ImplDX11_CreateDeviceObjects();
    CreateRenderTargetView(swapchain);
  }

  return hr;
}

} // namespace

bool InitializeDx11Hook() {
  if (!InitializeHookSystem()) {
    dover::shared::LogError("MinHook initialization failed.");
    return false;
  }

  HMODULE d3d11 = GetModuleHandleW(L"d3d11.dll");
  if (!d3d11) {
    return false;
  }

  using D3D11CreateDeviceAndSwapChainFn = HRESULT(WINAPI*)(
      IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
      const D3D_FEATURE_LEVEL*, UINT, UINT,
      const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**,
      D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
  
  auto create_device_and_swap_chain = reinterpret_cast<D3D11CreateDeviceAndSwapChainFn>(
      GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain"));
  
  if (!create_device_and_swap_chain) {
    dover::shared::LogError("Failed to locate D3D11CreateDeviceAndSwapChain.");
    return false;
  }

  HWND dummy_window = CreateWindowExA(0, "STATIC", "dummy", 0, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
  if (!dummy_window) {
    dover::shared::LogError("Failed to create dummy window.");
    return false;
  }

  DXGI_SWAP_CHAIN_DESC desc = {};
  desc.BufferDesc.Width = 1;
  desc.BufferDesc.Height = 1;
  desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.OutputWindow = dummy_window;
  desc.Windowed = TRUE;

  IDXGISwapChain* dummy_swapchain = nullptr;
  ID3D11Device* dummy_device = nullptr;
  ID3D11DeviceContext* dummy_context = nullptr;
  D3D_FEATURE_LEVEL feature_level{};

  HRESULT hr = create_device_and_swap_chain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
      nullptr, 0, D3D11_SDK_VERSION,
      &desc, &dummy_swapchain, &dummy_device, &feature_level, &dummy_context);

  if (FAILED(hr) || !dummy_swapchain) {
    dover::shared::LogError("Failed to create dummy D3D11 device and swapchain.");
    DestroyWindow(dummy_window);
    return false;
  }

  void** vtable = *reinterpret_cast<void***>(dummy_swapchain);

  if (!g_present_hooked.load()) {
    if (CreateAndEnableHook(vtable[kPresentIndex], reinterpret_cast<void*>(&HookedPresent),
                            reinterpret_cast<void**>(&g_original_present))) {
      g_present_hooked = true;
    }
  }

  if (!g_resize_hooked.load()) {
    if (CreateAndEnableHook(vtable[kResizeBuffersIndex], reinterpret_cast<void*>(&HookedResizeBuffers),
                            reinterpret_cast<void**>(&g_original_resize_buffers))) {
      g_resize_hooked = true;
    }
  }

  dummy_swapchain->Release();
  dummy_device->Release();
  dummy_context->Release();
  DestroyWindow(dummy_window);

  if (g_present_hooked.load()) {
    dover::shared::LogInfo("DX11 Hook installed via dummy swapchain.");
    return true;
  }

  dover::shared::LogError("Failed to hook DX11 functions.");
  return false;
}

void ShutdownDx11Hook() {
  if (g_imgui_initialized.load()) {
    if (g_game_hwnd && GetOverlayState().original_wnd_proc) {
      SetWindowLongPtrW(g_game_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(GetOverlayState().original_wnd_proc));
    }
    CleanupRenderTargetView();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_imgui_initialized = false;
  }

  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_present));
  DisableAndRemoveHook(reinterpret_cast<void*>(g_original_resize_buffers));

  g_original_present = nullptr;
  g_original_resize_buffers = nullptr;
  g_present_hooked = false;
  g_resize_hooked = false;

  if (g_d3d11_device) {
    g_d3d11_device->Release();
    g_d3d11_device = nullptr;
  }
  if (g_d3d11_context) {
    g_d3d11_context->Release();
    g_d3d11_context = nullptr;
  }
}

ID3D11Device* GetDx11Device() {
  return g_d3d11_device;
}

ID3D11DeviceContext* GetDx11Context() {
  return g_d3d11_context;
}

} // namespace dover::overlay
