# Pre-Commit Optimization Plan: Input & Crosshair Visualizer

## 1. Analysis of Uncommitted Changes vs @AGENTS.md Principles

I have objectively analyzed the uncommitted changes and found **TWO CRITICAL VIOLATIONS** of **Rule 1: PERFORMANCE DRIVEN CODING (brutal runtime efficiency, zero overhead, honoring CPU cache).**

### Violation A: O(N^2) String Comparison on the Hot Rendering Loop
**Location:** `crosshair_window.cpp`
**Issue:** 
The rendering grid for the crosshair menu currently uses a loop like this:
`for (int i = 0; i < total_crosshairs; ++i) { auto* asset = GetCrosshairAsset(i); ... }`
Inside `GetCrosshairAsset(i)`, it loops from the start of the `assets` vector and performs `std::string::rfind("gamepad/", 0)` on every asset until it reaches the index.
This results in **O(N^2) string operations every single frame** just to render the grid. 
**Verdict:** FAILS Rule 1. This is a severe waste of CPU cycles on the rendering thread. 

### Violation B: XInput Disconnected Polling Penalty
**Location:** `input_window.cpp` (`RenderGamepadOverlay`)
**Issue:** 
To draw the gamepad HUD, the code currently runs:
`for (DWORD i = 0; i < 4; ++i) if (XInputGetState(i, &state) == ERROR_SUCCESS) break;`
On Windows, `XInputGetState` is notorious for causing massive thread blocking (up to 2-5ms) when polling a disconnected port because it queries the USB stack. Doing this 4 times per frame for disconnected ports will absolutely destroy the game's framerate, causing stuttering.
**Verdict:** FAILS Rule 1 & Rule 3 (Zero-Overhead Principle). Dover runs on the game's rendering thread; we cannot block it for USB polling.

---

## 2. Refactoring Plan

To rectify these violations without over-engineering (Rule 5), we will implement the following mechanical optimizations:

### Step 1: Linear `O(N)` Asset Grid Rendering
- **Action:** Refactor the `CrosshairGrid` rendering loop in `crosshair_window.cpp`.
- **Implementation:** Instead of calling `GetCrosshairAsset(i)` inside a `0` to `total` loop, we will use a single range-based `for (auto& asset : assets)` loop. 
- We will manually increment a `virtual_idx` counter inside the loop, skipping `asset.name.rfind("gamepad/", 0) == 0`. 
- This drops the complexity from `O(N^2)` to a single `O(N)` pass per frame.

### Step 2: XInput Polling Throttling
- **Action:** Prevent `XInputGetState` from polling disconnected ports every frame.
- **Implementation:** 
  - Introduce a lightweight static frame counter or timestamp in `RenderGamepadOverlay`.
  - Cache the last known `active_user_index`.
  - On every frame, ONLY poll the `active_user_index`.
  - If it returns `ERROR_DEVICE_NOT_CONNECTED`, stop rendering the HUD for that frame.
  - Only poll the other 3 indices once every 60 frames (1 second) to detect new connections. 
  - This completely eliminates the 15ms frame-drop penalty while remaining zero-cost and heap-allocation-free.


### USER REVIEW

Ada satu skenario interaksi yang harus lu pastiin aman: pas stick controller aktif tiba-tiba putus atau DC di tengah game (ERROR_DEVICE_NOT_CONNECTED), kuli lu bilang bakal langsung stop render HUD di frame itu. Pastiin pas momen DC itu terjadi, status cache active_user_index langsung di-invalidate seketika, dan jangan dipaksa nunggu siklus 60 frame penuh cuma buat nyari tahu kalau stick-nya udah mati. Respon dOverlay harus tetep instan pas stick user mendadak mati atau dicolok ulang.