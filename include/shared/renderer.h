#pragma once
#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDirect3DDevice9;
struct ID3D12Device;

namespace dover::shared {

void SetDx11Device(ID3D11Device* device);
ID3D11Device* GetDx11Device();

void SetDx11Context(ID3D11DeviceContext* context);
ID3D11DeviceContext* GetDx11Context();

void SetDx9Device(IDirect3DDevice9* device);
IDirect3DDevice9* GetDx9Device();

void SetDx12Device(ID3D12Device* device);
ID3D12Device* GetDx12Device();

extern void* (*g_CreateDx12TextureFn)(const uint8_t* rgba_data, int width, int height);
extern void (*g_ReleaseDx12TextureFn)(void* texture_id);

void* CreateDx12Texture(const uint8_t* rgba_data, int width, int height);
void ReleaseDx12Texture(void* texture_id);

} // namespace dover::shared
