# PowerShell Build Automation Script for Dover
$ErrorActionPreference = "Stop"

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "      DOVER AUTOMATED BUILD SYSTEM      " -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

# 1. Build x64 Target
Write-Host "`n[1/3] Building 64-bit Architecture (x64)..." -ForegroundColor Yellow
if (!(Test-Path "build_x64")) {
    cmake -B build_x64 -S . -A x64
}
cmake --build build_x64 --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[ERROR] Failed to compile 64-bit target." -ForegroundColor Red
    Exit $LASTEXITCODE
}

# 2. Build x86 Target
Write-Host "`n[2/3] Building 32-bit Architecture (x86)..." -ForegroundColor Yellow
if (!(Test-Path "build_x86")) {
    cmake -B build_x86 -S . -A Win32
}
cmake --build build_x86 --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[ERROR] Failed to compile 32-bit target." -ForegroundColor Red
    Exit $LASTEXITCODE
}

# 3. Consolidate Release Binaries
Write-Host "`n[3/3] Consolidating and packaging binaries..." -ForegroundColor Yellow
$dest_dir = "build_x64\bin\Release"

if (!(Test-Path $dest_dir)) {
    New-Item -ItemType Directory -Force -Path $dest_dir | Out-Null
}

if (Test-Path "build_x86\bin\Release\dover_injector32.exe") {
    Copy-Item "build_x86\bin\Release\dover_injector32.exe" "$dest_dir\" -Force
    Write-Host "Copied dover_injector32.exe successfully." -ForegroundColor Gray
} else {
    Write-Host "[WARNING] build_x86\bin\Release\dover_injector32.exe not found." -ForegroundColor DarkYellow
}

if (Test-Path "build_x86\bin\Release\dover_overlay32.dll") {
    Copy-Item "build_x86\bin\Release\dover_overlay32.dll" "$dest_dir\" -Force
    Write-Host "Copied dover_overlay32.dll successfully." -ForegroundColor Gray
} else {
    Write-Host "[WARNING] build_x86\bin\Release\dover_overlay32.dll not found." -ForegroundColor DarkYellow
}

Write-Host "`n=========================================" -ForegroundColor Green
Write-Host "  BUILD SUCCESSFUL! Binaries packaged at: " -ForegroundColor Green
Write-Host "  $dest_dir" -ForegroundColor Green
Write-Host "=========================================" -ForegroundColor Green
