# Phase 07 — Kurangi Alokasi Heap di Formatter Word-Wrap

## Target
`src/overlay/notes/formatter.cpp` — fungsi `ProcessWordWrap` dan `WrapGlobalBuffer`

## Problem

### `ProcessWordWrap` (dipanggil via FormatCallback setiap frame saat editor aktif):
1. `std::string clean_str` — heap alloc, dibangun char-by-char via `+=`
2. `std::vector<int> map_W_to_C(data->BufTextLen + 1)` — heap alloc
3. `std::string wrapped_str` — heap alloc
4. `std::vector<int> map_C_to_Wnew(clean_str.length() + 1)` — heap alloc
5. `static std::string s_last_clean_str` — disimpan statis, heap alloc saat content berubah

Total: 4 heap allocs per invocation saat content berubah. Dimitigasi oleh early-exit cache check (L77-81), tapi saat user mengetik, ini terjadi setiap keystroke.

### `WrapGlobalBuffer` (dipanggil saat resize atau mode switch):
1. `std::string clean_str` — heap alloc
2. `std::string wrapped_str` — heap alloc

Ini bukan hot-path (hanya dipanggil saat wrap width berubah), tapi bisa dioptimasi.

## Instruksi Teknis

### Fokus: `ProcessWordWrap` saja (hot path)

### 1. Ganti `std::vector<int>` dengan stack-allocated fixed buffer + fallback:

```cpp
// Untuk buffer 64KB, mapping butuh 64K * sizeof(int) = 256KB di stack.
// Ini TERLALU BESAR untuk stack.
```

**Analisis jujur:** Buffer user `m_edit_buffer` berukuran 65536 bytes. Mapping array butuh 65536 * 4 = 262KB. Ini tidak masuk akal untuk stack allocation.

**Solusi pragmatis:** Gunakan `static` thread-local buffers yang di-resize sekali dan bertahan:

```cpp
static std::string s_clean_str;       // reuse alloc
static std::string s_wrapped_str;     // reuse alloc  
static std::vector<int> s_map_W_to_C; // reuse alloc
static std::vector<int> s_map_C_to_Wnew; // reuse alloc

// Di awal ProcessWordWrap:
s_clean_str.clear();     // clear tanpa dealloc
s_wrapped_str.clear();   
s_map_W_to_C.clear();
s_map_C_to_Wnew.clear();
// resize akan reuse capacity yang sudah ada
s_map_W_to_C.resize(data->BufTextLen + 1, 0);
```

Ini mengubah N heap allocs per-keystroke menjadi 0 heap allocs (setelah warm-up pertama). Capacity hanya naik, tidak pernah turun.

### 2. Sama untuk `s_last_clean_str` — sudah static, tidak perlu diubah.

### 3. `WrapGlobalBuffer` — TIDAK diubah. Bukan hot path.

## Peringatan
- Perhatikan bahwa `static` variables di namespace anonymous berarti satu instance per TU. Karena `formatter.cpp` adalah satu TU dan single-threaded (dipanggil dari render thread), ini aman.
- `reserve()` TIDAK diperlukan karena `resize()` sudah menangani pertumbuhan capacity.
- JANGAN gunakan `shrink_to_fit()` — kita ingin capacity tetap tinggi.

## Kriteria Selesai
- `ProcessWordWrap` tidak lagi melakukan heap alloc setelah frame pertama
- `build.ps1` pass
- Editor behavior identik
