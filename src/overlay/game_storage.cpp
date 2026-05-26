#include "overlay/game_storage.h"
#include "shared/storage.h"
#include "shared/config.h"
#include "shared/log.h"
#include "overlay/overlay_ui.h"
#include "overlay/notes/layout.h"
#include "overlay/notes/manager.h"
#include "overlay/settings/settings_window.h"

#include <windows.h>

namespace dover::overlay {

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

    // Build and cache the layout.ini UTF-8 path string — this pointer must outlive ImGui
    fs::path layout_path = m_game_dir / L"layout.ini";
    std::wstring layout_wide = layout_path.wstring();
    int size = WideCharToMultiByte(CP_UTF8, 0, layout_wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    m_layout_path_utf8.resize(static_cast<size_t>(size));
    WideCharToMultiByte(CP_UTF8, 0, layout_wide.c_str(), -1, m_layout_path_utf8.data(), size, nullptr, nullptr);
    // Remove null terminator from std::string size (WideCharToMultiByte includes it)
    if (!m_layout_path_utf8.empty() && m_layout_path_utf8.back() == '\0') {
        m_layout_path_utf8.pop_back();
    }

    m_initialized = true;
}

void GameStorage::LoadConfig() {
    if (!m_initialized) return;
    auto cfg = GetConfigPath();

    g_cfg_show_fps   = shared::ReadIniBool(cfg, "osd", "show_fps",   true);
    g_cfg_show_clock = shared::ReadIniBool(cfg, "osd", "show_clock", true);
    g_cfg_show_api   = shared::ReadIniBool(cfg, "osd", "show_api",   false);

    g_global_window_alpha = shared::ReadIniFloat(cfg, "theme", "window_alpha", 0.95f);
    g_overlay_bg_alpha    = shared::ReadIniFloat(cfg, "theme", "overlay_alpha", 0.63f);

    // Sync opacity to open windows
    notes::GetNotesWindow().SetBgAlpha(g_global_window_alpha);
    settings::GetSettingsWindow().SetBgAlpha(g_global_window_alpha);
}

void GameStorage::SaveConfig() {
    if (!m_initialized) return;
    auto cfg = GetConfigPath();

    shared::WriteIniBool(cfg, "osd", "show_fps",   g_cfg_show_fps);
    shared::WriteIniBool(cfg, "osd", "show_clock", g_cfg_show_clock);
    shared::WriteIniBool(cfg, "osd", "show_api",   g_cfg_show_api);

    shared::WriteIniFloat(cfg, "theme", "window_alpha",  g_global_window_alpha);
    shared::WriteIniFloat(cfg, "theme", "overlay_alpha", g_overlay_bg_alpha);
}

void GameStorage::LoadState() {
    if (!m_initialized) return;
    auto st = GetStatePath();

    std::string note_file = shared::ReadIniString(st, "notes", "selected_note_filename", "");
    {
      std::string msg = "GameStorage::LoadState - Loaded selected_note_filename: '" + note_file + "'";
      shared::LogInfo(msg.c_str());
    }
    
    int view_mode = shared::ReadIniInt(st, "notes", "view_mode", 1);
    
    if (!note_file.empty()) {
        notes::GetNotesWindow().SelectNoteByFilename(note_file);
    } else {
        shared::LogInfo("GameStorage::LoadState - selected_note_filename was empty, selecting note 0.");
        notes::GetNotesWindow().SelectNote(0, false);
    }
    notes::GetNotesWindow().SetViewMode(view_mode);

    int settings_cat = shared::ReadIniInt(st, "settings", "selected_category", 0);
    settings::GetSettingsWindow().SetSelectedCategory(settings_cat);

    bool notes_open = shared::ReadIniBool(st, "notes", "is_open", false);
    notes::GetNotesWindow().SetOpenDirect(notes_open);

    bool settings_open = shared::ReadIniBool(st, "settings", "is_open", false);
    settings::GetSettingsWindow().SetOpenDirect(settings_open);
}

void GameStorage::SaveState() {
    if (!m_initialized) return;
    auto st = GetStatePath();

    std::string note_fn = notes::GetNotesWindow().GetSelectedNoteFilename();
    {
      std::string msg = "GameStorage::SaveState - Writing selected_note_filename: '" + note_fn + "'";
      shared::LogInfo(msg.c_str());
    }

    shared::WriteIniString(st, "notes", "selected_note_filename", note_fn.c_str());
    shared::WriteIniInt(st, "notes", "view_mode",           notes::GetNotesWindow().GetViewMode());
    shared::WriteIniInt(st, "settings", "selected_category", settings::GetSettingsWindow().GetSelectedCategory());

    shared::WriteIniBool(st, "notes", "is_open",            notes::GetNotesWindow().IsOpen());
    shared::WriteIniBool(st, "settings", "is_open",         settings::GetSettingsWindow().IsOpen());
}

} // namespace dover::overlay
