# Phase 06 — Eliminasi Heap Allocs di Settings Sidebar

## Target
`src/overlay/settings/settings_window.cpp` — fungsi `RenderContent`

## Problem
Mirip dengan Phase 05, settings sidebar memiliki:
```cpp
std::string id_str = "##cat_" + std::to_string(i);  // L67
```
Ini heap alloc per-frame per-kategori (4 item = 4 heap allocs per frame).

Selain itu, sidebar rendering pattern (gradient background, custom selectable, highlight rect, text rendering) diduplikasi secara hampir identik dengan notes sidebar — tapi ini adalah masalah SoC yang lebih besar dan TIDAK akan ditangani di phase ini. Phase ini fokus hanya pada eliminasi heap alloc.

## Instruksi Teknis

### 1. Ganti `id_str` dengan `snprintf` ke stack buffer:

```cpp
char id_buf[16];
snprintf(id_buf, sizeof(id_buf), "##cat_%d", i);
```

Ganti `id_str.c_str()` → `id_buf` pada pemanggilan `ImGui::Selectable()`.

### 2. Periksa apakah ada string construction lain di hot path:

Settings sidebar menggunakan `Category` struct dengan `const char*` fields (icon dan name). Ini sudah zero-alloc — **tidak perlu diubah**.

Formatting label text juga menggunakan `const char*` literals. **Tidak ada masalah.**

Satu-satunya pelanggaran adalah `id_str` di L67.

## Peringatan
- Jangan ubah struktur `Category` — sudah optimal.
- Jangan ubah visual apa pun.
- Phase ini sangat kecil dan bisa diselesaikan dalam <5 menit.

## Kriteria Selesai
- Zero `std::string` construction di settings sidebar loop
- `build.ps1` pass
- Settings sidebar terlihat identik
