#include "shared/renderer.h"

namespace dover::shared {

static ID3D11Device* g_dx11_device = nullptr;
static ID3D11DeviceContext* g_dx11_context = nullptr;
static IDirect3DDevice9* g_dx9_device = nullptr;
static ID3D12Device* g_dx12_device = nullptr;

void SetDx11Device(ID3D11Device* device) {
    g_dx11_device = device;
}

ID3D11Device* GetDx11Device() {
    return g_dx11_device;
}

void SetDx11Context(ID3D11DeviceContext* context) {
    g_dx11_context = context;
}

ID3D11DeviceContext* GetDx11Context() {
    return g_dx11_context;
}

void SetDx9Device(IDirect3DDevice9* device) {
    g_dx9_device = device;
}

IDirect3DDevice9* GetDx9Device() {
    return g_dx9_device;
}

void SetDx12Device(ID3D12Device* device) {
    g_dx12_device = device;
}

ID3D12Device* GetDx12Device() {
    return g_dx12_device;
}

void* (*g_CreateDx12TextureFn)(const uint8_t* rgba_data, int width, int height) = nullptr;
void (*g_ReleaseDx12TextureFn)(void* texture_id) = nullptr;

void* CreateDx12Texture(const uint8_t* rgba_data, int width, int height) {
    if (g_CreateDx12TextureFn) return g_CreateDx12TextureFn(rgba_data, width, height);
    return nullptr;
}

void ReleaseDx12Texture(void* texture_id) {
    if (g_ReleaseDx12TextureFn) g_ReleaseDx12TextureFn(texture_id);
}

} // namespace dover::shared
