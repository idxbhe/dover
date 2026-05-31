#pragma once

struct IDirect3DDevice9;

namespace dover::overlay {

bool InitializeDx9Hook();
void ShutdownDx9Hook();

IDirect3DDevice9* GetDx9Device();

} // namespace dover::overlay
