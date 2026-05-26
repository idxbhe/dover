#pragma once

#include <filesystem>
#include <string>

namespace dover::overlay {

// Singleton that manages all per-game storage paths and config/state persistence.
// Must be initialized via Initialize() before any other overlay code runs.
class GameStorage {
public:
    static GameStorage& Get();

    // Call once from SetupImGuiTheme() with the raw exe name (e.g. L"eldenring.exe")
    void Initialize(const std::wstring& exe_name);

    bool IsInitialized() const { return m_initialized; }

    // Path accessors
    std::filesystem::path GetGameDir()    const { return m_game_dir; }
    std::filesystem::path GetNotesDir()   const { return m_game_dir / L"notes"; }
    std::filesystem::path GetConfigPath() const { return m_game_dir / L"config.ini"; }
    std::filesystem::path GetStatePath()  const { return m_game_dir / L"state.ini"; }

    // Safe pointer for io.IniFilename — lifetime matches singleton (= DLL lifetime)
    const char* GetLayoutPathCStr() const { return m_layout_path_utf8.c_str(); }

    // Config: OSD visibility and theme opacity
    void LoadConfig();
    void SaveConfig();

    // State: last active note index, view mode, settings category
    void LoadState();
    void SaveState();

private:
    GameStorage() = default;

    bool m_initialized = false;
    std::filesystem::path m_game_dir;

    // Persistent member string — pointer stays valid for entire DLL lifetime
    std::string m_layout_path_utf8;
};

} // namespace dover::overlay
