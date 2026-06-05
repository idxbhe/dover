#include "shared/ipc.h"
#include <cstdio>

namespace dover::shared {

void GetOverlayReadyEventName(DWORD pid, wchar_t* out_name, size_t max_count) {
  swprintf_s(out_name, max_count, L"DoverOverlayReady_%u", pid);
}

HANDLE CreateOverlayReadyEvent(DWORD pid) {
  wchar_t name[128];
  GetOverlayReadyEventName(pid, name, 128);
  return CreateEventW(nullptr, TRUE, FALSE, name);
}

bool SignalOverlayReadyEvent(DWORD pid) {
  HANDLE event_handle = CreateOverlayReadyEvent(pid);
  if (!event_handle) {
    return false;
  }

  const BOOL signaled = SetEvent(event_handle);
  CloseHandle(event_handle);
  return signaled != FALSE;
}

} // namespace dover::shared
