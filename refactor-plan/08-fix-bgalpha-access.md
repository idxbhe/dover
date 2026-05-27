# Phase 08 — Perbaiki BaseWindow BgAlpha Access Pattern

## Target
- `include/overlay/ui/components/base_window.h`
- `include/overlay/notes/layout.h`
- `src/overlay/notes/layout.cpp`

## Problem
Di Phase sebelumnya (sebelum refactor-plan ini), `float& GetBgAlpha()` ditambahkan ke `NotesWindow` di `layout.h` (L28) sebagai workaround agar fungsi statis bisa memodifikasi `m_bg_alpha` (protected di `BaseWindow`) secara in-place — khususnya untuk `ImGui::SliderFloat("##opacity", &window->GetBgAlpha(), ...)`.

Masalah:
- `BaseWindow` sudah punya `float GetBgAlpha() const` (return by value) dan `void SetBgAlpha(float)` di `base_window.h` L65-66.
- Menambahkan `float& GetBgAlpha()` di subclass menyembunyikan (`hides`) fungsi const base — compile-time valid tapi semantically misleading.
- Returning mutable reference ke protected member dari luar class hierarchy melanggar enkapsulasi.

## Instruksi Teknis

### 1. Hapus `float& GetBgAlpha()` dari `layout.h`

### 2. Di `layout.cpp`, fungsi `RenderToolbarInternal` — ubah slider opacity:

**Sebelum:**
```cpp
ImGui::SliderFloat("##opacity", &window->GetBgAlpha(), 0.00f, 1.00f, "");
float grab_center_x = slider_pos.x + window->GetBgAlpha() * slider_width;
```

**Sesudah:**
```cpp
float bg_alpha = window->GetBgAlpha();  // read via const accessor
ImGui::SliderFloat("##opacity", &bg_alpha, 0.00f, 1.00f, "");
if (bg_alpha != window->GetBgAlpha()) {
    window->SetBgAlpha(bg_alpha);  // write via setter
}
float grab_center_x = slider_pos.x + bg_alpha * slider_width;
```

### 3. Ganti semua `window->GetBgAlpha()` read-only sites:
Ini sudah benar — `GetBgAlpha()` return by value. Cukup pastikan tidak ada yang mengambil address lagi.

Lokasi di `layout.cpp` yang perlu diperiksa:
- `RenderToolbarInternal`: slider + track draw + text display
- `RenderSidebarInternal`: gradient background colors

Semua sudah pass-by-value, hanya slider yang butuh address.

### 4. Verifikasi `base_window.h`:
Pastikan `GetBgAlpha() const` dan `SetBgAlpha(float)` sudah ada (mereka ada di L65-66). Tidak perlu diubah.

## Peringatan
- Perbandingan `bg_alpha != window->GetBgAlpha()` menggunakan float equality. Ini OK di sini karena kita hanya ingin mendeteksi apakah slider mengubah nilainya — ImGui sendiri menjamin perubahan hanya terjadi saat user drag.
- Jangan pernah gunakan `==` untuk float comparison di konteks numerik — tapi di sini ini guard condition yang valid.

## Kriteria Selesai
- Tidak ada `float&` accessor yang mengembalikan reference ke protected member
- `build.ps1` pass
- Slider opacity berfungsi identik
