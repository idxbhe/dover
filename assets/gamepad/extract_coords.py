import os
import glob
from PIL import Image

script_dir = os.path.dirname(os.path.abspath(__file__))
folder = os.path.join(script_dir, 'button-position-reference')
files = glob.glob(os.path.join(folder, '*.png'))
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
        # Synchronize with the transparent padding added to the final texture
        # We assume pad_svgs.py added 2px padding on all sides.
        pad_size = 2
        png_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'png', name + '.png')
        
        if os.path.exists(png_path):
            with Image.open(png_path) as icon_img:
                iw, ih = icon_img.size
                orig_iw = iw - (pad_size * 2)
                orig_ih = ih - (pad_size * 2)
                if orig_iw > 0 and orig_ih > 0:
                    # Expand the quad width/height by the exact padding ratio
                    bw_pct *= (iw / orig_iw)
                    bh_pct *= (ih / orig_ih)
                else:
                    print(f"Warning: {png_path} is too small to account for padding.")
        else:
            print(f"Warning: Corresponding {png_path} not found. Scale might be slightly off.")
        
        out_cpp += f'    {{ "{name}", {cx_pct:.4f}f, {cy_pct:.4f}f, {bw_pct:.4f}f, {bh_pct:.4f}f }},\n'
    else:
        print(f"Warning: {f} is completely transparent.")

out_cpp += "};\n"

with open("button_coordinates.h", "w") as out_file:
    out_file.write(out_cpp)

print("Generated button_coordinates.h successfully.")
print(out_cpp)
