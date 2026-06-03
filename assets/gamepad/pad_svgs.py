import os
import glob
import subprocess
from PIL import Image

def process_svgs():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    src_dir = os.path.join(script_dir, 'unpadded_svg')
    dst_dir = os.path.join(script_dir, 'png')
    
    os.makedirs(dst_dir, exist_ok=True)
    
    svgs = glob.glob(os.path.join(src_dir, '*.svg'))
    if not svgs:
        print("[ERROR] No SVGs found in unpadded_svg")
        return
        
    for svg in svgs:
        name = os.path.basename(svg).replace('.svg', '.png')
        out_png = os.path.join(dst_dir, name)
        
        print(f"Rasterizing and padding {name}...")
        
        # 1. Rasterize SVG to PNG using Inkscape
        # Export at a true 1:1 pixel perfect resolution (48x48) to avoid runtime minification aliasing
        res = subprocess.run(['inkscape', '-w', '48', '-h', '48', svg, '-o', out_png], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        
        if not os.path.exists(out_png):
            print(f"[ERROR] Failed to rasterize {svg} using Inkscape.")
            continue
            
        # 2. Add 2px transparent padding on all sides to prevent bilinear Wrap bleeding
        img = Image.open(out_png).convert("RGBA")
        w, h = img.size
        pad_size = 2
        padded = Image.new("RGBA", (w + pad_size*2, h + pad_size*2), (0, 0, 0, 0))
        padded.paste(img, (pad_size, pad_size))
        padded.save(out_png)
        
    print("[SUCCESS] All SVGs rasterized and padded.")

if __name__ == "__main__":
    process_svgs()
