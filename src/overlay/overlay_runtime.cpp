#include "overlay/dx9_hook.h"
#include "overlay/dx11_hook.h"
#include "shared/ipc.h"
#include "shared/log.h"

#include <atomic>
#include <windows.h>

namespace dover::overlay {

namespace {
constexpr DWORD kHookRetryDelayMs = 500;
constexpr DWORD kHookRetryMaxAttempts = 120;

std::atomic<bool> g_shutdown_requested{false};

DWORD WINAPI OverlayThreadProc(LPVOID /*param*/) {
  const DWORD pid = GetCurrentProcessId();
  if (!dover::shared::SignalOverlayReadyEvent(pid)) {
    dover::shared::LogError("Failed to signal overlay ready event.");
  }

  dover::shared::LogInfo("Overlay runtime started.");
  DWORD attempts = 0;
  bool hook_installed = false;
  while (!g_shutdown_requested.load() && attempts < kHookRetryMaxAttempts) {
    if (GetModuleHandleW(L"d3d11.dll")) {
      if (InitializeDx11Hook()) {
        dover::shared::LogInfo("DX11 hook installed.");
        hook_installed = true;
        break;
      }
    } else if (GetModuleHandleW(L"d3d9.dll")) {
      if (InitializeDx9Hook()) {
        dover::shared::LogInfo("DX9 hook installed.");
        hook_installed = true;
        break;
      }
    }

    ++attempts;
    if ((attempts % 12) == 0) {
      dover::shared::LogInfo("overlay: hook retry still waiting for graphics init.");
    }

    Sleep(kHookRetryDelayMs);
  }

  if (!g_shutdown_requested.load()) {
    if (!hook_installed) {
      dover::shared::LogError("Hook install timed out.");
      ShutdownDx9Hook();
      ShutdownDx11Hook();
      return 0;
    }

    dover::shared::LogInfo("Overlay runtime entering wait loop.");
    while (!g_shutdown_requested.load()) {
      Sleep(100);
    }
  }

  ShutdownDx9Hook();
  ShutdownDx11Hook();
  dover::shared::LogInfo("Overlay runtime stopped.");
  return 0;
}
} // namespace

bool StartOverlayRuntime(HMODULE module) {
  HANDLE thread = CreateThread(nullptr, 0, OverlayThreadProc, module, 0, nullptr);
  if (!thread) {
    dover::shared::LogError("Failed to start overlay runtime thread.");
    return false;
  }

  CloseHandle(thread);
  return true;
}

void RequestOverlayShutdown() {
  g_shutdown_requested.store(true);
}

} // namespace dover::overlay
