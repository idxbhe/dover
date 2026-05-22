import os
import json

# Paths relative to the script's directory (which is now raw_icons/)
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, '..'))

ICOMOON_JSON_PATH = os.path.join(SCRIPT_DIR, "dover.icomoon.json")
FONT_TTF_PATH = os.path.join(SCRIPT_DIR, "fonts", "icons.ttf")
FONT_OTF_PATH = os.path.join(SCRIPT_DIR, "fonts", "icons.otf")

MAPPING_JSON_OUTPUT = os.path.join(SCRIPT_DIR, "icons_mapping.json")
CPP_HEADER_OUTPUT = os.path.join(PROJECT_ROOT, "include", "overlay", "icons.h")

def format_macro_name(name):
    # e.g., 'window-close' -> 'ICON_WINDOW_CLOSE'
    upper_name = name.upper().replace("-", "_")
    return f"ICON_{upper_name}"

def format_cpp_hex(codepoint):
    # Convert codepoint to UTF-8 byte string in hex format
    utf8_bytes = chr(codepoint).encode('utf-8')
    return "".join(f"\\x{b:02x}" for b in utf8_bytes)

def main():
    if not os.path.exists(ICOMOON_JSON_PATH):
        print(f"Error: Could not find {ICOMOON_JSON_PATH}")
        return

    # 1. Parse icomoon json for mappings
    with open(ICOMOON_JSON_PATH, "r", encoding="utf-8") as f:
        data = json.load(f)

    mappings = []
    if "icons" in data:
        for item in data["icons"]:
            props = item.get("properties", {})
            name = props.get("name")
            codepoint = props.get("code")
            if name and codepoint is not None:
                mappings.append({"name": name, "codepoint": codepoint})
    elif "glyphs" in data:
        for item in data["glyphs"]:
            extras = item.get("extras", {})
            name = extras.get("name")
            codepoint = extras.get("codePoint")
            if name and codepoint is not None:
                mappings.append({"name": name, "codepoint": codepoint})

    if not mappings:
        print("Error: No icons mapped. Please check dover.icomoon.json structure.")
        return

    # 2. Write metadata JSON
    metadata = {}
    for m in mappings:
        hex_code = f"U+{m['codepoint']:04X}"
        macro_name = format_macro_name(m["name"])
        metadata[m["name"]] = {
            "macro": macro_name,
            "codepoint_dec": m['codepoint'],
            "codepoint_hex": hex_code,
            "utf8_cpp": format_cpp_hex(m['codepoint'])
        }

    with open(MAPPING_JSON_OUTPUT, "w", encoding="utf-8") as f:
        json.dump(metadata, f, indent=4)
    print(f"Saved metadata to {MAPPING_JSON_OUTPUT}")

    # 3. Read Font binary (check .ttf then .otf)
    font_path = FONT_TTF_PATH if os.path.exists(FONT_TTF_PATH) else FONT_OTF_PATH
    if not os.path.exists(font_path):
        print(f"Error: Could not find font binary at {font_path}")
        return

    with open(font_path, "rb") as f:
        font_bytes = f.read()

    # 4. Generate C++ Header
    lines = []
    lines.append("#pragma once")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("// Auto-generated Icon Macros from dover.icomoon.json")
    
    # Sort macros alphabetically for cleanliness
    for m in sorted(mappings, key=lambda x: x["name"]):
        macro_name = format_macro_name(m["name"])
        cpp_hex = format_cpp_hex(m['codepoint'])
        lines.append(f"#define {macro_name:<30} \"{cpp_hex}\" // {m['name']}")

    lines.append("")
    lines.append(f"// array size is {len(font_bytes)}")
    lines.append("static const uint8_t g_icons_data[] = {")

    # Format bytes in blocks of 16
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
    main()
