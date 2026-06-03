#include "shared/renderer.h"

namespace dover::shared {

static ID3D11Device* g_dx11_device = nullptr;
static ID3D11DeviceContext* g_dx11_context = nullptr;
static IDirect3DDevice9* g_dx9_device = nullptr;

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

} // namespace dover::shared
