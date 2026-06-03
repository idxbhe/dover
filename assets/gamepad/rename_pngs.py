import os

mapping = {
    "A.png": "btn_a.png",
    "B.png": "btn_b.png",
    "X.png": "btn_x.png",
    "Y.png": "btn_y.png",
    "Up.png": "btn_dpad_up.png",
    "Bottom.png": "btn_dpad_down.png",
    "Left.png": "btn_dpad_left.png",
    "Right.png": "btn_dpad_right.png",
    "LB.png": "btn_lb.png",
    "RB.png": "btn_rb.png",
    "LT.png": "btn_lt.png",
    "RT.png": "btn_rt.png",
    "L-Thumb.png": "btn_lthumb.png",
    "R-Thumb.png": "btn_rthumb.png",
    "L-Stick.png": "btn_lstick.png",
    "R-Stick.png": "btn_rstick.png",
    "Menu.png": "btn_menu.png",
    "View.png": "btn_view.png",
    "Guide.png": "btn_guide.png",
    "Share.png": "btn_share.png",
    "Chassis.png": "chassis.png"
}

def rename_assets():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    png_dir = os.path.join(script_dir, "png")

    if not os.path.exists(png_dir):
        print(f"[ERROR] PNG directory not found at: {png_dir}")
        return

    renamed_count = 0
    for src, dst in mapping.items():
        src_path = os.path.join(png_dir, src)
        dst_path = os.path.join(png_dir, dst)
        
        if os.path.exists(src_path):
            try:
                # If target already exists and isn't the same file, remove it first
                if os.path.exists(dst_path) and src_path.lower() != dst_path.lower():
                    os.remove(dst_path)
                os.rename(src_path, dst_path)
                print(f"[SUCCESS] Renamed: {src} -> {dst}")
                renamed_count += 1
            except Exception as e:
                print(f"[ERROR] Failed to rename {src}: {e}")
        else:
            # Check if it was already renamed to avoid false warnings
            if os.path.exists(dst_path):
                pass
            else:
                print(f"[INFO] File not found (skipping): {src}")

    print(f"[INFO] Renaming complete. Total renamed: {renamed_count} files.")

if __name__ == "__main__":
    rename_assets()
