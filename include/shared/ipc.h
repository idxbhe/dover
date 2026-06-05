#pragma once

#include <string>

#include <windows.h>

namespace dover::shared {

void GetOverlayReadyEventName(DWORD pid, wchar_t* out_name, size_t max_count);
HANDLE CreateOverlayReadyEvent(DWORD pid);
bool SignalOverlayReadyEvent(DWORD pid);

} // namespace dover::shared
