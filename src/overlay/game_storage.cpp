#include "overlay/game_storage.h"
#include "shared/storage.h"
#include "shared/config.h"
#include "shared/log.h"
#include "overlay/overlay_ui.h"
#include "overlay/notes/layout.h"
#include "overlay/notes/manager.h"
#include "overlay/settings/settings_window.h"
#include "overlay/crosshair/crosshair_window.h"
#include "overlay/input/input_window.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

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
    
    // Convert wide-character path to UTF-8 and write to the static buffer safely
    int size = WideCharToMultiByte(CP_UTF8, 0, layout_wide.c_str(), -1, m_layout_path_utf8, sizeof(m_layout_path_utf8) - 1, nullptr, nullptr);
    if (size > 0) {
        m_layout_path_utf8[size - 1] = '\0'; // Explicit Null-Terminator protection!
    } else {
        m_layout_path_utf8[0] = '\0';
    }

    m_initialized = true;
}

void GameStorage::LoadConfig() {
    if (!m_initialized) return;
    auto cfg = GetConfigPath();

    GetOverlayConfig().show_fps   = shared::ReadIniBool(cfg, "osd", "show_fps",   true);
    GetOverlayConfig().show_clock = shared::ReadIniBool(cfg, "osd", "show_clock", true);
    GetOverlayConfig().show_api   = shared::ReadIniBool(cfg, "osd", "show_api",   false);
    GetOverlayConfig().show_gamepad_hud     = shared::ReadIniBool(cfg, "osd", "show_gamepad_hud", false);
    GetOverlayConfig().gamepad_hud_position = shared::ReadIniInt(cfg, "osd", "gamepad_hud_position", 2);
    GetOverlayConfig().gamepad_hud_scale    = shared::ReadIniFloat(cfg, "osd", "gamepad_hud_scale", 1.0f);

    GetOverlayConfig().global_window_alpha = shared::ReadIniFloat(cfg, "theme", "window_alpha", 0.95f);
    GetOverlayConfig().overlay_bg_alpha    = shared::ReadIniFloat(cfg, "theme", "overlay_alpha", 0.63f);

    GetOverlayConfig().hotkey_toggle_main = shared::ReadIniInt(cfg, "hotkeys", "toggle_main", VK_TAB);
    GetOverlayConfig().hotkey_toggle_modifier = shared::ReadIniInt(cfg, "hotkeys", "toggle_modifier", VK_SHIFT);

    for (int i = 0; i < 18; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "map_%d", i);
        GetOverlayConfig().gamepad_to_vk_map[i].vk_code = static_cast<uint8_t>(shared::ReadIniInt(cfg, "input", key, 0));
        
        snprintf(key, sizeof(key), "map_%d_ctrl", i);
        GetOverlayConfig().gamepad_to_vk_map[i].modifier_ctrl = shared::ReadIniBool(cfg, "input", key, false);
        
        snprintf(key, sizeof(key), "map_%d_shift", i);
        GetOverlayConfig().gamepad_to_vk_map[i].modifier_shift = shared::ReadIniBool(cfg, "input", key, false);
        
        snprintf(key, sizeof(key), "map_%d_alt", i);
        GetOverlayConfig().gamepad_to_vk_map[i].modifier_alt = shared::ReadIniBool(cfg, "input", key, false);
    }

    // Sync opacity to open windows
    notes::GetNotesWindow().SetBgAlpha(GetOverlayConfig().global_window_alpha);
    settings::GetSettingsWindow().SetBgAlpha(GetOverlayConfig().global_window_alpha);
}

void GameStorage::SaveConfig() {
    if (!m_initialized) return;
    auto cfg = GetConfigPath();

    shared::WriteIniBool(cfg, "osd", "show_fps",   GetOverlayConfig().show_fps);
    shared::WriteIniBool(cfg, "osd", "show_clock", GetOverlayConfig().show_clock);
    shared::WriteIniBool(cfg, "osd", "show_api",   GetOverlayConfig().show_api);
    shared::WriteIniBool(cfg, "osd", "show_gamepad_hud",     GetOverlayConfig().show_gamepad_hud);
    shared::WriteIniInt(cfg, "osd", "gamepad_hud_position", GetOverlayConfig().gamepad_hud_position);
    shared::WriteIniFloat(cfg, "osd", "gamepad_hud_scale",    GetOverlayConfig().gamepad_hud_scale);

    shared::WriteIniFloat(cfg, "theme", "window_alpha",  GetOverlayConfig().global_window_alpha);
    shared::WriteIniFloat(cfg, "theme", "overlay_alpha", GetOverlayConfig().overlay_bg_alpha);

    shared::WriteIniInt(cfg, "hotkeys", "toggle_main", GetOverlayConfig().hotkey_toggle_main);
    shared::WriteIniInt(cfg, "hotkeys", "toggle_modifier", GetOverlayConfig().hotkey_toggle_modifier);

    for (int i = 0; i < 18; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "map_%d", i);
        shared::WriteIniInt(cfg, "input", key, GetOverlayConfig().gamepad_to_vk_map[i].vk_code);
        
        snprintf(key, sizeof(key), "map_%d_ctrl", i);
        shared::WriteIniBool(cfg, "input", key, GetOverlayConfig().gamepad_to_vk_map[i].modifier_ctrl);
        
        snprintf(key, sizeof(key), "map_%d_shift", i);
        shared::WriteIniBool(cfg, "input", key, GetOverlayConfig().gamepad_to_vk_map[i].modifier_shift);
        
        snprintf(key, sizeof(key), "map_%d_alt", i);
        shared::WriteIniBool(cfg, "input", key, GetOverlayConfig().gamepad_to_vk_map[i].modifier_alt);
    }
}

void GameStorage::LoadState() {
    if (!m_initialized) return;
    auto st = GetStatePath();

    char note_file[64] = {};
    shared::ReadIniString(st, "notes", "selected_note_filename", "", note_file, sizeof(note_file));
    {
      char msg[512];
      snprintf(msg, sizeof(msg), "GameStorage::LoadState - Loaded selected_note_filename: '%s'", note_file);
      shared::LogInfo(msg);
    }
    
    int view_mode = shared::ReadIniInt(st, "notes", "view_mode", 1);
    
    if (note_file[0] != '\0') {
        notes::GetNotesWindow().SelectNoteByFilename(note_file);
    } else {
        shared::LogInfo("GameStorage::LoadState - selected_note_filename was empty, selecting note 0.");
        notes::GetNotesWindow().SelectNote(0, false);
    }
    notes::GetNotesWindow().SetViewMode(view_mode);

    int zoom_idx = shared::ReadIniInt(st, "notes", "zoom_idx", 2);
    notes::GetNotesWindow().SetZoomIndex(zoom_idx);

    int settings_cat = shared::ReadIniInt(st, "settings", "selected_category", 0);
    settings::GetSettingsWindow().SetSelectedCategory(settings_cat);

    bool notes_open = shared::ReadIniBool(st, "notes", "is_open", false);
    notes::GetNotesWindow().SetOpenDirect(notes_open);

    bool settings_open = shared::ReadIniBool(st, "settings", "is_open", false);
    settings::GetSettingsWindow().SetOpenDirect(settings_open);

    bool crosshair_open = shared::ReadIniBool(st, "crosshair", "is_open", false);
    crosshair::GetCrosshairWindow().SetOpenDirect(crosshair_open);
    
    bool input_open = shared::ReadIniBool(st, "inputmap", "is_open", false);
    input::GetInputWindow().SetOpenDirect(input_open);
    
    bool crosshair_active = shared::ReadIniBool(st, "crosshair", "is_active", false);
    crosshair::GetCrosshairWindow().SetCrosshairActive(crosshair_active);
    
    int crosshair_idx = shared::ReadIniInt(st, "crosshair", "selected_index", 0);
    crosshair::GetCrosshairWindow().SetSelectedIndex(crosshair_idx);
    
    float cr = shared::ReadIniFloat(st, "crosshair", "color_r", 1.0f);
    float cg = shared::ReadIniFloat(st, "crosshair", "color_g", 1.0f);
    float cb = shared::ReadIniFloat(st, "crosshair", "color_b", 1.0f);
    float ca = shared::ReadIniFloat(st, "crosshair", "color_a", 1.0f);
    crosshair::GetCrosshairWindow().SetColor(ImVec4(cr, cg, cb, ca));
    
    bool coutline = shared::ReadIniBool(st, "crosshair", "outline_enabled", false);
    crosshair::GetCrosshairWindow().SetOutlineEnabled(coutline);
    
    float ocr = shared::ReadIniFloat(st, "crosshair", "outline_r", 0.0f);
    float ocg = shared::ReadIniFloat(st, "crosshair", "outline_g", 0.0f);
    float ocb = shared::ReadIniFloat(st, "crosshair", "outline_b", 0.0f);
    float oca = shared::ReadIniFloat(st, "crosshair", "outline_a", 1.0f);
    crosshair::GetCrosshairWindow().SetOutlineColor(ImVec4(ocr, ocg, ocb, oca));
    
    float cscale = shared::ReadIniFloat(st, "crosshair", "scale", 1.0f);
    crosshair::GetCrosshairWindow().SetScale(cscale);

    float copacity = shared::ReadIniFloat(st, "crosshair", "opacity", 1.0f);
    crosshair::GetCrosshairWindow().SetOpacity(copacity);
    
    float cpos_x = shared::ReadIniFloat(st, "crosshair", "pos_x", 0.0f);
    float cpos_y = shared::ReadIniFloat(st, "crosshair", "pos_y", 0.0f);
    crosshair::GetCrosshairWindow().SetPosX(cpos_x);
    crosshair::GetCrosshairWindow().SetPosY(cpos_y);
}

void GameStorage::SaveState() {
    if (!m_initialized) return;
    auto st = GetStatePath();

    const char* note_fn = notes::GetNotesWindow().GetSelectedNoteFilename();
    {
      char msg[512];
      snprintf(msg, sizeof(msg), "GameStorage::SaveState - Writing selected_note_filename: '%s'", note_fn);
      shared::LogInfo(msg);
    }

    shared::WriteIniString(st, "notes", "selected_note_filename", note_fn);
    shared::WriteIniInt(st, "notes", "view_mode",           notes::GetNotesWindow().GetViewMode());
    shared::WriteIniInt(st, "notes", "zoom_idx",            notes::GetNotesWindow().GetZoomIndex());
    shared::WriteIniInt(st, "settings", "selected_category", settings::GetSettingsWindow().GetSelectedCategory());

    shared::WriteIniBool(st, "notes", "is_open",            notes::GetNotesWindow().IsOpen());
    shared::WriteIniBool(st, "settings", "is_open",         settings::GetSettingsWindow().IsOpen());
    shared::WriteIniBool(st, "crosshair", "is_open",        crosshair::GetCrosshairWindow().IsOpen());
    shared::WriteIniBool(st, "inputmap", "is_open",         input::GetInputWindow().IsOpen());
    
    shared::WriteIniBool(st, "crosshair", "is_active",      crosshair::GetCrosshairWindow().IsCrosshairActive());
    shared::WriteIniInt(st, "crosshair", "selected_index",  crosshair::GetCrosshairWindow().GetSelectedIndex());
    
    const ImVec4& ccolor = crosshair::GetCrosshairWindow().GetColor();
    shared::WriteIniFloat(st, "crosshair", "color_r",       ccolor.x);
    shared::WriteIniFloat(st, "crosshair", "color_g",       ccolor.y);
    shared::WriteIniFloat(st, "crosshair", "color_b",       ccolor.z);
    shared::WriteIniFloat(st, "crosshair", "color_a",       ccolor.w);
    
    shared::WriteIniBool(st, "crosshair", "outline_enabled",crosshair::GetCrosshairWindow().IsOutlineEnabled());
    const ImVec4& ocolor = crosshair::GetCrosshairWindow().GetOutlineColor();
    shared::WriteIniFloat(st, "crosshair", "outline_r",     ocolor.x);
    shared::WriteIniFloat(st, "crosshair", "outline_g",     ocolor.y);
    shared::WriteIniFloat(st, "crosshair", "outline_b",     ocolor.z);
    shared::WriteIniFloat(st, "crosshair", "outline_a",     ocolor.w);
    
    shared::WriteIniFloat(st, "crosshair", "scale",         crosshair::GetCrosshairWindow().GetScale());
    shared::WriteIniFloat(st, "crosshair", "opacity",       crosshair::GetCrosshairWindow().GetOpacity());
    shared::WriteIniFloat(st, "crosshair", "pos_x",         crosshair::GetCrosshairWindow().GetPosX());
    shared::WriteIniFloat(st, "crosshair", "pos_y",         crosshair::GetCrosshairWindow().GetPosY());
}

} // namespace dover::overlay
