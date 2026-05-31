#pragma once

struct ID3D11Device;

namespace dover::overlay {

bool InitializeDx11Hook();
void ShutdownDx11Hook();

ID3D11Device* GetDx11Device();

} // namespace dover::overlay
