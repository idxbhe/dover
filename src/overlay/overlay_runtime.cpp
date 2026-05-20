#include "overlay/dx9_hook.h"
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
  while (!g_shutdown_requested.load() && attempts < kHookRetryMaxAttempts) {
    if (InitializeDx9Hook()) {
      dover::shared::LogInfo("DX9 hook installed.");
      break;
    }

    ++attempts;
    if ((attempts % 12) == 0) {
      dover::shared::LogInfo("overlay: hook retry still waiting for graphics init.");
    }

    Sleep(kHookRetryDelayMs);
  }

  if (!g_shutdown_requested.load()) {
    if (attempts >= kHookRetryMaxAttempts) {
      dover::shared::LogError("DX9 hook install timed out.");
      ShutdownDx9Hook();
      return 0;
    }

    dover::shared::LogInfo("Overlay runtime entering wait loop.");
    while (!g_shutdown_requested.load()) {
      Sleep(100);
    }
  }

  ShutdownDx9Hook();
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
