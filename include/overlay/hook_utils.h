#pragma once

namespace dover::overlay {

bool InitializeHookSystem();
void ShutdownHookSystem();

bool CreateAndEnableHook(void* target, void* detour, void** original);
void DisableAndRemoveHook(void* target);

bool CreateVTableHook(void* instance, int vtable_index, void* detour, void** original);
void RemoveVTableHook(void* instance, int vtable_index, void* original);
void RemoveVTableHookFromAddress(void** vtable, int vtable_index, void* original);

} // namespace dover::overlay
