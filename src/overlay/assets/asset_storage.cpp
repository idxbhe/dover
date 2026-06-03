#include "overlay/assets/asset_storage.h"
#include "shared/log.h"

#include <windows.h>
#include <filesystem>

// Internal structures mirroring the Python struct format
#pragma pack(push, 1)
struct PakHeader {
    char magic[4]; // 'DPAK'
    uint32_t version;
    uint32_t count;
};

struct PakTocEntry {
    char name[32];
    uint32_t width;
    uint32_t height;
    uint64_t data_offset;
    uint64_t data_size;
};
#pragma pack(pop)

extern "C" IMAGE_DOS_HEADER __ImageBase; // Used to get the current DLL module handle

namespace dover::overlay::assets {

AssetStorage& AssetStorage::Get() {
    static AssetStorage instance;
    return instance;
}

AssetStorage::~AssetStorage() {
    Shutdown();
}

bool AssetStorage::Initialize() {
    if (m_initialized) return true;

    // Get the directory where this DLL resides
    wchar_t dll_path[MAX_PATH];
    if (GetModuleFileNameW((HINSTANCE)&__ImageBase, dll_path, MAX_PATH) == 0) {
        shared::LogError("AssetStorage: Failed to get module file name.");
        return false;
    }

    std::filesystem::path pak_path = std::filesystem::path(dll_path).parent_path() / L"assets.pak";

    // Fallback error handling: If file is missing, do not panic.
    if (!std::filesystem::exists(pak_path)) {
        shared::LogInfo("AssetStorage: assets.pak not found. Crosshair features will be disabled.");
        return false;
    }

    // 1. Open File
    HANDLE hFile = CreateFileW(
        pak_path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        shared::LogError("AssetStorage: Failed to open assets.pak.");
        return false;
    }

    DWORD file_size = GetFileSize(hFile, nullptr);
    if (file_size == INVALID_FILE_SIZE || file_size < sizeof(PakHeader)) {
        shared::LogError("AssetStorage: File is too small to be a valid assets.pak.");
        CloseHandle(hFile);
        return false;
    }

    // 2. Create File Mapping (Zero-overhead RAM mapping)
    HANDLE hMap = CreateFileMappingW(
        hFile,
        nullptr,
        PAGE_READONLY,
        0, 0,
        nullptr
    );

    if (!hMap) {
        shared::LogError("AssetStorage: Failed to create file mapping.");
        CloseHandle(hFile);
        return false;
    }

    // 3. Map View into Virtual Memory Address
    void* pBuf = MapViewOfFile(
        hMap,
        FILE_MAP_READ,
        0, 0,
        0
    );

    if (!pBuf) {
        shared::LogError("AssetStorage: Failed to map view of file.");
        CloseHandle(hMap);
        CloseHandle(hFile);
        return false;
    }

    m_file_handle = hFile;
    m_mapping_handle = hMap;
    m_base_address = pBuf;

    // Parse the PAK file safely
    const uint8_t* base_ptr = static_cast<const uint8_t*>(m_base_address);
    const PakHeader* header = reinterpret_cast<const PakHeader*>(base_ptr);

    if (std::strncmp(header->magic, "DPAK", 4) != 0 || header->version != 1) {
        shared::LogError("AssetStorage: Invalid assets.pak format or version.");
        Shutdown();
        return false;
    }

    uint64_t expected_toc_size = static_cast<uint64_t>(header->count) * sizeof(PakTocEntry);
    if (file_size < sizeof(PakHeader) + expected_toc_size) {
        shared::LogError("AssetStorage: File size is too small to contain the reported TOC.");
        Shutdown();
        return false;
    }
    
    constexpr uint32_t MAX_ASSETS = 10000;
    if (header->count > MAX_ASSETS) {
        shared::LogError("AssetStorage: asset count exceeds arbitrary safety limits.");
        Shutdown();
        return false;
    }

    const PakTocEntry* toc = reinterpret_cast<const PakTocEntry*>(base_ptr + sizeof(PakHeader));

    m_assets.reserve(header->count);
    for (uint32_t i = 0; i < header->count; ++i) {
        if (toc[i].data_offset + toc[i].data_size < toc[i].data_offset || 
            toc[i].data_offset + toc[i].data_size > file_size) {
            shared::LogError("AssetStorage: Asset TOC entry extends beyond EOF. Pak is corrupted.");
            Shutdown();
            return false;
        }

        TextureData data;
        // Ensure null-termination
        char safe_name[33] = {0};
        std::memcpy(safe_name, toc[i].name, 32);
        data.name = std::string(safe_name);
        
        data.width = toc[i].width;
        data.height = toc[i].height;
        data.data_size = toc[i].data_size;
        
        // Zero-copy pointer directly into the mapped memory!
        data.rgba_data = base_ptr + toc[i].data_offset;
        data.texture_id = nullptr;
        
        m_assets.push_back(data);
    }

    // Pre-compute crosshair cache (O(K) pointers for O(1) runtime lookup)
    m_crosshair_cache.clear();
    m_crosshair_cache.reserve(m_assets.size());
    for (auto& asset : m_assets) {
        if (asset.name.rfind("gamepad/", 0) != 0) {
            m_crosshair_cache.push_back(&asset);
        }
    }

    m_initialized = true;
    shared::LogInfo("AssetStorage: assets.pak memory-mapped successfully.");
    return true;
}

void AssetStorage::Shutdown() {
    m_crosshair_cache.clear();
    m_assets.clear();
    m_initialized = false;

    if (m_base_address) {
        UnmapViewOfFile(m_base_address);
        m_base_address = nullptr;
    }
    if (m_mapping_handle) {
        CloseHandle(m_mapping_handle);
        m_mapping_handle = nullptr;
    }
    if (m_file_handle && m_file_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_file_handle);
        m_file_handle = nullptr;
    }
}

} // namespace dover::overlay::assets
