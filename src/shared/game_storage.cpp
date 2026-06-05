#include "shared/game_storage.h"
#include "shared/storage.h"
#include "shared/log.h"

#include <windows.h>
#include <cstdio>

namespace dover::shared {

namespace fs = std::filesystem;

GameStorage& GameStorage::Get() {
    static GameStorage instance;
    return instance;
}

void GameStorage::Initialize(const std::wstring& exe_name) {
    if (m_initialized) return;

    // Resolve game directory: Documents/Dover/overlay/games/<exe_name>/
    m_game_dir = shared::EnsureGameDir(exe_name);
    if (m_game_dir.empty()) {
        // Fallback: use temp dir so the app still functions without crashing
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        m_game_dir = fs::path(tmp) / L"dover" / exe_name;
        std::error_code ec;
        fs::create_directories(m_game_dir, ec);
    }

    // Ensure notes subdirectory exists
    {
        std::error_code ec;
        fs::create_directories(m_game_dir / L"notes", ec);
    }

    // Build and cache the layout.ini UTF-8 path string
    fs::path layout_path = m_game_dir / L"layout.ini";
    std::wstring layout_wide = layout_path.wstring();
    
    int size = WideCharToMultiByte(CP_UTF8, 0, layout_wide.c_str(), -1, m_layout_path_utf8, sizeof(m_layout_path_utf8) - 1, nullptr, nullptr);
    if (size > 0) {
        m_layout_path_utf8[size - 1] = '\0';
    } else {
        m_layout_path_utf8[0] = '\0';
    }

    m_initialized = true;
}

void GameStorage::LoadConfig() {
    if (!m_initialized) return;
    auto cfg = GetConfigPath();
    for (size_t i = 0; i < m_cfg_load_count; ++i) m_cfg_load[i](cfg);
}

void GameStorage::FlushConfig() {
    if (!m_initialized) return;
    auto cfg = GetConfigPath();
    for (size_t i = 0; i < m_cfg_save_count; ++i) m_cfg_save[i](cfg);
}

void GameStorage::SaveConfig() {
    m_config_capture_requested.store(true, std::memory_order_release);
}

void GameStorage::LoadState() {
    if (!m_initialized) return;
    auto st = GetStatePath();
    for (size_t i = 0; i < m_state_load_count; ++i) m_state_load[i](st);
}

void GameStorage::FlushState() {
    if (!m_initialized) return;
    auto st = GetStatePath();
    for (size_t i = 0; i < m_state_save_count; ++i) m_state_save[i](st);
}

void GameStorage::SaveState() {
    m_state_capture_requested.store(true, std::memory_order_release);
}

} // namespace dover::shared
