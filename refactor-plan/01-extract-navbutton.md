# Phase 01 — Extract Duplicated Nav-Bar Button Rendering

## Target
`src/overlay/overlay_ui.cpp`

## Problem
Dalam `RenderImGuiUI()` terdapat dua blok rendering tombol navigasi (Notes L187-252 dan Settings L256-323) yang **90%+ identik**. Keduanya menggambar:
1. Premium gradient box background (4 warna identik)
2. Rounded border
3. Double shadow text
4. Main icon text
5. Active/focused indicator (line atau dot)
6. ImGui::Button + tooltip

Perbedaan hanya pada:
- Ikon glyph (`ICON_PANEL_NOTES` vs `ICON_PANEL_SETTINGS`)
- Warna aksen aktif (biru vs ungu)  
- Target window (`NotesWindow` vs `SettingsWindow`)
- Window name string

Duplikasi ini membuang cache L1 instruksi dan menambah binary bloat tanpa alasan.

## Instruksi Teknis

### 1. Buat fungsi statis di atas `RenderImGuiUI()`:

```cpp
struct NavButtonState {
    bool is_open;
    bool is_focused;
};

static void RenderNavButton(
    const char* icon,
    const char* tooltip,
    const NavButtonState& state,
    const ImVec4& active_shadow_color,   // e.g. blue for notes, purple for settings
    const ImVec4& active_border_color,
    float icon_btn_width,
    float icon_box_height
)
```

Fungsi ini mengambil SEMUA kode rendering dari blok Notes button (L187-243), mengganti hardcoded icon/color/state dengan parameter.

### 2. Ganti kedua blok tombol:

**Notes button (sebelum):**
```cpp
// 60+ lines of rendering code
```
**Notes button (sesudah):**
```cpp
NavButtonState notes_state{show_notes, show_notes && notes::GetNotesWindow().IsFocused()};
RenderNavButton(
    ICON_PANEL_NOTES, "Notes", notes_state,
    ImVec4(0.00f, 0.45f, 0.85f, 0.85f),  // active shadow
    ImVec4(0.20f, 0.65f, 1.00f, 0.85f),  // active border
    icon_btn_width, icon_box_height
);
if (ImGui::Button(ICON_PANEL_NOTES, ImVec2(icon_btn_width, icon_box_height))) {
    // existing click handler
}
```

Sama untuk Settings button.

### 3. Jangan sentuh:
- Close button rendering (visual berbeda, bukan duplikat)
- Logic flow `if (g_show_overlay)` dan window ordering
- Apapun di luar `overlay_ui.cpp`

## Kriteria Selesai
- `build.ps1` lulus tanpa error (x64 + x86, `/WX`)
- UI navigasi terlihat 100% identik sebelum dan sesudah refactor
- Tidak ada perubahan di file lain
