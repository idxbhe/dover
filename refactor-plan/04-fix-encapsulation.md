# Phase 04 — Fix Encapsulation: Friend Static Functions

## Target
- `include/overlay/notes/layout.h`
- `src/overlay/notes/layout.cpp`
- `include/overlay/ui/components/base_window.h`

## Problem
Pada refaktor sebelumnya, field `m_sidebar_width`, `m_view_mode`, `m_zoom_idx`, `m_edit_buffer[65536]`, dll. dipindahkan dari `private` ke `public` agar fungsi statis (`RenderToolbarInternal`, `RenderSidebarInternal`, `RenderEditorInternal`, dll.) bisa mengakses mereka via `window->m_field`.

Selain itu, `float& GetBgAlpha()` ditambahkan ke `NotesWindow` sebagai workaround untuk mengakses `m_bg_alpha` yang `protected` di `BaseWindow`.

Ini merusak enkapsulasi. Field-field tersebut seharusnya TIDAK bisa diakses oleh kode sembarang.

## Instruksi Teknis

### 1. Di `layout.cpp`, temukan semua 4 fungsi statis:
```
RenderToolbarInternal
RenderSidebarInternal
RenderEditorInternal
RenderPreviewInternal
RenderFloatingButtonsInternal
```

### 2. Forward-declare mereka di `layout.h` dan jadikan `friend`:

```cpp
// Di dalam namespace dover::overlay::notes, SEBELUM class NotesWindow:
// Forward declarations for internal static renderers
static const char* RenderToolbarInternal(NotesWindow*, bool, float);
static int RenderSidebarInternal(NotesWindow*, float, float);
static void RenderEditorInternal(NotesWindow*, float, float);
static void RenderPreviewInternal(NotesWindow*, float);
enum class FloatBtnAction;
static FloatBtnAction RenderFloatingButtonsInternal(NotesWindow*);
```

PERHATIAN: Forward-declare fungsi statis tidak bekerja lintas translation unit. Karena semua fungsi statis berada di `layout.cpp` (single TU), pendekatan yang benar adalah:

**Alternatif (lebih sederhana dan benar):** Gunakan anonymous namespace free functions di `layout.cpp` dan deklarasikan mereka sebagai `friend` di header:

Di `layout.h`:
```cpp
class NotesWindow : public ui::BaseWindow {
    // ... public interface tetap ...

private:
    // Member variables kembali ke PRIVATE
    float m_sidebar_width = 240.0f;
    // ... semua field ...
    char m_edit_buffer[65536] = {};

    // Helper methods
    void SyncEditBufferFromNote(int idx);
    void FlushEditBufferToNote();
    void SwitchToEditor();

    // Grant access to internal static renderers (defined in layout.cpp)
    friend const char* detail::RenderToolbarInternal(NotesWindow*, bool, float);
    friend int detail::RenderSidebarInternal(NotesWindow*, float, float);
    friend void detail::RenderEditorInternal(NotesWindow*, float, float);
    friend void detail::RenderPreviewInternal(NotesWindow*, float);
    friend detail::FloatBtnAction detail::RenderFloatingButtonsInternal(NotesWindow*);
};
```

Di `layout.cpp`, bungkus fungsi statis dalam `namespace dover::overlay::notes::detail { ... }`:

```cpp
namespace dover::overlay::notes::detail {
    enum class FloatBtnAction { None, ToggleMode, DeleteNote };
    const char* RenderToolbarInternal(NotesWindow* w, bool, float win_w) { ... }
    // etc.
}
```

### 3. Hapus `float& GetBgAlpha()` dari `layout.h`:
Fungsi statis sekarang adalah friend, mereka bisa akses `m_bg_alpha` secara langsung dari `BaseWindow` karena `NotesWindow` inherits secara protected. 

**WAIT — `friend` dari `NotesWindow` tetap tidak bisa akses `protected` member dari `BaseWindow`.** Maka:
- Tambahkan `friend` declaration juga di `BaseWindow` untuk `detail::RenderToolbarInternal` dan `detail::RenderSidebarInternal` (yang butuh `m_bg_alpha`).
- ATAU (lebih simpel): biarkan `GetBgAlpha()` di `BaseWindow` sebagai public accessor tapi kembalikan field-field NotesWindow ke private. Ini trade-off yang lebih masuk akal.

### Keputusan Final:
- `NotesWindow` fields: kembali ke `private`, friend-declare fungsi detail.
- `BaseWindow::GetBgAlpha()`: TETAP sebagai public read accessor (sudah ada `SetBgAlpha()`). Hapus return-by-reference, ubah ke return-by-value + setter pattern yang sudah ada.
- Di `layout.cpp`: ganti `window->GetBgAlpha()` menjadi akses via `window->m_bg_alpha` (karena friend) — TIDAK BISA karena m_bg_alpha milik BaseWindow dan protected. Maka tetap gunakan `GetBgAlpha()` tapi kembalikan public accessor.

### Resolusi Akhir (Pragmatis):
1. Kembalikan member `NotesWindow` dari `public` ke `private`.
2. Tambahkan `friend` declarations untuk 5 fungsi detail.
3. `m_bg_alpha` tetap diakses via `GetBgAlpha()` yang sudah ada di `BaseWindow`.
4. Hapus `float& GetBgAlpha()` dari `layout.h` — gunakan `float GetBgAlpha() const` yang sudah ada di base + `SetBgAlpha()`.
5. Ganti semua `&window->GetBgAlpha()` (reference) di slider menjadi pattern: baca ke local, slider modify local, set back via `SetBgAlpha()`.

## Kriteria Selesai
- Field `NotesWindow` kembali ke `private`
- Tidak ada `public` hack
- `build.ps1` pass
