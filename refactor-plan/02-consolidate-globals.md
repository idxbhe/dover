# Phase 02 — Konsolidasi Global State ke Struct

## Target
- `include/overlay/overlay_ui.h`
- `src/overlay/overlay_ui.cpp`
- Semua file yang mereferensi `g_show_overlay`, `g_in_overlay_frame`, `g_overlay_bg_alpha`, `g_global_window_alpha`, `g_cfg_show_fps`, `g_cfg_show_clock`, `g_cfg_show_api`

## Problem
7 variabel global `extern` tersebar bebas di `overlay_ui.h`. Siapa pun yang include header ini dapat menulis ke variabel mana pun tanpa kontrak. Ini melanggar prinsip SoC dan data locality:
- `g_show_overlay` dan `g_in_overlay_frame` adalah **runtime state** (berubah setiap frame).
- `g_cfg_show_fps/clock/api` adalah **user config** (berubah saat user toggle di settings).
- `g_overlay_bg_alpha` dan `g_global_window_alpha` adalah **theme config**.

Mencampur lifetime dan mutation-frequency dalam scope global yang sama membuat reasoning tentang state menjadi mustahil.

## Instruksi Teknis

### 1. Di `overlay_ui.h`, ganti 7 extern variabel dengan:

```cpp
struct OverlayState {
    bool show_overlay = false;
    bool in_overlay_frame = false;
    WNDPROC original_wnd_proc = nullptr;
    const char* active_dx_version = "Unknown API";
};

struct OverlayConfig {
    float overlay_bg_alpha = 0.63f;
    float global_window_alpha = 0.95f;
    bool show_fps = true;
    bool show_clock = true;
    bool show_api = false;
};

OverlayState& GetOverlayState();
OverlayConfig& GetOverlayConfig();
```

### 2. Di `overlay_ui.cpp`, implementasi singleton akses:

```cpp
OverlayState& GetOverlayState() {
    static OverlayState s;
    return s;
}
OverlayConfig& GetOverlayConfig() {
    static OverlayConfig s;
    return s;
}
```

### 3. Update semua consumer:

File-file yang perlu diubah (find-and-replace mekanis):
- `overlay_ui.cpp`: Ganti `g_show_overlay` → `GetOverlayState().show_overlay`, dst.
- `input_hook.cpp`: Mereferensi `g_show_overlay`, `g_in_overlay_frame`.
- `dx11_hook.cpp` & `dx9_hook.cpp`: Mereferensi `g_in_overlay_frame`, `g_original_wnd_proc`, `g_active_dx_version`.
- `game_storage.cpp`: Mereferensi `g_cfg_show_*`, `g_overlay_bg_alpha`, `g_global_window_alpha`.
- `settings_window.cpp`: Mereferensi `g_cfg_show_*` jika ada.

### 4. HAPUS semua `extern` variabel lama dari header.

## Peringatan Kritis
- `g_original_wnd_proc` diakses dari `HookedWndProc` di `input_hook.cpp` dan di-set di `dx9_hook.cpp`/`dx11_hook.cpp`. Pastikan akses via `GetOverlayState()` — ini thread-safe karena pointer ini hanya diwrite sekali saat init.
- `g_in_overlay_frame` di-set di hook thread dan dibaca di hook thread yang sama. Tidak perlu atomic.
- `g_allow_xinput` tetap `thread_local`, jangan sentuh.

## Kriteria Selesai
- Seluruh `extern` variabel global kecuali `g_allow_xinput` sudah tereliminasi dari header
- `build.ps1` pass (`/WX`)
- Behavior identik
