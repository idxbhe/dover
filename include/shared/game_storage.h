#pragma once

#include <filesystem>
#include <functional>
#include <vector>

namespace dover::shared {

// Singleton that manages all per-game storage paths and config/state persistence.
// Must be initialized via Initialize() before any other code runs.
class GameStorage {
public:
    static GameStorage& Get();

    // Call once with the raw exe name (e.g. L"eldenring.exe")
    void Initialize(const std::wstring& exe_name);

    bool IsInitialized() const { return m_initialized; }

    // Path accessors
    std::filesystem::path GetGameDir()    const { return m_game_dir; }
    std::filesystem::path GetNotesDir()   const { return m_game_dir / L"notes"; }
    std::filesystem::path GetConfigPath() const { return m_game_dir / L"config.ini"; }
    std::filesystem::path GetStatePath()  const { return m_game_dir / L"state.ini"; }

    // Safe pointer for io.IniFilename — lifetime matches singleton
    const char* GetLayoutPathCStr() const { return m_layout_path_utf8; }

    // Delegation pattern for loading/saving
    using StorageCallback = std::function<void(const std::filesystem::path&)>;
    void RegisterConfigLoad(StorageCallback cb) { m_cfg_load.push_back(std::move(cb)); }
    void RegisterConfigSave(StorageCallback cb) { m_cfg_save.push_back(std::move(cb)); }
    void RegisterStateLoad(StorageCallback cb) { m_state_load.push_back(std::move(cb)); }
    void RegisterStateSave(StorageCallback cb) { m_state_save.push_back(std::move(cb)); }

    void LoadConfig();
    void SaveConfig();
    void LoadState();
    void SaveState();

private:
    GameStorage() = default;

    bool m_initialized = false;
    std::filesystem::path m_game_dir;

    char m_layout_path_utf8[260] = {0};

    std::vector<StorageCallback> m_cfg_load;
    std::vector<StorageCallback> m_cfg_save;
    std::vector<StorageCallback> m_state_load;
    std::vector<StorageCallback> m_state_save;
};

} // namespace dover::shared
