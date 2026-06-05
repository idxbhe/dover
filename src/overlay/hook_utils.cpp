#include "overlay/hook_utils.h"
#include "shared/log.h"
#include <MinHook.h>
#include <mutex>
#include <string>

namespace dover::overlay {

namespace {
std::once_flag g_mh_once;
MH_STATUS g_mh_status = MH_OK;
bool g_mh_ready = false;

bool EnsureMinHookInitialized() {
  std::call_once(g_mh_once, []() {
    g_mh_status = MH_Initialize();
    g_mh_ready = (g_mh_status == MH_OK || g_mh_status == MH_ERROR_ALREADY_INITIALIZED);
    if (!g_mh_ready) {
      dover::shared::LogError("MinHook initialization failed with status: %d", g_mh_status);
    }
  });
  return g_mh_ready;
}
} // namespace

bool InitializeHookSystem() {
  return EnsureMinHookInitialized();
}

void ShutdownHookSystem() {
  if (g_mh_ready) {
    (void)MH_DisableHook(MH_ALL_HOOKS);
    (void)MH_Uninitialize();
    g_mh_ready = false;
  }
}

bool CreateAndEnableHook(void* target, void* detour, void** original) {
  if (!InitializeHookSystem()) {
    return false;
  }
  if (!target || !detour) {
    return false;
  }

  MH_STATUS status = MH_CreateHook(target, detour, original);
  if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
    dover::shared::LogError("MH_CreateHook failed with status: %d", status);
    return false;
  }

  status = MH_EnableHook(target);
  if (status != MH_OK && status != MH_ERROR_ENABLED) {
    dover::shared::LogError("MH_EnableHook failed with status: %d", status);
    return false;
  }

  return true;
}

void DisableAndRemoveHook(void* target) {
  if (target) {
    (void)MH_DisableHook(target);
    (void)MH_RemoveHook(target);
  }
}

} // namespace dover::overlay
