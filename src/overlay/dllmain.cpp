#include <windows.h>

namespace dover::overlay {
bool StartOverlayRuntime(HMODULE module);
void RequestOverlayShutdown();
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    return dover::overlay::StartOverlayRuntime(module) ? TRUE : FALSE;
  }

  if (reason == DLL_PROCESS_DETACH) {
    dover::overlay::RequestOverlayShutdown();
  }

  return TRUE;
}
