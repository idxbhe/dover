#include "overlay/dx9_hook.h"

#include "shared/log.h"

#include <MinHook.h>
#include <d3d9.h>
#include <windows.h>

#include <atomic>
#include <array>
#include <mutex>
#include <string>

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
std::atomic<bool> g_logged_end_scene_once{false};
std::atomic<bool> g_logged_reset_once{false};

std::once_flag g_mh_once;
MH_STATUS g_mh_status = MH_OK;
bool g_mh_ready = false;

struct OverlayStats {
  LARGE_INTEGER frequency{};
  LARGE_INTEGER last{};
  int frames = 0;
  float fps = 0.0f;
};

OverlayStats g_stats{};
struct UiVertex {
  float x;
  float y;
  float z;
  float rhw;
  DWORD color;
};

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

void UpdateStats() {
  if (g_stats.frequency.QuadPart == 0) {
    QueryPerformanceFrequency(&g_stats.frequency);
    QueryPerformanceCounter(&g_stats.last);
  }

  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  ++g_stats.frames;

  const double elapsed = static_cast<double>(now.QuadPart - g_stats.last.QuadPart) /
                         static_cast<double>(g_stats.frequency.QuadPart);
  if (elapsed >= 1.0) {
    g_stats.fps = static_cast<float>(g_stats.frames / elapsed);
    g_stats.frames = 0;
    g_stats.last = now;
  }
}

struct Glyph {
  char ch;
  std::array<uint8_t, 7> rows;
};

constexpr std::array<Glyph, 18> kGlyphs = {{
    Glyph{'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    Glyph{'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    Glyph{'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    Glyph{'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    Glyph{'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    Glyph{'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    Glyph{'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    Glyph{'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    Glyph{'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    Glyph{'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    Glyph{':', {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00}},
    Glyph{'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    Glyph{'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    Glyph{'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    Glyph{'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    Glyph{' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    Glyph{'\'', {0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}},
    Glyph{'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
}};

constexpr const Glyph* FindGlyph(char ch) {
  for (const auto& glyph : kGlyphs) {
    if (glyph.ch == ch) {
      return &glyph;
    }
  }
  return nullptr;
}

void SetOverlayState(IDirect3DDevice9* device) {
  device->SetTexture(0, nullptr);
  device->SetVertexShader(nullptr);
  device->SetPixelShader(nullptr);
  device->SetRenderState(D3DRS_ZENABLE, FALSE);
  device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  device->SetRenderState(D3DRS_LIGHTING, FALSE);
  device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
}

void RestoreOverlayState(IDirect3DDevice9* device, DWORD old_fvf, DWORD old_alpha, DWORD old_z, DWORD old_lighting, DWORD old_cull) {
  device->SetFVF(old_fvf);
  device->SetRenderState(D3DRS_ALPHABLENDENABLE, old_alpha ? TRUE : FALSE);
  device->SetRenderState(D3DRS_ZENABLE, old_z ? TRUE : FALSE);
  device->SetRenderState(D3DRS_LIGHTING, old_lighting ? TRUE : FALSE);
  device->SetRenderState(D3DRS_CULLMODE, old_cull);
}

void DrawRect(IDirect3DDevice9* device, float x, float y, float w, float h, DWORD color) {
  const UiVertex vertices[6] = {
      {x, y, 0.0f, 1.0f, color},
      {x + w, y, 0.0f, 1.0f, color},
      {x, y + h, 0.0f, 1.0f, color},
      {x + w, y, 0.0f, 1.0f, color},
      {x + w, y + h, 0.0f, 1.0f, color},
      {x, y + h, 0.0f, 1.0f, color},
  };

  device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, vertices, sizeof(UiVertex));
}

void DrawTextBitmap(IDirect3DDevice9* device, float x, float y, float scale, const std::wstring& text, DWORD color) {
  float cursor_x = x;
  for (wchar_t wide_ch : text) {
    const char ch = static_cast<char>(wide_ch);
    const Glyph* glyph = FindGlyph(ch);
    if (!glyph) {
      cursor_x += 4.0f * scale;
      continue;
    }

    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if ((glyph->rows[row] & (1 << (4 - col))) == 0) {
          continue;
        }
        DrawRect(device,
                 cursor_x + static_cast<float>(col) * scale,
                 y + static_cast<float>(row) * scale,
                 scale,
                 scale,
                 color);
      }
    }

    cursor_x += 6.0f * scale;
  }
}

void DrawOverlayText(IDirect3DDevice9* device) {
  UpdateStats();
  SYSTEMTIME time{};
  GetLocalTime(&time);

  wchar_t clock_text[64] = {};
  swprintf_s(clock_text, L"%02u:%02u:%02u", time.wHour, time.wMinute, time.wSecond);

  wchar_t fps_text[64] = {};
  swprintf_s(fps_text, L"FPS: %.1f", g_stats.fps);

  DWORD old_fvf = 0;
  DWORD old_alpha = FALSE;
  DWORD old_z = FALSE;
  DWORD old_lighting = FALSE;
  DWORD old_cull = D3DCULL_NONE;
  device->GetFVF(&old_fvf);
  device->GetRenderState(D3DRS_ALPHABLENDENABLE, &old_alpha);
  device->GetRenderState(D3DRS_ZENABLE, &old_z);
  device->GetRenderState(D3DRS_LIGHTING, &old_lighting);
  device->GetRenderState(D3DRS_CULLMODE, &old_cull);

  // Save shaders
  IDirect3DVertexShader9* old_vs = nullptr;
  IDirect3DPixelShader9* old_ps = nullptr;
  device->GetVertexShader(&old_vs);
  device->GetPixelShader(&old_ps);

  // Save texture
  IDirect3DBaseTexture9* old_tex = nullptr;
  device->GetTexture(0, &old_tex);

  SetOverlayState(device);
  device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

  const DWORD kClockColor = D3DCOLOR_ARGB(255, 0, 255, 180);
  const DWORD kFpsColor = D3DCOLOR_ARGB(255, 255, 255, 255);
  DrawTextBitmap(device, 12.0f, 10.0f, 2.0f, clock_text, kClockColor);
  DrawTextBitmap(device, 12.0f, 32.0f, 2.0f, fps_text, kFpsColor);

  RestoreOverlayState(device, old_fvf, old_alpha, old_z, old_lighting, old_cull);

  // Restore shaders
  device->SetVertexShader(old_vs);
  if (old_vs) old_vs->Release();
  device->SetPixelShader(old_ps);
  if (old_ps) old_ps->Release();

  // Restore texture
  device->SetTexture(0, old_tex);
  if (old_tex) old_tex->Release();
}

HRESULT WINAPI HookedEndScene(IDirect3DDevice9* device) {
  if (!g_logged_end_scene_once.exchange(true)) {
    dover::shared::LogInfo("HookedEndScene reached.");
  }
  if (device) {
    DrawOverlayText(device);
  }
  if (g_original_end_scene) {
    return g_original_end_scene(device);
  }
  return D3D_OK;
}

HRESULT WINAPI HookedReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) {
  if (!g_logged_reset_once.exchange(true)) {
    dover::shared::LogInfo("HookedReset reached.");
  }

  HRESULT hr = D3D_OK;
  if (g_original_reset) {
    hr = g_original_reset(device, params);
  }

  return hr;
}

} // namespace

bool InitializeDx9Hook() {
  if (!EnsureMinHookInitialized()) {
    dover::shared::LogError("MinHook initialization failed.");
    return false;
  }

  // Use GetModuleHandle to wait until target process loads D3D9.
  HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll");
  if (!d3d9) {
    return false; // Let the retry loop in overlay_runtime try again
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
  DisableHook(reinterpret_cast<void*>(g_original_end_scene));
  DisableHook(reinterpret_cast<void*>(g_original_reset));

  g_original_end_scene = nullptr;
  g_original_reset = nullptr;
  g_end_scene_hooked = false;
  g_reset_hooked = false;
  g_logged_end_scene_once = false;
  g_logged_reset_once = false;

  if (g_mh_ready) {
    (void)MH_DisableHook(MH_ALL_HOOKS);
    (void)MH_Uninitialize();
    g_mh_ready = false;
  }
}

} // namespace dover::overlay
