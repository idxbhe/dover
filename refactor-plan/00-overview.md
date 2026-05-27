# Dover Refactoring Master Plan — Overview

## Diagnosis

Hasil analisis mendalam terhadap seluruh source tree Dover (~30 file, ~5500 LOC kode proyek):

### Pelanggaran Kritis terhadap AGENTS.md

1. **Hot-Path Heap Allocations (Prinsip #1 DILANGGAR):**
   - `overlay_ui.cpp` L214-218: 4x `ImGui::ColorConvertFloat4ToU32(ImVec4(...))` dengan literal identik diduplikasi secara verbatim antara Notes button dan Settings button. Bukan pelanggaran heap, tapi waste CPU.
   - `overlay_ui.cpp` RenderImGuiUI(): Seluruh rendering Notes button (~60 LOC) dan Settings button (~60 LOC) adalah copy-paste 90% identik. Duplikasi = lebih banyak instruksi = lebih banyak cache miss.
   - `layout.cpp` RenderSidebarInternal: `std::string title` dibangun via heap setiap frame per-item sidebar (`ExtractTitleFromContent`, `substr`, string concat `"* " + title`).
   - `layout.cpp` RenderEditorInternal: `std::string clean_content` dibangun via heap setiap frame saat `changed == true`. Ini *acceptable* karena hanya terjadi saat input berubah (bukan setiap frame).
   - `formatter.cpp` ProcessWordWrap: `std::string clean_str`, `std::vector<int> map_W_to_C` — alokasi heap setiap callback invocation. Dimitigasi oleh early-exit cache check, tapi saat content berubah, alokasi terjadi.
   - `manager.cpp` CreateAutoNote: Loop `std::to_string` dan string concat — one-shot, bukan hot path. **Acceptable.**
   - `settings_window.cpp` L67: `std::string id_str = "##cat_" + std::to_string(i)` — heap alloc per-frame per-category item.

2. **Global Variable Soup (Prinsip #5 SoC DILANGGAR):**
   - `overlay_ui.h` mengekspor 7 `extern` global variables (`g_show_overlay`, `g_in_overlay_frame`, `g_original_wnd_proc`, `g_active_dx_version`, `g_overlay_bg_alpha`, `g_global_window_alpha`, `g_cfg_show_*`).
   - `theme.h` mengekspor 8 `extern ImFont*` global arrays.
   - Ini menciptakan implicit coupling antar translation units. Setiap file yang include `overlay_ui.h` mendapat akses tulis ke state global tanpa kontrak apa pun.

3. **SoC: theme.cpp Melakukan Terlalu Banyak (Prinsip #5 DILANGGAR):**
   - `SetupImGuiTheme()` selain setup font dan warna, juga: menginisialisasi `GameStorage`, `NotesManager`, `NotesWindow`, `SettingsWindow`, dan memuat config/state. Fungsi ini seharusnya hanya mengurusi tema.

4. **Duplikasi Kode Berat:**
   - `overlay_ui.cpp`: Notes button block (L187-252) dan Settings button block (L256-323) adalah 90%+ identik. Hanya berbeda di ikon, warna, dan target window.
   - `layout.cpp` + `settings_window.cpp`: Sidebar rendering pattern (gradient background + custom selectable items) diduplikasi secara verbatim.

5. **Encapsulation Breach:**
   - `layout.h`: Field `m_edit_buffer[65536]`, `m_view_mode`, dll dibuat `public` untuk diakses fungsi statis. Ini menyalahi prinsip enkapsulasi. Lebih baik gunakan `friend` declaration untuk fungsi statis internal.
   - `base_window.h`: `GetBgAlpha()` accessor ditambahkan hanya untuk mengakali `protected` — harus diganti dengan mekanisme yang benar.

6. **Compiler Warning Hygiene (Sudah Diperbaiki Sebagian):**
   - `/WX` sekarang aktif via `DOVER_STRICT_BUILD`. Sudah bersih.

### Hal yang TIDAK Perlu Diubah (Jujur & Objektif)

- **`dllmain.cpp`**: Minimal, benar, tidak ada masalah.
- **`overlay_runtime.cpp`**: Arsitektur thread sudah benar dan aman.
- **`hook_utils.cpp`**: Clean RAII-like pattern untuk MinHook.
- **`dx9_hook.cpp` & `dx11_hook.cpp`**: Solid. Vtable hooking pattern standar industri.
- **`input_hook.cpp`**: Besar tapi fungsional. Struktur sudah benar. Tidak ada hot-path issue.
- **`manager.cpp`**: Clean. File I/O hanya saat init/save (bukan hot path).
- **`style.cpp` (DoverMarkdownRenderer)**: Virtual dispatch via imgui_md. Ini *inherent* dari library imgui_md — bukan sesuatu yang bisa kita eliminasi tanpa menulis renderer sendiri. **Tidak disentuh.**
- **`shared/*`**: Utility functions. Clean. Tidak ada masalah.

## Strategi Phase

Setiap phase:
- Menghasilkan kode yang HARUS COMPILE tanpa error (`/WX`).
- Tidak mengubah perilaku visual atau fungsional apa pun.
- Cukup kecil untuk AI agent dengan context window terbatas.
- Urut berdasarkan prioritas: Performance → Memory → Zero-Overhead → SoC.

| Phase | File Target | Ringkasan |
|-------|-------------|-----------|
| 01 | `overlay_ui.cpp` | Extract duplicated nav-bar button rendering ke fungsi statis reusable |
| 02 | `overlay_ui.h`, `overlay_ui.cpp` | Konsolidasikan global state ke struct `OverlayState` |
| 03 | `theme.cpp` | Pisahkan inisialisasi subsystem dari setup tema visual |
| 04 | `layout.h` | Fix encapsulation: `friend` static functions, hapus `public` hack |
| 05 | `layout.cpp` sidebar | Eliminasi heap allocs per-frame di sidebar rendering |
| 06 | `settings_window.cpp` | Eliminasi heap allocs per-frame di settings sidebar |
| 07 | `formatter.cpp` | Kurangi alokasi heap di word-wrap hot path |
| 08 | `base_window.cpp` | Hapus `GetBgAlpha()` accessor, perbaiki access pattern |
