# Dover

A lightweight, high-performance in-game overlay for DirectX 9 and DirectX 11 games. Built with C++ and Dear ImGui.

---

## Features

*   **Universal Support:** Works on x86 and x64 architectures for DX9/DX11.
*   **In-Game Notes:** Markdown-supported notes with pinning capability.
*   **Custom Crosshair:** Load custom reticles from `assets.pak` with scaling and positioning controls.
*   **Opacity Control:** Adjustable window and background transparency.
*   **Auto-Save Layout:** Automatically saves window positions and settings.
*   **Input Blocking:** Prevents overlay inputs from leaking into the game.

---

## How to Use

1. Extract the release package. Ensure `launcher.exe`, the DLLs, injectors, and `assets.pak` are in the same directory.
2. Run `launcher.exe`.
3. Use the interactive menu to add your game's absolute executable path, or select a previously saved game to launch it.
4. Once in-game, press **`Shift + Tab`** to toggle the overlay.

*(Alternatively, you can launch a game directly via command line: `launcher.exe "C:\Path\To\Game.exe"`)*

---

## Build Guide

Compiled using MSVC on Windows.

### Requirements
*   Windows 10/11
*   CMake (3.20+)
*   Visual Studio (C++ Desktop workload)

### Compiling
Run the provided build script to compile and consolidate both x64 and x86 targets automatically:

```powershell
.\build.ps1
```

The final compiled binaries will be available in the `build_x64\bin\Debug` (or `Release`) directory.