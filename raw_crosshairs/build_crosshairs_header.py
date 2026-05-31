import os
import glob
try:
    from PIL import Image
except ImportError:
    print("[ERROR] Please install Pillow: pip install Pillow")
    exit(1)

def build_header():
    input_dir = "crosshairs.png"
    output_path = os.path.join("..", "include", "overlay", "assets", "crosshairs.h")
    
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    os.makedirs(input_dir, exist_ok=True)
    
    png_files = glob.glob(os.path.join(input_dir, "*.png"))
    
    if not png_files:
        print(f"[WARNING] No PNG files found in {input_dir}. Generating a dummy one for testing...")
        dummy_img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
        # Draw a simple cross
        from PIL import ImageDraw
        draw = ImageDraw.Draw(dummy_img)
        draw.line((32, 16, 32, 48), fill=(255, 255, 255, 255), width=2)
        draw.line((16, 32, 48, 32), fill=(255, 255, 255, 255), width=2)
        dummy_img.save(os.path.join(input_dir, "cross-1.png"))
        png_files = glob.glob(os.path.join(input_dir, "*.png"))
        
    print(f"[INFO] Found {len(png_files)} PNG files. Baking RGBA header...")
    
    def natural_sort_key(s):
        import re
        return [int(text) if text.isdigit() else text.lower() for text in re.split(r'(\d+)', s)]
        
    png_files.sort(key=natural_sort_key)
    
    header_lines = []
    header_lines.append("#pragma once")
    header_lines.append("#include <cstdint>")
    header_lines.append("#include <cstddef>")
    header_lines.append("")
    header_lines.append("namespace dover::overlay::assets {")
    header_lines.append("")
    header_lines.append("struct CrosshairData {")
    header_lines.append("    const char* name;")
    header_lines.append("    int width;")
    header_lines.append("    int height;")
    header_lines.append("    const uint8_t* rgba_data;")
    header_lines.append("    size_t data_size;")
    header_lines.append("    void* texture_id = nullptr; // For ImGui::Image")
    header_lines.append("};")
    header_lines.append("")
    
    array_entries = []
    
    for file_path in png_files:
        filename = os.path.basename(file_path)
        name, _ = os.path.splitext(filename)
        var_name = name.replace("-", "_")
        
        img = Image.open(file_path).convert("RGBA")
        width, height = img.size
        rgba_bytes = img.tobytes()
        
        hex_data = ", ".join([f"0x{b:02x}" for b in rgba_bytes])
        
        header_lines.append(f"inline constexpr uint8_t raw_{var_name}[] = {{ {hex_data} }};")
        array_entries.append(f'    {{ "{name}", {width}, {height}, raw_{var_name}, sizeof(raw_{var_name}), nullptr }},')
        
    header_lines.append("")
    # Mutable array because we need to write texture_id at runtime
    header_lines.append("inline CrosshairData g_crosshairs[] = {")
    header_lines.extend(array_entries)
    header_lines.append("};")
    header_lines.append("")
    header_lines.append("inline constexpr size_t g_crosshairs_count = sizeof(g_crosshairs) / sizeof(g_crosshairs[0]);")
    header_lines.append("")
    header_lines.append("} // namespace dover::overlay::assets")
    
    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(header_lines) + "\n")
        
    print(f"[SUCCESS] Generated {output_path} with {len(png_files)} crosshairs.")

if __name__ == "__main__":
    build_header()
