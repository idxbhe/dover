# Dover: Steam Overlay Alternative

Dover adalah overlay universal premium berperforma tinggi dan modern yang dirancang khusus sebagai alternatif pengganti Steam Overlay. Proyek ini dibangun menggunakan **C++** untuk kinerja maksimal, serta menggunakan **Dear ImGui** untuk antarmuka pengguna yang dinamis, responsif, dan estetis.

---

## 🚀 Fitur Utama

- **Dukungan Multi-API Grafis & Arsitektur:** Mendukung game yang menggunakan **DirectX 9** dan **DirectX 11** baik dalam arsitektur **32-bit (x86)** maupun **64-bit (x64)**.
- **Antarmuka Premium Bergaya "Steam Overlay":**
  - **Bilah Navigasi Persisten:** Bilah alat di sisi paling atas layar yang persisten (tidak dapat digeser/diubah ukuran) untuk kontrol menu satu pintu.
  - **Jendela Fitur Modular:** Jendela *Notes* dan *Settings* yang independen, dapat digeser secara bebas, dan ukurannya dapat disesuaikan.
  - **Penyimpanan State Konfigurasi:** Posisi dan ukuran jendela otomatis disimpan di `%LOCALAPPDATA%\dover\imgui.ini` sehingga posisi jendela Anda tetap dipertahankan pada sesi permainan berikutnya.
- **Isolasi Input Lapis-Tiga Bebas Kebocoran (*Zero Input Leakage*):**
  - Mencegat pesan keyboard/mouse Windows standar.
  - Memblokir pesan Raw Input (`WM_INPUT`) dari hardware.
  - Mengaitkan (*hooking*) API status tombol (`GetAsyncKeyState`, `GetKeyState`, `GetKeyboardState`) dan pembebasan snap lock mouse (`ClipCursor`).
  - Menyaring koordinat polling mouse (`GetCursorPos`, `SetCursorPos`) serta event queue (`PeekMessage`, `GetMessage`) agar game tidak bergerak/merespons klik sama sekali ketika overlay aktif.

---

## 🏛️ Konsep "Satu Executable" (Unified Launcher)

Sistem operasi Windows **tidak mengizinkan** proses 64-bit untuk secara langsung memetakan memori atau memanggil thread remote di dalam proses 32-bit (lintas arsitektur). 

Untuk mengatasi keterbatasan sistem operasi ini secara elegan tanpa membebani pengguna dengan dua launcher terpisah, Dover menggunakan konsep **Launcher Satu Pintu Pintar**:

1. Pengguna hanya perlu menjalankan satu file eksekusi utama: **`dover_launcher.exe`** (aplikasi 64-bit native).
2. Launcher mendeteksi arsitektur target game secara otomatis menggunakan fungsi Windows `GetBinaryTypeW`.
3. **Jika Game Target adalah 64-bit:** `dover_launcher.exe` langsung menyuntikkan `dover_overlay64.dll` ke memori game target.
4. **Jika Game Target adalah 32-bit:** Launcher mendelegasikan tugas penyuntikan secara transparan ke helper **`dover_injector32.exe`** (aplikasi 32-bit), yang kemudian bertugas menyuntikkan `dover_overlay32.dll` ke dalam game target 32-bit.

Dengan arsitektur ini, seluruh binary rilis disatukan dalam satu folder distribusi bersih untuk kenyamanan penggunaan maksimal.

---

## 🛠️ Panduan Build Dual-Architecture secara Benar

Karena proyek ini mencakup binary dengan arsitektur target yang berbeda (32-bit dan 64-bit), proses kompilasi harus dilakukan menggunakan **dua konfigurasi build generator CMake terpisah**, lalu hasilnya disatukan ke dalam folder rilis utama.

Ikuti panduan langkah demi langkah di bawah ini menggunakan PowerShell:

### Langkah 1: Build untuk Arsitektur 64-bit (x64)
```powershell
# Buat folder build x64 dan generate proyek Visual Studio
cmake -B build_x64 -S . -A x64

# Kompilasi binary x64 dengan konfigurasi Release
cmake --build build_x64 --config Release
```

### Langkah 2: Build untuk Arsitektur 32-bit (x86)
```powershell
# Buat folder build x86 dan generate proyek Visual Studio
cmake -B build_x86 -S . -A Win32

# Kompilasi binary x86 dengan konfigurasi Release
cmake --build build_x86 --config Release
```

### Langkah 3: Satukan Seluruh Binary Rilis ke Folder Distribusi Utama
Salin helper injector 32-bit dan DLL overlay 32-bit ke dalam folder output binary 64-bit utama:
```powershell
Copy-Item build_x86\bin\Release\dover_injector32.exe build_x64\bin\Release\
Copy-Item build_x86\bin\Release\dover_overlay32.dll build_x64\bin\Release\
```

Setelah langkah ini selesai, folder **`build_x64\bin\Release\`** Anda kini siap didistribusikan secara mandiri dan utuh!

---

## 🎮 Cara Penggunaan

1. Buka folder output distribusi: `build_x64\bin\Release\`
2. Jalankan launcher utama: **`dover_launcher.exe`**
3. Ketikkan nama file executable game target yang sedang berjalan (contoh: `gta_sa.exe` atau `csgo.exe`).
4. Tekan pintasan keyboard **`Shift + Tab`** di dalam game untuk membuka/menutup overlay premium Dover Anda!