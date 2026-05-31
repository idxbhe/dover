import os
import sys
import glob
import cairosvg

def main():
    # 1. Parsing argumen dari terminal (default 128 kalau si Budi lupa ngisi)
    target_size = 128
    
    if len(sys.argv) > 1:
        try:
            target_size = int(sys.argv[1])
        except ValueError:
            print("❌ Woi ngab! Argumennya harus angka. Contoh: py makepng.py 512")
            sys.exit(1)
            
    print(f"🎯 Target Resolusi: {target_size}x{target_size} px (Kualitas Setajam Silet!)")

    input_dir = "./svg"
    output_dir = "./png"

    # 2. Auto-make folder PNG kalo belum ada (Logistik Anti-Ribet)
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"📁 Folder '{output_dir}/' berhasil dibuat.")

    # 3. Cek folder SVG
    if not os.path.exists(input_dir):
        print(f"❌ Folder '{input_dir}/' kagak ada! Bikin dulu dan taruh file SVG lu di situ, ngab.")
        sys.exit(1)

    # 4. Ambil semua file .svg di dalam folder ./svg/
    svg_files = glob.glob(os.path.join(input_dir, "*.svg"))
    
    if not svg_files:
        print(f"⚠️ Kuli gak nemu file .svg satupun di '{input_dir}/'. Libur dulu kita.")
        sys.exit(0)

    print(f"📦 Ditemukan {len(svg_files)} file SVG. Mesin baker mulai memanaskan kanvas...\n")

    # 5. Eksekusi Massal (Baking ke Raster Murni)
    for svg_path in svg_files:
        filename = os.path.basename(svg_path)
        name_only = os.path.splitext(filename)[0]
        png_path = os.path.join(output_dir, f"{name_only}.png")
        
        try:
            # Ini kasta tertingginya: merender rumus SVG langsung ke target ukuran!
            # Zero scaling artifacts, murni tajam!
            cairosvg.svg2png(
                url=svg_path, 
                write_to=png_path, 
                output_width=target_size, 
                output_height=target_size
            )
            print(f"✅ Baked: {filename}  ──>  {name_only}.png ({target_size}x{target_size})")
        except Exception as e:
            print(f"❌ Gagal render {filename}. Error: {e}")

    print("\n🏁 Operasi Baking Selesai, Mandor! Aset suci siap diconvert jadi RGBA binary .pak!")

if __name__ == "__main__":
    main()