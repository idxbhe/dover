import os
import glob
import struct
try:
    from PIL import Image
except ImportError:
    print("[ERROR] Please install Pillow: pip install Pillow")
    exit(1)

def build_pak():
    input_dir = "crosshairs.png"
    # Output to the project root, from where it can be distributed
    output_path = os.path.join("..", "dover_assets.pak")
    
    os.makedirs(input_dir, exist_ok=True)
    
    png_files = glob.glob(os.path.join(input_dir, "*.png"))
    
    if not png_files:
        print(f"[WARNING] No PNG files found in {input_dir}. Generating dummy.")
        dummy_img = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
        from PIL import ImageDraw
        draw = ImageDraw.Draw(dummy_img)
        draw.line((64, 32, 64, 96), fill=(255, 255, 255, 255), width=4)
        draw.line((32, 64, 96, 64), fill=(255, 255, 255, 255), width=4)
        dummy_img.save(os.path.join(input_dir, "cross-1.png"))
        png_files = glob.glob(os.path.join(input_dir, "*.png"))
        
    print(f"[INFO] Found {len(png_files)} PNG files. Baking PAK...")
    
    def natural_sort_key(s):
        import re
        return [int(text) if text.isdigit() else text.lower() for text in re.split(r'(\d+)', s)]
        
    png_files.sort(key=natural_sort_key)
    
    # Pak Format:
    # Header: Magic ('DPAK', 4 bytes), Version (uint32_t), AssetCount (uint32_t)
    # TOC Entries: Name (32 bytes string, null-padded), Width (uint32_t), Height (uint32_t), DataOffset (uint64_t), DataSize (uint64_t)
    # Data: Raw RGBA bytes
    
    magic = b'DPAK'
    version = 1
    count = len(png_files)
    
    header_fmt = '<4sII'
    toc_entry_fmt = '<32sIIQQ'
    
    toc_size = count * struct.calcsize(toc_entry_fmt)
    current_data_offset = struct.calcsize(header_fmt) + toc_size
    
    toc_entries = []
    data_blocks = []
    
    for file_path in png_files:
        filename = os.path.basename(file_path)
        name, _ = os.path.splitext(filename)
        # Max 31 chars + null terminator
        name_bytes = name.encode('utf-8')[:31].ljust(32, b'\0')
        
        img = Image.open(file_path).convert("RGBA")
        # Ensure optimal scaling master resolution (e.g. 128x128) if needed, but we trust the input
        width, height = img.size
        rgba_bytes = img.tobytes()
        data_size = len(rgba_bytes)
        
        toc_entries.append(struct.pack(toc_entry_fmt, name_bytes, width, height, current_data_offset, data_size))
        data_blocks.append(rgba_bytes)
        
        current_data_offset += data_size
        
    with open(output_path, "wb") as f:
        f.write(struct.pack(header_fmt, magic, version, count))
        for toc in toc_entries:
            f.write(toc)
        for data in data_blocks:
            f.write(data)
            
    print(f"[SUCCESS] Generated {output_path} with {count} crosshairs. Size: {current_data_offset / 1024 / 1024:.2f} MB")

if __name__ == "__main__":
    build_pak()
