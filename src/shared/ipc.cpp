#include "shared/ipc.h"

namespace dover::shared {

std::wstring GetOverlayReadyEventName(DWORD pid) {
  return L"DoverOverlayReady_" + std::to_wstring(pid);
}

HANDLE CreateOverlayReadyEvent(DWORD pid) {
  const std::wstring name = GetOverlayReadyEventName(pid);
  return CreateEventW(nullptr, TRUE, FALSE, name.c_str());
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
