import os
import json
import glob
from xml.etree import ElementTree as ET

from fontTools.ttLib import TTFont
from fontTools.ttLib.tables._c_m_a_p import cmap_format_4
from fontTools.pens.ttGlyphPen import TTGlyphPen, GlyphCoordinates
from fontTools.pens.transformPen import TransformPen
from fontTools.svgLib.path import SVGPath
import pathops

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, '..'))

ICONS_DIR = os.path.join(SCRIPT_DIR, "icons")
TEMPLATE_TTF = os.path.join(SCRIPT_DIR, "template.ttf")
OUTPUT_TTF = os.path.join(SCRIPT_DIR, "fonts", "icons.ttf")
MAPPING_JSON_OUTPUT = os.path.join(SCRIPT_DIR, "icons.json")
CPP_HEADER_OUTPUT = os.path.join(PROJECT_ROOT, "include", "shared", "icons.h")

def format_macro_name(name):
    upper_name = name.upper().replace("-", "_")
    return f"ICON_{upper_name}"

def format_cpp_hex(codepoint):
    utf8_bytes = chr(codepoint).encode('utf-8')
    return "".join(f"\\x{b:02x}" for b in utf8_bytes)

def parse_svg_to_glyph(svg_filepath):
    tree = ET.parse(svg_filepath)
    root = tree.getroot()

    # KASTA ELIT: Amankan koordinat origin (vb_x, vb_y) agar posisi tidak loncat!
    viewbox = root.attrib.get('viewBox')
    if viewbox:
        vb_x, vb_y, vb_w, vb_h = map(float, viewbox.split())
    else:
        vb_x, vb_y = 0.0, 0.0
        vb_w = float(root.attrib.get('width', 32).replace('px', ''))
        vb_h = float(root.attrib.get('height', 32).replace('px', ''))

    path = pathops.Path()
    pen = path.getPen()
    svg_path = SVGPath(svg_filepath)
    svg_path.draw(pen)
    path.simplify()

    # TrueType parameters (Extracted from template.ttf: Ascent 960, Descent -64)
    UPEM = 1024
    TARGET_SIZE = 870 # ~85% of UPEM

    # Ambil skala pengunci aspek rasio kanvas
    scale = TARGET_SIZE / max(vb_w, vb_h)

    # Definisikan koordinat tengah target pada grid TTF font
    TARGET_CENTER_X = UPEM / 2.0  # 512.0
    # MANDOR: Set ke 370.0 (Final calibration between 350 and 380)
    TARGET_CENTER_Y = 370.0       # Center geometris vertikal (Ascent 960, Descent -64)

    # Hitung ukuran dimensi kanvas setelah di-scale
    scaled_w = vb_w * scale
    scaled_h = vb_h * scale

    # =========================================================================
    # MATRIKS AFFINE CARMACK STYLE (GABUNGAN TRANSLASI, SKALA, FLIP, DAN CENTERING)
    # x' = A*x + C*y + E
    # y' = B*x + D*y + F
    # =========================================================================
    A = scale
    B = 0
    C = 0
    D = -scale  # Balik sumbu Y untuk koordinat font

    # E dan F bertugas menormalisasi origin vb_x/y sekaligus mengunci ke tengah grid!
    E = -scale * vb_x + (TARGET_CENTER_X - scaled_w / 2.0)
    F = scale * vb_y + (TARGET_CENTER_Y + scaled_h / 2.0)

    transform = (A, B, C, D, E, F)

    # Eksekusi transformasi murni dalam satu pipa instruksi
    tt_pen = TTGlyphPen(None)
    t_pen = TransformPen(tt_pen, transform)
    path.draw(t_pen)

    glyph = tt_pen.glyph()
    glyph.recalcBounds(None)

    # Debug log untuk mandor memantau kestabilan grid
    icon_name = os.path.basename(svg_filepath)
    print(f"[GRID-LOCK] {icon_name:<30} | Origin: ({vb_x}, {vb_y}) | Size: {vb_w}x{vb_h} | Matrix applied successfully.")

    return glyph

def build_font_and_headers():
    if not os.path.exists(TEMPLATE_TTF):
        print(f"Error: Template font {TEMPLATE_TTF} missing.")
        return

    font = TTFont(TEMPLATE_TTF)
    
    glyph_order = ['.notdef']
    glyphs = {'.notdef': font['glyf']['.notdef']}
    hmtx = {'.notdef': (1024, 0)}
    
    # Clean up old cmaps
    cmap_table = font['cmap']
    cmap_table.tableVersion = 0
    cmap_table.tables = []
    
    cmap4 = cmap_format_4(4)
    cmap4.platformID = 3
    cmap4.platEncID = 1
    cmap4.language = 0
    cmap4.cmap = {}
    
    svg_files = sorted(glob.glob(os.path.join(ICONS_DIR, "*.svg")))
    
    if not svg_files:
        print(f"No SVGs found in {ICONS_DIR}")
        return
        
    codepoint = 0xf100
    mappings = []
    
    for svg_file in svg_files:
        icon_name = os.path.splitext(os.path.basename(svg_file))[0]
        glyph_name = f"icon_{icon_name}"
        
        glyph = parse_svg_to_glyph(svg_file)
        
        glyph_order.append(glyph_name)
        glyphs[glyph_name] = glyph
        lsb = int(glyph.xMin) if hasattr(glyph, "xMin") else 0
        hmtx[glyph_name] = (1024, lsb)
        
        cmap4.cmap[codepoint] = glyph_name
        
        mappings.append({
            'name': icon_name,
            'codepoint': codepoint
        })
        
        codepoint += 1
        
    cmap_table.tables.append(cmap4)
    font.setGlyphOrder(glyph_order)
    font['glyf'].glyphs = glyphs
    font['hmtx'].metrics = hmtx
    
    os.makedirs(os.path.dirname(OUTPUT_TTF), exist_ok=True)
    font.save(OUTPUT_TTF)
    print(f"Compiled TTF font with {len(svg_files)} icons to {OUTPUT_TTF}")
    
    # --- Generate JSON ---
    metadata = {}
    for m in mappings:
        metadata[m["name"]] = {
            "macro": format_macro_name(m["name"]),
            "codepoint_dec": m['codepoint'],
            "codepoint_hex": f"U+{m['codepoint']:04X}",
            "utf8_cpp": format_cpp_hex(m['codepoint'])
        }
        
    with open(MAPPING_JSON_OUTPUT, "w", encoding="utf-8") as f:
        json.dump(metadata, f, indent=4)
    print(f"Saved mapping reference to {MAPPING_JSON_OUTPUT}")
    
    # --- Generate C++ Header ---
    with open(OUTPUT_TTF, "rb") as f:
        font_bytes = f.read()
        
    lines = [
        "#pragma once",
        "#include <stdint.h>",
        "",
        "// Auto-generated Icon Macros from build_from_svg.py"
    ]
    
    min_cp = mappings[0]['codepoint']
    max_cp = mappings[-1]['codepoint']
    
    lines.append(f"#define DI_ICON_MIN 0x{min_cp:04x}")
    lines.append(f"#define DI_ICON_MAX 0x{max_cp:04x}")
    lines.append("")
    
    for m in mappings:
        macro = format_macro_name(m["name"])
        cpp_hex = format_cpp_hex(m['codepoint'])
        lines.append(f"#define {macro:<30} \"{cpp_hex}\" // {m['name']}")
        
    lines.append("")
    lines.append(f"// array size is {len(font_bytes)}")
    lines.append("static const uint8_t g_icons_data[] = {")
    
    hex_blocks = []
    for i in range(0, len(font_bytes), 16):
        chunk = font_bytes[i:i+16]
        hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
        hex_blocks.append("  " + hex_str + ",")
        
    if hex_blocks:
        hex_blocks[-1] = hex_blocks[-1].rstrip(",")
        
    lines.extend(hex_blocks)
    lines.append("};")
    
    with open(CPP_HEADER_OUTPUT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"Successfully generated {CPP_HEADER_OUTPUT}")

if __name__ == "__main__":
    build_font_and_headers()
