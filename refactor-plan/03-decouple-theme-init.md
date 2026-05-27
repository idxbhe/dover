# Phase 03 — Pisahkan Inisialisasi Subsystem dari Theme

## Target
- `src/overlay/theme.cpp`
- `include/overlay/theme.h`

## Problem
`SetupImGuiTheme()` saat ini melakukan 5 hal sekaligus:
1. Resolve nama exe game
2. Inisialisasi `GameStorage` 
3. Load fonts dari memori
4. Inisialisasi `NotesManager`, `NotesWindow`, `SettingsWindow`, load config/state
5. Terapkan warna/style ImGui

Ini melanggar SoC secara frontal. Fungsi bernama "SetupImGuiTheme" seharusnya HANYA mengurusi tema (font + warna). Pencampuran ini juga menyebabkan `theme.cpp` harus include `notes/manager.h`, `notes/layout.h`, `settings/settings_window.h`, dan `game_storage.h` — dependensi yang tidak ada hubungannya dengan tema.

## Instruksi Teknis

### 1. Buat fungsi baru di `theme.h` / `theme.cpp`:

```cpp
// Satu-satunya entry point inisialisasi overlay (dipanggil dari HookedPresent/HookedEndScene)
void InitializeOverlay();
```

### 2. Pindahkan logika non-tema KELUAR dari `SetupImGuiTheme()`:

`InitializeOverlay()` akan berisi urutan:
```cpp
void InitializeOverlay() {
    // 1. Resolve game exe name
    wchar_t exe_name_w[MAX_PATH] = {};
    GetModuleBaseNameW(GetCurrentProcess(), nullptr, exe_name_w, MAX_PATH);
    std::wstring exe_name(exe_name_w);

    // 2. Init GameStorage
    GameStorage::Get().Initialize(exe_name);

    // 3. Set ImGui INI path
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = GameStorage::Get().GetLayoutPathCStr();

    // 4. Setup theme (font + colors) 
    SetupImGuiTheme();

    // 5. Init subsystems
    notes::InitializeNotesManager(GameStorage::Get().GetNotesDir());
    notes::GetNotesWindow().Initialize();
    settings::GetSettingsWindow().Initialize();

    // 6. Load persistent config/state
    GameStorage::Get().LoadConfig();
    GameStorage::Get().LoadState();
}
```

### 3. Reduksi `SetupImGuiTheme()` menjadi HANYA:
- Font loading (step 4 dari fungsi lama)
- Style/color application (step 5 dari fungsi lama)
- Hapus semua include yang tidak relevan (`game_storage.h`, `notes/manager.h`, dll.)

### 4. Update caller di `dx11_hook.cpp` dan `dx9_hook.cpp`:
- Ganti `SetupImGuiTheme()` → `InitializeOverlay()` di dalam `HookedPresent` / `HookedEndScene`.
- Perlu tambah `#include "overlay/theme.h"` jika belum ada (sudah ada).

## Peringatan
- Urutan init PENTING: GameStorage HARUS sebelum font loading karena `io.IniFilename` harus di-set sebelum font atlas dibangun.
- `SetupImGuiTheme()` tidak boleh lagi melakukan hal apapun selain font + style.

## Kriteria Selesai
- `SetupImGuiTheme()` hanya mengandung font loading dan color/style setup
- `InitializeOverlay()` mengurusi semua inisialisasi subsystem
- `theme.cpp` tidak lagi include `notes/manager.h`, `notes/layout.h`, atau `settings/settings_window.h`
- `build.ps1` pass
