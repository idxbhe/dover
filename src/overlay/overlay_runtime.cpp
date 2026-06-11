#include "overlay/dx9_hook.h"
#include "overlay/dx11_hook.h"
#if _WIN64
#include "overlay/dx12_hook.h"
#endif
#include "overlay/input_hook.h"
#include "overlay/hook_utils.h"
#include "overlay/overlay_ui.h"
#include "shared/game_storage.h"
#include "shared/notes/manager.h"
#include "shared/notes/layout.h"
#include "shared/crosshair/crosshair_window.h"
#include "shared/input/input_window.h"
#include "shared/ipc.h"
#include "shared/log.h"
#include "shared/engine_quirks.h"

#include <atomic>
#include <windows.h>

namespace dover::overlay {

namespace {
constexpr DWORD kHookRetryDelayMs = 500;
constexpr DWORD kHookRetryMaxAttempts = 120;

std::atomic<bool> g_shutdown_requested{false};

DWORD WINAPI OverlayThreadProc(LPVOID /*param*/) {
  dover::shared::InitializeLogging();
  dover::shared::InitializeEngineQuirks();
  const DWORD pid = GetCurrentProcessId();
  if (!dover::shared::SignalOverlayReadyEvent(pid)) {
    dover::shared::LogError("Failed to signal overlay ready event.");
  }

  dover::shared::LogInfo("Overlay runtime started.");
  DWORD attempts = 0;
  bool hook_installed = false;
  while (!g_shutdown_requested.load(std::memory_order_acquire) && attempts < kHookRetryMaxAttempts) {
#if _WIN64
    if (GetModuleHandleW(L"d3d12.dll")) {
      if (InitializeDx12Hook()) {
        dover::shared::LogInfo("DX12 hook installed.");
        hook_installed = true;
        break;
      }
    } else if (GetModuleHandleW(L"d3d11.dll")) {
#else
    if (GetModuleHandleW(L"d3d11.dll")) {
#endif
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
#if _WIN64
      ShutdownDx12Hook();
#endif
      ShutdownHookSystem();
      return 0;
    }

    // Initialize input state API hooking
    if (InitializeInputHooks()) {
      dover::shared::LogInfo("Input state API hooks installed successfully.");
    } else {
      dover::shared::LogError("Some input API hooks failed to install.");
    }

    dover::shared::LogInfo("Overlay runtime entering wait loop.");
    while (!g_shutdown_requested.load(std::memory_order_acquire)) {
      PollGamepadToggle();
      PollKeyboardToggle();
      Sleep(16); // 16ms poll for responsive Guide button toggle
      if (dover::shared::GameStorage::Get().IsConfigFlushReady()) {
        dover::shared::GameStorage::Get().FlushConfig();
        dover::shared::GameStorage::Get().ClearConfigFlushReady();
      }
      if (dover::shared::GameStorage::Get().IsStateFlushReady()) {
        dover::shared::GameStorage::Get().FlushState();
        dover::shared::GameStorage::Get().ClearStateFlushReady();
      }
    }
  }

  // Ensure render thread has time to observe shutdown and exit the hot path
  Sleep(200);

  shared::notes::GetNotesWindow().Shutdown();
  shared::crosshair::GetCrosshairWindow().Shutdown();
  shared::notes::AutoSaveAll();
  shared::notes::ShutdownNotesManager();
  
  if (dover::shared::GameStorage::Get().IsStateFlushReady()) {
    dover::shared::GameStorage::Get().FlushState();
  }
  if (dover::shared::GameStorage::Get().IsConfigFlushReady()) {
    dover::shared::GameStorage::Get().FlushConfig();
  }
  ShutdownInputHooks();
  ShutdownDx9Hook();
  ShutdownDx11Hook();
#if _WIN64
  ShutdownDx12Hook();
#endif
  ShutdownHookSystem();
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
  g_shutdown_requested.store(true, std::memory_order_release);
}

bool IsOverlayShutdownRequested() {
  return g_shutdown_requested.load(std::memory_order_acquire);
}

} // namespace dover::overlay
