#include "overlay/dx11_hook.h"
#include "overlay/overlay_ui.h"
#include "shared/log.h"

#include <MinHook.h>
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <atomic>
#include <mutex>

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
  if (!swapchain) {
    if (g_original_present) {
      return g_original_present(swapchain, sync_interval, flags);
    }
    return S_OK;
  }

  if (!g_imgui_initialized.load()) {
    if (SUCCEEDED(swapchain->GetDevice(IID_PPV_ARGS(&g_d3d11_device)))) {
      g_d3d11_device->GetImmediateContext(&g_d3d11_context);

      DXGI_SWAP_CHAIN_DESC desc = {};
      swapchain->GetDesc(&desc);
      g_game_hwnd = desc.OutputWindow;

      IMGUI_CHECKVERSION();
      ImGui::CreateContext();
      SetupImGuiTheme();

      ImGui_ImplWin32_Init(g_game_hwnd);
      ImGui_ImplDX11_Init(g_d3d11_device, g_d3d11_context);

      // Subclass WndProc using shared HookedWndProc
      g_original_wnd_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
          g_game_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookedWndProc)));

      g_active_dx_version = "DirectX 11";
      CreateRenderTargetView(swapchain);

      g_imgui_initialized = true;
      dover::shared::LogInfo("Dear ImGui initialized inside D3D11 Present hook.");
    }
  }

  if (g_imgui_initialized.load() && g_render_target_view) {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Render shared UI
    RenderImGuiUI();

    ImGui::Render();

    // Bind RenderTargetView before rendering ImGui
    g_d3d11_context->OMSetRenderTargets(1, &g_render_target_view, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
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
    ImGui_ImplDX11_CreateDeviceObjects();
    CreateRenderTargetView(swapchain);
  }

  return hr;
}

} // namespace

bool InitializeDx11Hook() {
  if (!EnsureMinHookInitialized()) {
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
    if (EnableHook(vtable[kPresentIndex], reinterpret_cast<void*>(&HookedPresent),
                   reinterpret_cast<void**>(&g_original_present))) {
      g_present_hooked = true;
    }
  }

  if (!g_resize_hooked.load()) {
    if (EnableHook(vtable[kResizeBuffersIndex], reinterpret_cast<void*>(&HookedResizeBuffers),
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
    if (g_game_hwnd && g_original_wnd_proc) {
      SetWindowLongPtrW(g_game_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_original_wnd_proc));
    }
    CleanupRenderTargetView();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_imgui_initialized = false;
  }

  DisableHook(reinterpret_cast<void*>(g_original_present));
  DisableHook(reinterpret_cast<void*>(g_original_resize_buffers));

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

  if (g_mh_ready) {
    (void)MH_DisableHook(MH_ALL_HOOKS);
    (void)MH_Uninitialize();
    g_mh_ready = false;
  }
}

} // namespace dover::overlay
