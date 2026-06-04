#pragma once

#include <filesystem>
#include <functional>
#include <vector>
#include <atomic>

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
    void FlushConfig();
    
    void LoadState();
    void SaveState();
    void FlushState();
    
    using CaptureCallback = std::function<void()>;
    void RegisterConfigCapture(CaptureCallback cb) { m_cfg_capture.push_back(std::move(cb)); }
    void RegisterStateCapture(CaptureCallback cb) { m_state_capture.push_back(std::move(cb)); }

    bool IsConfigCaptureRequested() const { return m_config_capture_requested.load(std::memory_order_acquire); }
    bool TestAndClearConfigCaptureRequested() { return m_config_capture_requested.exchange(false, std::memory_order_acq_rel); }
    void ClearConfigCaptureRequested() { m_config_capture_requested.store(false, std::memory_order_release); }
    bool IsStateCaptureRequested() const { return m_state_capture_requested.load(std::memory_order_acquire); }
    bool TestAndClearStateCaptureRequested() { return m_state_capture_requested.exchange(false, std::memory_order_acq_rel); }
    void ClearStateCaptureRequested() { m_state_capture_requested.store(false, std::memory_order_release); }

    void ExecuteConfigCapture() { for(auto& cb : m_cfg_capture) cb(); }
    void ExecuteStateCapture() { for(auto& cb : m_state_capture) cb(); }

    bool IsConfigFlushReady() const { return m_config_flush_ready.load(std::memory_order_acquire); }
    void SetConfigFlushReady() { m_config_flush_ready.store(true, std::memory_order_release); }
    void ClearConfigFlushReady() { m_config_flush_ready.store(false, std::memory_order_release); }

    bool IsStateFlushReady() const { return m_state_flush_ready.load(std::memory_order_acquire); }
    void SetStateFlushReady() { m_state_flush_ready.store(true, std::memory_order_release); }
    void ClearStateFlushReady() { m_state_flush_ready.store(false, std::memory_order_release); }

private:
    GameStorage() = default;

    bool m_initialized = false;
    std::filesystem::path m_game_dir;

    char m_layout_path_utf8[260] = {0};

    std::vector<StorageCallback> m_cfg_load;
    std::vector<StorageCallback> m_cfg_save;
    std::vector<StorageCallback> m_state_load;
    std::vector<StorageCallback> m_state_save;
    
    std::vector<CaptureCallback> m_cfg_capture;
    std::vector<CaptureCallback> m_state_capture;
    
    std::atomic<bool> m_config_capture_requested{false};
    std::atomic<bool> m_state_capture_requested{false};
    std::atomic<bool> m_config_flush_ready{false};
    std::atomic<bool> m_state_flush_ready{false};
};

} // namespace dover::shared
