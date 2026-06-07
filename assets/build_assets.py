import os
import glob
import struct
try:
    from PIL import Image
except ImportError:
    print("[ERROR] Please install Pillow: pip install Pillow")
    exit(1)

def build_pak():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    
    # Define asset categories
    categories = {
        "crosshairs": os.path.join(script_dir, "crosshairs", "png"),
        "gamepad": os.path.join(script_dir, "gamepad", "png")
    }
    
    # Target all build folders to ensure assets are always present
    output_paths = [
        os.path.join(project_root, "out", "bin", "Release", "assets.pak"),
        os.path.join(project_root, "out", "bin", "Debug", "assets.pak")
    ]
    
    for p in output_paths:
        os.makedirs(os.path.dirname(p), exist_ok=True)
    
    asset_files = []
    for cat_name, input_dir in categories.items():
        if os.path.exists(input_dir):
            pngs = glob.glob(os.path.join(input_dir, "*.png"))
            for p in pngs:
                asset_files.append((cat_name, p))
        else:
            print(f"[WARNING] Directory not found: {input_dir}")
            
    print(f"[INFO] Found {len(asset_files)} total PNG files. Baking PAK...")
    
    def natural_sort_key(item):
        import re
        s = item[1]
        return [int(text) if text.isdigit() else text.lower() for text in re.split(r'(\d+)', s)]
        
    asset_files.sort(key=natural_sort_key)
    
    # Pak Format:
    # Header: Magic ('DPAK', 4 bytes), Version (uint32_t), AssetCount (uint32_t)
    # TOC Entries: Name (32 bytes string, null-padded), Width (uint32_t), Height (uint32_t), DataOffset (uint64_t), DataSize (uint64_t)
    # Data: Raw RGBA bytes
    
    magic = b'DPAK'
    version = 1
    count = len(asset_files)
    
    header_fmt = '<4sII'
    toc_entry_fmt = '<32sIIQQ'
    
    toc_size = count * struct.calcsize(toc_entry_fmt)
    current_data_offset = struct.calcsize(header_fmt) + toc_size
    
    toc_entries = []
    data_blocks = []
    
    for cat_name, file_path in asset_files:
        filename = os.path.basename(file_path)
        name, _ = os.path.splitext(filename)
        
        # We prefix the name with category, e.g. "gamepad/btn_a"
        full_asset_name = f"{cat_name}/{name}"
        
        # Max 31 chars + null terminator
        name_bytes = full_asset_name.encode('utf-8')[:31].ljust(32, b'\0')
        
        img = Image.open(file_path).convert("RGBA")
        width, height = img.size
        rgba_bytes = img.tobytes()
        data_size = len(rgba_bytes)
        
        toc_entries.append(struct.pack(toc_entry_fmt, name_bytes, width, height, current_data_offset, data_size))
        data_blocks.append(rgba_bytes)
        
        current_data_offset += data_size
        
    for output_path in output_paths:
        with open(output_path, "wb") as f:
            f.write(struct.pack(header_fmt, magic, version, count))
            for toc in toc_entries:
                f.write(toc)
            for data in data_blocks:
                f.write(data)
        print(f"[SUCCESS] Generated {output_path} with {count} assets. Size: {current_data_offset / 1024 / 1024:.2f} MB")

if __name__ == "__main__":
    build_pak()
