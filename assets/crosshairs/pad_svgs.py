import os
import glob
from bs4 import BeautifulSoup
import re

def parse_dimension(d):
    if not d:
        return 0.0
    m = re.match(r'^([-+]?[\d.]+)', str(d))
    return float(m.group(1)) if m else 0.0

def process_svgs():
    input_dir = "unpadded_crosshairs"
    output_dir = "svg"
    os.makedirs(output_dir, exist_ok=True)
    
    svg_files = glob.glob(os.path.join(input_dir, "*.svg"))
    
    if not svg_files:
        print(f"[ERROR] Tidak ada file .svg di {input_dir}")
        return
        
    print(f"[START] Memproses {len(svg_files)} file SVG dengan Bounding Constraint (Max 48px) pada kanvas 64x64... \n")
    
    success_count = 0
    for file_path in svg_files:
        filename = os.path.basename(file_path)
        try:
            with open(file_path, "r", encoding="utf-8") as f:
                content = f.read()
                
            soup = BeautifulSoup(content, 'xml')
            svg = soup.find('svg')
            
            if not svg:
                print(f"[SKIPPED] {filename} - Tidak menemukan tag <svg>")
                continue
                
            w_attr = svg.get('width', '')
            h_attr = svg.get('height', '')
            vb_attr = svg.get('viewBox', '')
            
            min_x, min_y, vb_w, vb_h = 0.0, 0.0, 0.0, 0.0
            
            if vb_attr:
                parts = [float(p) for p in vb_attr.replace(',', ' ').split() if p.strip()]
                if len(parts) >= 4:
                    min_x, min_y, vb_w, vb_h = parts[:4]
            else:
                vb_w = parse_dimension(w_attr)
                vb_h = parse_dimension(h_attr)
                
            if vb_w <= 0 or vb_h <= 0:
                print(f"[SKIPPED] {filename} - Dimensi internal 0 atau tidak valid (w:{vb_w}, h:{vb_h})")
                continue
                
            # Hitung scaling berdasarkan constraint maksimal 48px
            max_dim = max(vb_w, vb_h)
            scale = 48.0 / max_dim
            
            # Hitung titik tengah asli dari SVG
            orig_cx = min_x + (vb_w / 2.0)
            orig_cy = min_y + (vb_h / 2.0)
            
            # Hitung translasi agar `orig_cx * scale` dan `orig_cy * scale` berada tepat di (32, 32)
            tx = 32.0 - (orig_cx * scale)
            ty = 32.0 - (orig_cy * scale)
            
            # Ambil semua children dari root <svg>
            children = list(svg.contents)
            
            # Bersihkan atribut dan children root <svg>
            svg.clear()
            
            # Set root attribut agar Fixed Square Canvas 64x64
            svg['width'] = "64"
            svg['height'] = "64"
            svg['viewBox'] = "0 0 64 64"
            
            # Buat grup wrapper untuk menampung elemen lama beserta transformasinya
            g_wrapper = soup.new_tag('g')
            g_wrapper['transform'] = f"translate({tx:.4f}, {ty:.4f}) scale({scale:.4f})"
            
            # Masukkan semua child lama ke dalam wrapper
            for child in children:
                g_wrapper.append(child)
                
            svg.append(g_wrapper)
            
            out_path = os.path.join(output_dir, filename)
            with open(out_path, "w", encoding="utf-8") as f:
                f.write(str(soup))
                
            print(f"[OK] {filename} (Scale: {scale:.3f}, Translate: {tx:.2f}, {ty:.2f})")
            success_count += 1
            
        except Exception as e:
            print(f"[ERROR] memproses {filename}: {e}")

    print(f"\n[DONE] Selesai! Berhasil mem-padding {success_count} file SVG ke dalam 'crosshairs/'.")

if __name__ == "__main__":
    process_svgs()
