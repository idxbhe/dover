#pragma once
#include <cstdint>

namespace dover::overlay {

bool InitializeDx12Hook();
void ShutdownDx12Hook();

// Helper functions for UI windows to allocate texture SRVs directly into our descriptor heap
void* CreateDx12Texture(const uint8_t* rgba_data, int width, int height);
void ReleaseDx12Texture(void* texture_id);

} // namespace dover::overlay
