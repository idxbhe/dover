#pragma once

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace dover::overlay {

bool InitializeDx11Hook();
void ShutdownDx11Hook();

ID3D11Device* GetDx11Device();
ID3D11DeviceContext* GetDx11Context();

} // namespace dover::overlay
