#pragma once

#include <filesystem>
#include <functional>
#include <array>
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
    void RegisterConfigLoad(StorageCallback cb) { if (m_cfg_load_count < kMaxCallbacks) m_cfg_load[m_cfg_load_count++] = std::move(cb); }
    void RegisterConfigSave(StorageCallback cb) { if (m_cfg_save_count < kMaxCallbacks) m_cfg_save[m_cfg_save_count++] = std::move(cb); }
    void RegisterStateLoad(StorageCallback cb) { if (m_state_load_count < kMaxCallbacks) m_state_load[m_state_load_count++] = std::move(cb); }
    void RegisterStateSave(StorageCallback cb) { if (m_state_save_count < kMaxCallbacks) m_state_save[m_state_save_count++] = std::move(cb); }

    void LoadConfig();
    void SaveConfig();
    void FlushConfig();
    
    void LoadState();
    void SaveState();
    void FlushState();
    
    using CaptureCallback = std::function<void()>;
    void RegisterConfigCapture(CaptureCallback cb) { if (m_cfg_capture_count < kMaxCallbacks) m_cfg_capture[m_cfg_capture_count++] = std::move(cb); }
    void RegisterStateCapture(CaptureCallback cb) { if (m_state_capture_count < kMaxCallbacks) m_state_capture[m_state_capture_count++] = std::move(cb); }

    bool IsConfigCaptureRequested() const { return m_config_capture_requested.load(std::memory_order_acquire); }
    bool TestAndClearConfigCaptureRequested() { return m_config_capture_requested.exchange(false, std::memory_order_acq_rel); }
    void ClearConfigCaptureRequested() { m_config_capture_requested.store(false, std::memory_order_release); }
    bool IsStateCaptureRequested() const { return m_state_capture_requested.load(std::memory_order_acquire); }
    bool TestAndClearStateCaptureRequested() { return m_state_capture_requested.exchange(false, std::memory_order_acq_rel); }
    void ClearStateCaptureRequested() { m_state_capture_requested.store(false, std::memory_order_release); }

    void ExecuteConfigCapture() { for(size_t i = 0; i < m_cfg_capture_count; ++i) m_cfg_capture[i](); }
    void ExecuteStateCapture() { for(size_t i = 0; i < m_state_capture_count; ++i) m_state_capture[i](); }

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

    static constexpr size_t kMaxCallbacks = 16;
    std::array<StorageCallback, kMaxCallbacks> m_cfg_load;
    size_t m_cfg_load_count = 0;
    
    std::array<StorageCallback, kMaxCallbacks> m_cfg_save;
    size_t m_cfg_save_count = 0;
    
    std::array<StorageCallback, kMaxCallbacks> m_state_load;
    size_t m_state_load_count = 0;
    
    std::array<StorageCallback, kMaxCallbacks> m_state_save;
    size_t m_state_save_count = 0;
    
    std::array<CaptureCallback, kMaxCallbacks> m_cfg_capture;
    size_t m_cfg_capture_count = 0;
    
    std::array<CaptureCallback, kMaxCallbacks> m_state_capture;
    size_t m_state_capture_count = 0;
    
    std::atomic<bool> m_config_capture_requested{false};
    std::atomic<bool> m_state_capture_requested{false};
    std::atomic<bool> m_config_flush_ready{false};
    std::atomic<bool> m_state_flush_ready{false};
};

} // namespace dover::shared
