#include "shared/settings/app_config.h"
#include "shared/config.h"
#include <cstdio>

namespace dover::shared {

AppConfig& GetAppConfig() {
    static AppConfig config;
    return config;
}

void LoadAppConfigFromIni(const std::filesystem::path& path) {
    auto& config = GetAppConfig();
    config.show_fps.store(ReadIniBool(path, "osd", "show_fps", true), std::memory_order_relaxed);
    config.show_clock.store(ReadIniBool(path, "osd", "show_clock", true), std::memory_order_relaxed);
    config.show_api.store(ReadIniBool(path, "osd", "show_api", false), std::memory_order_relaxed);
    config.show_gamepad_hud.store(ReadIniBool(path, "osd", "show_gamepad_hud", false), std::memory_order_relaxed);
    config.gamepad_hud_position.store(ReadIniInt(path, "osd", "gamepad_hud_position", 2), std::memory_order_relaxed);
    config.gamepad_hud_scale.store(ReadIniFloat(path, "osd", "gamepad_hud_scale", 1.0f), std::memory_order_relaxed);

    config.global_window_alpha.store(ReadIniFloat(path, "theme", "window_alpha", 0.95f), std::memory_order_relaxed);
    config.overlay_bg_alpha.store(ReadIniFloat(path, "theme", "overlay_alpha", 0.63f), std::memory_order_relaxed);

    config.hotkey_toggle_main.store(ReadIniInt(path, "hotkeys", "toggle_main", VK_TAB), std::memory_order_relaxed);
    config.hotkey_toggle_modifier.store(ReadIniInt(path, "hotkeys", "toggle_modifier", VK_SHIFT), std::memory_order_relaxed);

    int raw_method = ReadIniInt(path, "advanced", "injection_method", static_cast<int>(InjectionMethod::PureVTable));
    if (raw_method < 0 || raw_method > 1) raw_method = 0;
    config.injection_method.store(static_cast<InjectionMethod>(raw_method), std::memory_order_relaxed);

    for (int i = 0; i < 18; ++i) {
        char key[32];
        GamepadMapping mapping = {};
        snprintf(key, sizeof(key), "map_%d", i);
        mapping.vk_code = static_cast<uint8_t>(ReadIniInt(path, "input", key, 0));
        
        snprintf(key, sizeof(key), "map_%d_ctrl", i);
        mapping.modifier_ctrl = ReadIniBool(path, "input", key, false);
        
        snprintf(key, sizeof(key), "map_%d_shift", i);
        mapping.modifier_shift = ReadIniBool(path, "input", key, false);
        
        snprintf(key, sizeof(key), "map_%d_alt", i);
        mapping.modifier_alt = ReadIniBool(path, "input", key, false);

        config.gamepad_to_vk_map[i].store(mapping, std::memory_order_relaxed);
    }
}

void SaveAppConfigToIni(const std::filesystem::path& path, const AppConfigPOD* snap_pod) {
    auto& config = GetAppConfig();
    
    // Use snapshot if provided (asynchronous flush), otherwise read directly (synchronous Launcher save)
    bool use_snap = (snap_pod != nullptr);
    
    WriteIniBool(path, "osd", "show_fps", use_snap ? snap_pod->show_fps : config.show_fps.load());
    WriteIniBool(path, "osd", "show_clock", use_snap ? snap_pod->show_clock : config.show_clock.load());
    WriteIniBool(path, "osd", "show_api", use_snap ? snap_pod->show_api : config.show_api.load());
    WriteIniBool(path, "osd", "show_gamepad_hud", use_snap ? snap_pod->show_gamepad_hud : config.show_gamepad_hud.load());
    WriteIniInt(path, "osd", "gamepad_hud_position", use_snap ? snap_pod->gamepad_hud_position : config.gamepad_hud_position.load());
    WriteIniFloat(path, "osd", "gamepad_hud_scale", use_snap ? snap_pod->gamepad_hud_scale : config.gamepad_hud_scale.load());

    WriteIniFloat(path, "theme", "window_alpha", use_snap ? snap_pod->global_window_alpha : config.global_window_alpha.load());
    WriteIniFloat(path, "theme", "overlay_alpha", use_snap ? snap_pod->overlay_bg_alpha : config.overlay_bg_alpha.load());

    WriteIniInt(path, "hotkeys", "toggle_main", use_snap ? snap_pod->hotkey_toggle_main : config.hotkey_toggle_main.load());
    WriteIniInt(path, "hotkeys", "toggle_modifier", use_snap ? snap_pod->hotkey_toggle_modifier : config.hotkey_toggle_modifier.load());

    WriteIniInt(path, "advanced", "injection_method", static_cast<int>(use_snap ? snap_pod->injection_method : config.injection_method.load()));

    for (int i = 0; i < 18; ++i) {
        char key[32];
        auto mapping = use_snap ? snap_pod->gamepad_to_vk_map[i] : config.gamepad_to_vk_map[i].load();
        
        snprintf(key, sizeof(key), "map_%d", i);
        WriteIniInt(path, "input", key, mapping.vk_code);
        
        snprintf(key, sizeof(key), "map_%d_ctrl", i);
        WriteIniBool(path, "input", key, mapping.modifier_ctrl);
        
        snprintf(key, sizeof(key), "map_%d_shift", i);
        WriteIniBool(path, "input", key, mapping.modifier_shift);
        
        snprintf(key, sizeof(key), "map_%d_alt", i);
        WriteIniBool(path, "input", key, mapping.modifier_alt);
    }
}

} // namespace dover::shared
