# Phase 05 — Eliminasi Heap Allocs di Notes Sidebar Rendering

## Target
`src/overlay/notes/layout.cpp` — fungsi `RenderSidebarInternal`

## Problem
Setiap frame, per-item sidebar, kode saat ini melakukan:
1. `std::string title = ExtractTitleFromContent(...)` → heap alloc
2. `title = title.substr(0, max_chars - 2) + ".."` → heap alloc (substr + concat)
3. `title = "* " + title` → heap alloc (concat)
4. `std::string id_str = "##note_" + std::to_string(i)` → heap alloc (to_string + concat)

Jika ada 10 catatan, ini 40+ heap allocations per frame HANYA untuk sidebar. Ini terjadi setiap frame rendering — hot path penuh.

## Instruksi Teknis

### 1. Ganti `id_str` dengan `snprintf` ke stack buffer:
```cpp
char id_buf[24];
snprintf(id_buf, sizeof(id_buf), "##note_%d", i);
```
Ganti semua penggunaan `id_str.c_str()` dengan `id_buf`.

### 2. Ganti `title` string construction dengan stack buffer:

```cpp
char title_buf[128];
// ExtractTitleFromContent perlu versi yang menulis ke buffer
// TAPI: ExtractTitleFromContent() didefinisikan di manager.cpp / layout.cpp
// dan mengembalikan std::string.
```

**Analisis `ExtractTitleFromContent`:** Fungsi ini TIDAK ditemukan di header mana pun. Cari definisinya di layout.cpp.

Jika fungsi ini melakukan operasi string berat, buat versi `ExtractTitleToBuffer(const char* content, char* out, int max_len)` yang menulis langsung ke stack buffer tanpa alokasi heap.

Jika fungsi ini ringan dan hanya memotong baris pertama, bisa diganti inline:
```cpp
// Ambil baris pertama dari content, tulis ke title_buf
const char* src = (is_sel && window->m_view_mode == 0) ? window->m_edit_buffer : notes[i].content.c_str();
int j = 0;
// Skip leading "# " jika ada
if (src[0] == '#') { while (src[j] == '#' || src[j] == ' ') j++; }
int k = 0;
while (src[j] && src[j] != '\n' && k < max_chars - 1) { title_buf[k++] = src[j++]; }
if (k >= max_chars - 1 && src[j] && src[j] != '\n') {
    title_buf[k-2] = '.'; title_buf[k-1] = '.';
}
title_buf[k] = '\0';
```

### 3. Ganti dirty prefix:
Alih-alih `title = "* " + title` (heap concat), prepend langsung di buffer:
```cpp
if (notes[i].is_dirty) {
    // Shift buffer 2 chars ke kanan
    memmove(title_buf + 2, title_buf, strlen(title_buf) + 1);
    title_buf[0] = '*'; title_buf[1] = ' ';
}
```

### 4. Cari definisi `ExtractTitleFromContent`:
Jika berada di `layout.cpp` sebagai fungsi statis, ubah signature atau buat versi buffer. Jika berada di `manager.cpp` dan diekspos via header, tambahkan versi buffer di samping versi lama (jangan hapus versi lama — bisa dipakai di non-hot-path).

## Peringatan
- `title.c_str()` digunakan pada `ImGui::GetWindowDrawList()->AddText(...)`. `AddText` menerima `const char*` — stack buffer bekerja sempurna.
- Jangan lupa null-terminate.

## Kriteria Selesai
- Zero `std::string` construction di dalam loop sidebar per-frame
- `build.ps1` pass
- Sidebar terlihat identik
