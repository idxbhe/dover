#pragma once

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDirect3DDevice9;

namespace dover::shared {

void SetDx11Device(ID3D11Device* device);
ID3D11Device* GetDx11Device();

void SetDx11Context(ID3D11DeviceContext* context);
ID3D11DeviceContext* GetDx11Context();

void SetDx9Device(IDirect3DDevice9* device);
IDirect3DDevice9* GetDx9Device();

} // namespace dover::shared
