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

    std::filesystem::path pak_path = std::filesystem::path(dll_path).parent_path() / L"dover_assets.pak";

    // Fallback error handling: If file is missing, do not panic.
    if (!std::filesystem::exists(pak_path)) {
        shared::LogInfo("AssetStorage: dover_assets.pak not found. Crosshair features will be disabled.");
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
        shared::LogError("AssetStorage: Failed to open dover_assets.pak.");
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
        shared::LogError("AssetStorage: Invalid dover_assets.pak format or version.");
        Shutdown();
        return false;
    }

    const PakTocEntry* toc = reinterpret_cast<const PakTocEntry*>(base_ptr + sizeof(PakHeader));

    m_crosshairs.reserve(header->count);
    for (uint32_t i = 0; i < header->count; ++i) {
        CrosshairData data;
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
        
        m_crosshairs.push_back(data);
    }

    m_initialized = true;
    shared::LogInfo("AssetStorage: dover_assets.pak memory-mapped successfully.");
    return true;
}

void AssetStorage::Shutdown() {
    m_crosshairs.clear();
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
