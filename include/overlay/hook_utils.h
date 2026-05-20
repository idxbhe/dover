#pragma once

namespace dover::overlay {

bool InitializeHookSystem();
void ShutdownHookSystem();

bool CreateAndEnableHook(void* target, void* detour, void** original);
void DisableAndRemoveHook(void* target);

} // namespace dover::overlay
