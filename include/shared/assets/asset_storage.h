#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace dover::shared::assets {

struct TextureData {
    char name[33];
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
    
    std::vector<TextureData>& GetAssets() { return m_assets; }
    const std::vector<TextureData*>& GetCrosshairs() const { return m_crosshair_cache; }
    
    TextureData* GetAsset(const std::string& name) {
        for (auto& asset : m_assets) {
            if (name == asset.name) return &asset;
        }
        return nullptr;
    }
    
    TextureData* GetAsset(const char* name) {
        for (auto& asset : m_assets) {
            if (std::strcmp(asset.name, name) == 0) return &asset;
        }
        return nullptr;
    }

    void* GetIconForGame(const std::wstring& executable_path);

private:
    AssetStorage() = default;
    ~AssetStorage();

    bool m_initialized = false;
    void* m_file_handle = nullptr;     // HANDLE
    void* m_mapping_handle = nullptr;  // HANDLE
    void* m_base_address = nullptr;    // void*

    std::vector<TextureData> m_assets;
    std::vector<TextureData*> m_crosshair_cache;
    std::unordered_map<std::wstring, void*> m_game_icons;
};

} // namespace dover::shared::assets
