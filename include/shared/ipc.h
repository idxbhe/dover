#pragma once

#include <string>

#include <windows.h>

namespace dover::shared {

std::wstring GetOverlayReadyEventName(DWORD pid);
HANDLE CreateOverlayReadyEvent(DWORD pid);
bool SignalOverlayReadyEvent(DWORD pid);

} // namespace dover::shared
