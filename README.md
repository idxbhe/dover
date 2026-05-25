# Dover: Universal In-Game Overlay

Dover is a high-performance in-game overlay alternative to the Steam Overlay. It is written in C++ and uses Dear ImGui for a lightweight, responsive interface.

---

## Features

*   **Universal Compatibility:** Supports DirectX 9 and DirectX 11 games on both 32-bit (x86) and 64-bit (x64) architectures.
*   **In-Game Notes:** Create, edit, and view text notes directly over the game. Supports Markdown rendering, dynamic text sizes, and a pin feature to keep notes visible during gameplay.
*   **Global Opacity Control:** Adjust the transparency of all floating windows and configure the background dimming level of the overlay.
*   **Auto-Save Layout:** Automatically saves window positions, sizes, and setting configurations to `%LOCALAPPDATA%\dover\imgui.ini`.
*   **Zero Input Leakage:** Safely intercepts and blocks all game-bound keyboard and mouse inputs when the overlay is active.

---

## How to Use

1.  Place all release files in the same directory:
    *   `dover_launcher.exe`
    *   `dover_injector32.exe`
    *   `dover_overlay64.dll`
    *   `dover_overlay32.dll`
2.  Run `dover_launcher.exe`.
3.  Enter the process name of the active game (e.g., `csgo.exe` or `gta_sa.exe`) and press Enter.
4.  Press **`Shift + Tab`** inside the game to toggle the overlay.

---

## Build Guide

Dover uses a dual-architecture build pipeline to compile helper tools and DLLs for both x86 and x64 processes.

### Requirements
*   Windows 10/11
*   CMake (Version 3.20 or newer)
*   Visual Studio (with C++ Desktop workload)

### Compiling
Run the automated build script to compile and package both architectures:

```powershell
.\build.ps1
```

#### Manual Compilation Steps

1.  **Build 64-bit Target (x64):**
    ```powershell
    cmake -B build_x64 -S . -A x64
    cmake --build build_x64 --config Release
    ```

2.  **Build 32-bit Target (x86):**
    ```powershell
    cmake -B build_x86 -S . -A Win32
    cmake --build build_x86 --config Release
    ```

3.  **Consolidate Binaries:**
    Copy the x86 injector helper and overlay DLL into the main x64 release folder:
    ```powershell
    Copy-Item build_x86\bin\Release\dover_injector32.exe build_x64\bin\Release\
    Copy-Item build_x86\bin\Release\dover_overlay32.dll build_x64\bin\Release\
    ```

The final distribution package will be located at `build_x64\bin\Release\`.