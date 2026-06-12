#include "overlay/hook_utils.h"
#include "shared/log.h"
#include <windows.h>
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

bool CreateVTableHook(void* instance, int vtable_index, void* detour, void** original) {
  if (!instance || !detour || !original) return false;

  void** vtable = *reinterpret_cast<void***>(instance);
  void*  target = vtable[vtable_index];
  *original = target;

  DWORD old_protect = 0;
  void** entry_addr = &vtable[vtable_index];

  if (!VirtualProtect(entry_addr, sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protect)) {
    dover::shared::LogError("CreateVTableHook: VirtualProtect failed (err=%lu)", GetLastError());
    return false;
  }

  vtable[vtable_index] = detour;

  DWORD dummy_protect = 0;
  VirtualProtect(entry_addr, sizeof(void*), old_protect, &dummy_protect);

  return true;
}

void RemoveVTableHook(void* instance, int vtable_index, void* original) {
  if (!instance || !original) return;

  void** vtable = *reinterpret_cast<void***>(instance);
  RemoveVTableHookFromAddress(vtable, vtable_index, original);
}

void RemoveVTableHookFromAddress(void** vtable, int vtable_index, void* original) {
  if (!vtable || !original) return;

  DWORD old_protect = 0;
  void** entry_addr = &vtable[vtable_index];

  if (!VirtualProtect(entry_addr, sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protect)) {
    dover::shared::LogError("RemoveVTableHookFromAddress: VirtualProtect failed (err=%lu)", GetLastError());
    return;
  }

  vtable[vtable_index] = original;

  DWORD dummy_protect = 0;
  VirtualProtect(entry_addr, sizeof(void*), old_protect, &dummy_protect);
}

} // namespace dover::overlay
