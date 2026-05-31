#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dover::overlay::assets {

struct CrosshairData {
    std::string name;
    uint32_t width;
    uint32_t height;
    const uint8_t* rgba_data;
    uint64_t data_size;
    void* texture_id = nullptr; // For ImGui::Image
};

class AssetStorage {
public:
    static AssetStorage& Get();

    // Opens the .pak file using memory mapping. Safe to call multiple times.
    // Returns false if file not found or corrupted (safe fallback).
    bool Initialize();
    
    // Unmaps memory and closes handles.
    void Shutdown();
    
    bool IsInitialized() const { return m_initialized; }
    
    std::vector<CrosshairData>& GetCrosshairs() { return m_crosshairs; }

private:
    AssetStorage() = default;
    ~AssetStorage();

    bool m_initialized = false;
    void* m_file_handle = nullptr;     // HANDLE
    void* m_mapping_handle = nullptr;  // HANDLE
    void* m_base_address = nullptr;    // void*

    std::vector<CrosshairData> m_crosshairs;
};

} // namespace dover::overlay::assets
