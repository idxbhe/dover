import struct

file_path = "crosshair001.png"
def get_png_size_binary(file_path):
    try:
        with open(file_path, 'rb') as f:
            # Baca 24 byte pertama (Header PNG + Chunk IHDR)
            header = f.read(24)
            
            # Validasi apakah ini beneran file PNG yang suci
            if header[:8] != b'\x89PNG\r\n\x1a\n':
                print("❌ Bukan file PNG asli, ngab!")
                return None
                
            # Ekstrak Width dan Height (Big-Endian 4-byte Integer pada byte ke 16-24)
            width, height = struct.unpack('>II', header[16:24])
            return width, height
    except Exception as e:
        print(f"❌ Eror pembacaan file: {e}")
        return None

# Contoh Eksekusi
lebar, tinggi = get_png_size_binary(file_path)
print(f"📐 Ukuran Vektor Biner: {lebar}x{tinggi} pixel")