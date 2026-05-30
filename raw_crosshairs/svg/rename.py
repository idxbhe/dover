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
        new_name = f"cross-{counter}.svg"
        
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