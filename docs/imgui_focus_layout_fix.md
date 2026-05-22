# Dokumentasi Teknis: Resolusi Bug Fokus & Pergeseran Layout Jendela ImGui

Dokumentasi ini menjelaskan solusi mendalam untuk dua masalah kompleks pada antarmuka ImGui yang terjadi pada modul **Notes Dover Overlay**:
1. **Pergeseran Jendela ke Kiri saat Tombol Format Ditekan** (Horizontal Layout Jitter).
2. **Format Teks Tidak Terbaca Secara Langsung / Non-Live** (Input Text Activation Frame Callback Discard).

Dua masalah ini merupakan kasus batas (*edge cases*) yang bersumber langsung dari arsitektur internal ImGui, sehingga solusinya sulit ditemukan pada dokumentasi standar.

---

## Masalah 1: Pergeseran Layout Jendela (Horizontal Layout Jitter)

### Gejala Masalah
Saat pengguna menekan tombol formatting di toolbar, jendela Notes tampak menyentak atau bergeser beberapa piksel ke arah kiri secara horizontal.

### Analisis Akar Masalah (Root Cause)
Masalah ini bersumber dari interaksi antara **perhitungan lebar tata letak (layout layouting)** dengan **mekanisme scroll otomatis ImGui** (`ScrollToBringRectIntoView`).

1. **Perhitungan Lebar Awal**:
   Lebar panel konten utama (`cont_w`) dihitung secara matematis menggunakan rumus:
   $$\text{cont\_w} = \text{win\_w} - \text{sb\_w} - \text{split\_w}$$
2. **Margin Tersembunyi pada SameLine**:
   Secara bawaan (*default*), setiap kali memanggil `ImGui::SameLine()`, ImGui menyuntikkan jarak horizontal ekstra (*ItemSpacing.x* sekitar 4px hingga 8px tergantung tema) untuk memisahkan komponen di baris yang sama.
3. **Akumulasi Padding**:
   Karena terdapat pemanggilan `SameLine()` secara berturut-turut di antara Sidebar, Splitter, dan Editor, lebar fisik total yang dirender menjadi:
   $$\text{Lebar Total} = \text{sb\_w} + \text{ItemSpacing.x} + \text{split\_w} + \text{ItemSpacing.x} + \text{cont\_w}$$
   Ini menyebabkan total konten yang dirender melebihi lebar jendela utama sebesar $2 \times \text{ItemSpacing.x}$.
4. **Scrolling Paksa akibat Fokus**:
   Ketika tombol format diklik dan memicu `ImGui::SetKeyboardFocusHere()`, ImGui mencoba mencari elemen Editor. Navigasi ImGui mendeteksi bahwa ujung kanan Editor berada sedikit di luar batas pandang layar karena luapan *padding* tadi.
   Secara otomatis, ImGui memanggil *programmatic scroll* ke kanan untuk menampilkan area yang "terpotong" tersebut, menyebabkan seluruh isi jendela tampak tergeser ke arah kiri.

### Logika Resolusi
Solusinya adalah menghilangkan jarak bawaan (*ItemSpacing*) secara eksplisit di sekitar elemen tata letak utama dengan memaksa lebar pixel-perfect menggunakan `SameLine(0.0f, 0.0f)`:

```cpp
// Menghilangkan jarak tak terlihat di sekeliling pembatas visual
ImGui::EndChild();
ImGui::SameLine(0.0f, 0.0f); // Spacing 0
ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.14f, 0.18f, 1.0f));
ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.25f, 0.30f, 1.00f));
ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.20f, 0.25f, 1.00f));
ImGui::Button("##spl", ImVec2(split_w, win_h));
// ...
ImGui::PopStyleColor(3);
ImGui::SameLine(0.0f, 0.0f); // Spacing 0
```
Dengan cara ini, total lebar dari ketiga elemen tata letak secara tepat sama dengan `win_w`, mencegah terjadinya luapan piksel dan menghentikan efek *scrolling* paksa sepenuhnya.

---

## Masalah 2: Format Teks Tidak "Live" (Activation Frame Buffer Override)

### Gejala Masalah
Saat menekan tombol B (Bold) atau I (Italic) di toolbar, format tidak langsung muncul pada layar. Pengguna dipaksa untuk mengeklik kembali area editor secara manual agar teks yang terformat terlihat.

### Analisis Akar Masalah (Root Cause)
Masalah ini merupakan perilaku bawaan (*quirk*) dari komponen `ImGui::InputTextMultiline` ketika kehilangan dan mendapatkan fokus dalam urutan frame yang cepat:

```
[Klik Tombol Format] ──> [Editor Kehilangan Fokus] ──> [Simpan Buffer Teks] 
                                                                 │
[Hasil Format Terlihat] <── [Klik Manual Editor] <── [Batal Render Frame Pertama]
```

1. **Focus Stealing**:
   Saat mengeklik tombol format pada Toolbar (yang merupakan bagian dari *Parent Window*), status aktif pada sub-jendela Editor (*Child Window* "NoteContent") langsung dicabut. Editor menulis buffer internalnya kembali ke variabel string global `g_edit_buffer`.
2. **Prosedur Aktivasi Frame Pertama (Activation Frame)**:
   Saat tombol dilepas dan menginstruksikan `SetKeyboardFocusHere()`, Editor masuk ke *Frame Aktivasi* (frame transisi dari tidak aktif menjadi aktif).
   *   Dalam arsitektur ImGui, pada frame pertama komponen `InputText` diaktifkan, ia menyalin isi dari string mentah `g_edit_buffer` ke dalam struktur memori internal (`stb_textedit`).
   *   Meskipun fungsi callback (`FormatCallback` dengan bendera `CallbackAlways`) dipanggil dan berhasil memproses penyisipan karakter menggunakan `data->InsertChars()`, **perubahan tersebut dibuang/ditimpa oleh siklus inisialisasi internal ImGui** di akhir frame pertama tersebut.
3. **Penyebab Delay**:
   Karena perubahan dibuang pada frame aktivasi, format tidak tampil. Baru ketika pengguna mengeklik kembali editor secara manual, editor berada dalam status "Sudah Aktif" (bukan lagi frame aktivasi) sehingga modifikasi teks akhirnya dapat diterima dan digambar ke layar.

### Logika Resolusi
Solusi tercerdas untuk menghindari bug inisialisasi internal ImGui ini adalah dengan **memodifikasi isi teks secara langsung di luar siklus callback ImGui** pada saat tombol diklik:

1. **Manipulasi Buffer Langsung**:
   Begitu tombol format ditekan, sistem langsung mengambil `g_edit_buffer`, mendeteksi batas koordinat teks terpilih (`g_saved_selection_start` & `g_saved_selection_end`), dan menyisipkan simbol Markdown secara langsung menggunakan manipulasi string standar C++ (`std::string::insert`).
2. **Kalkulasi Ulang Koordinat Kursor**:
   Posisi kursor dan indeks seleksi yang baru dihitung secara matematis di memori sebelum editor menggambarnya kembali.
3. **Aktivasi Aman**:
   Setelah buffer `g_edit_buffer` diubah di memori, `SetKeyboardFocusHere()` dipanggil. Saat Editor aktif pada frame berikutnya, ia langsung memuat data yang **sudah dalam kondisi terformat**. Masalah pengabaian modifikasi teks pada frame aktivasi berhasil dihindari sepenuhnya.

---

## Kesimpulan & Praktik Terbaik
*   Selalu gunakan `ImGui::SameLine(0.0f, 0.0f)` ketika membagi jendela menjadi kolom-kolom persentase/piksel yang tepat untuk mencegah scroll otomatis yang tidak diinginkan.
*   Hindari memanipulasi buffer teks `InputText` menggunakan `InsertChars()` di dalam callback antarmuka ketika widget tersebut baru saja diaktifkan secara programatis pada frame yang sama. Lakukan modifikasi langsung pada variabel penampung string global sebelum memanggil instruksi fokus.
