#pragma once

#include <windows.h>

namespace dover::overlay {

bool StartOverlayRuntime(HMODULE module);
void RequestOverlayShutdown();
bool IsOverlayShutdownRequested();

} // namespace dover::overlay
