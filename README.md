# Dover

A lightweight, high-performance in-game overlay for DirectX 9, DirectX 11, and DirectX 12 games. Built with C++ and Dear ImGui.

---

## Features

*   **Universal Support:** Highly optimized x86 and x64 core for DX9/DX11/DX12 (DX12 is x64 only) with zero-overhead memory mapping.
*   **Stability First:** Safe VTable unhooking helper prevents crashes during shutdown or DLL detachment by avoiding dangling pointers.
*   **Advanced Injection:** Toggle between Pure VTable and Inline Hook (MinHook) methods via advanced settings.
*   **In-Game Notes:** Markdown-supported notes with pinning capability and interactive task lists.
*   **Custom Crosshair:** Load custom reticles from `assets.pak` with scaling and positioning controls.
*   **Controller Tool:** Map controller inputs (including LT/RT triggers) to keyboard keys with modifiers and visualize inputs in real-time.
*   **Gamepad HUD Visualizer:** Premium in-game real-time controller layout HUD visualizer.
*   **Opacity Control:** Adjustable window and background transparency.
*   **Auto-Save Layout:** Automatically saves window positions and settings.
*   **Input Blocking:** Prevents overlay inputs from leaking into the game.

---

## How to Use

1. Extract the release package. Ensure `launcher.exe`, the DLLs, injectors, and `assets.pak` are in the same directory.
2. Run `launcher.exe` to open the graphical launcher.
3. Use the "+ ADD NEW GAME" button to add your game's executable. The launcher will automatically minimize to the system tray while the game is running.
4. Once in-game, press **`Shift + Tab`** to toggle the overlay.

*(Alternatively, you can launch a game directly via command line: `launcher.exe "C:\Path\To\Game.exe"`)*

---

## Build Guide

Compiled using modern MSVC on Windows.

### Requirements
*   Windows 10/11
*   CMake (3.20+)
*   MSVC 2026 (C++23)

### Compiling
Run the optimized build script to compile, consolidate, and package both x64 and x86 targets:

```powershell
.\build.ps1
```

The final compiled and packaged binaries will be available in the `out/bin/Release` (or `Debug`) directory.
