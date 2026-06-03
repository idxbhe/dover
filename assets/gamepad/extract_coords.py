import os
import glob
from PIL import Image

folder = 'button-position-reference'
files = glob.glob(f'{folder}/*.png')
files.sort()

out_cpp = "struct ButtonOffset {\n    const char* name;\n    float x_pct;\n    float y_pct;\n    float w_pct;\n    float h_pct;\n};\n\n"
out_cpp += "const ButtonOffset g_button_offsets[] = {\n"

for f in files:
    img = Image.open(f).convert("RGBA")
    w, h = img.size
    
    # Get bounding box of non-zero alpha
    bbox = img.getbbox()
    if bbox:
        # bbox is (left, upper, right, lower)
        left, upper, right, lower = bbox
        
        # Calculate center
        cx = (left + right) / 2.0
        cy = (upper + lower) / 2.0
        
        # Calculate width/height
        bw = right - left
        bh = lower - upper
        
        # Normalize
        cx_pct = cx / w
        cy_pct = cy / h
        bw_pct = bw / w
        bh_pct = bh / h
        
        name = os.path.basename(f).replace('.png', '')
        
        out_cpp += f'    {{ "{name}", {cx_pct:.4f}f, {cy_pct:.4f}f, {bw_pct:.4f}f, {bh_pct:.4f}f }},\n'
    else:
        print(f"Warning: {f} is completely transparent.")

out_cpp += "};\n"

with open("button_coordinates.h", "w") as out_file:
    out_file.write(out_cpp)

print("Generated button_coordinates.h successfully.")
print(out_cpp)
