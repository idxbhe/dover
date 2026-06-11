import os
import glob

def bulk_rename_svgs():
    # 1. Cari seluruh file dengan ekstensi .svg di folder saat ini
    svg_files = glob.glob("*.svg")
    
    if not svg_files:
        print("❌ ERROR: Gak nemu file .svg sama sekali di folder ini, ngab!")
        return

    print(f"📦 Menemukan {len(svg_files)} file SVG. Memulai proses mutilasi nama... \n")
    
    # 2. Lakukan looping untuk merename secara sekuensial
    counter = 1
    for file_path in svg_files:
        # Biar script ini gak sengaja me-rename file yang udah bener formatnya
        if len(f"{counter}") == 1:
            suffix = f"00{counter}"
        elif len(f"{counter}") == 2:
            suffix = f"0{counter}"
        elif len(f"{counter}") == 3:
            suffix = f"{counter}"
        new_name = f"crosshair{suffix}.svg"
        
        # Cek jika file target sudah ada agar tidak terjadi tabrakan (override)
        if file_path == new_name:
            print(f"⏩ SKIPPED: {file_path} udah sesuai format.")
            counter += 1
            continue
            
        try:
            os.rename(file_path, new_name)
            print(f"🔄 RENAME: {file_path} -> {new_name}")
            counter += 1
        except Exception as e:
            print(f"❌ FAILED: Gagal merename {file_path}. Error: {e}")

    print(f"\n✨ Selesai, ngab! Sekarang semua file udah berwujud 'cross-[angka].svg'!")

if __name__ == "__main__":
    bulk_rename_svgs()