# Dover

A lightweight, high-performance in-game overlay for DirectX 9, DirectX 11, and DirectX 12 games. Built with C++ and Dear ImGui.

---

## Features

*   **Universal Support:** Works on x86 and x64 architectures for DX9/DX11/DX12 (DX12 is x64 only).
*   **In-Game Notes:** Markdown-supported notes with pinning capability.
*   **Custom Crosshair:** Load custom reticles from `assets.pak` with scaling and positioning controls.
*   **Controller Remapping:** Map controller inputs (including LT/RT triggers) to keyboard keys with modifiers.
*   **Gamepad HUD Visualizer:** Premium in-game real-time controller layout HUD visualizer.
*   **Opacity Control:** Adjustable window and background transparency.
*   **Auto-Save Layout:** Automatically saves window positions and settings.
*   **Input Blocking:** Prevents overlay inputs from leaking into the game.

---

## How to Use

1. Extract the release package. Ensure `launcher.exe`, the DLLs, injectors, and `assets.pak` are in the same directory.
2. Run `launcher.exe` to open the graphical launcher.
3. Use the "Browse..." button or manually enter your game's absolute executable path to add it, or click a previously saved game to launch it. The launcher will automatically minimize to the system tray while the game is running.
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