param(
    [string]$Config = "Release",
    [switch]$ForceConfigure,
    [switch]$EnableASAN,
    [switch]$EnableAnalyze,
    [switch]$PGOInstrument,
    [switch]$PGOOptimize
)

$ErrorActionPreference = "Stop"

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "      DOVER AUTOMATED BUILD SYSTEM      " -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

# Locate vswhere.exe
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Host "[ERROR] vswhere.exe not found. Is MSVC installed?" -ForegroundColor Red
    Exit 1
}

# Locate latest MSVC Installation (MSVC 2026)
Write-Host "Locating MSVC Build Tools..." -ForegroundColor Gray
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    Write-Host "[ERROR] Valid MSVC installation with C++ tools not found." -ForegroundColor Red
    Exit 1
}
$vcvarsall = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
if (-not (Test-Path $vcvarsall)) {
    Write-Host "[ERROR] vcvarsall.bat not found at $vcvarsall" -ForegroundColor Red
    Exit 1
}
Write-Host "Found MSVC at: $vsPath" -ForegroundColor Green

# Prepare CMake arguments for advanced features
$CMakeArgs = "-DCMAKE_BUILD_TYPE=$Config -DDOVER_STRICT_BUILD=ON `"-DCMAKE_POLICY_VERSION_MINIMUM=3.5`""
if ($EnableASAN) { $CMakeArgs += " -DDOVER_ENABLE_ASAN=ON" }
if ($EnableAnalyze) { $CMakeArgs += " -DDOVER_ENABLE_ANALYZE=ON" }
if ($PGOInstrument) { $CMakeArgs += " -DDOVER_PGO_INSTRUMENT=ON" }
if ($PGOOptimize) { $CMakeArgs += " -DDOVER_PGO_OPTIMIZE=ON" }

function Invoke-MSVCCmd {
    param([string]$arch, [string]$cmdString)
    $fullCmd = "`"$vcvarsall`" $arch > NUL && $cmdString"
    cmd.exe /c $fullCmd
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE"
    }
}

# 1. Build x64 Target
Write-Host "`n[1/3] Building 64-bit Architecture (x64)..." -ForegroundColor Yellow
$ConfigureX64 = $ForceConfigure -or !(Test-Path "build_x64\CMakeCache.txt")
if ($ConfigureX64) {
    Write-Host "Configuring build directory for x64..." -ForegroundColor Gray
    Invoke-MSVCCmd "x64" "cmake -B build_x64 -S . -G Ninja $CMakeArgs"
}
Write-Host "Compiling x64..." -ForegroundColor Gray
Invoke-MSVCCmd "x64" "cmake --build build_x64 --config $Config --parallel"

# 2. Build x86 Target
Write-Host "`n[2/3] Building 32-bit Architecture (x86)..." -ForegroundColor Yellow
$ConfigureX86 = $ForceConfigure -or !(Test-Path "build_x86\CMakeCache.txt")
if ($ConfigureX86) {
    Write-Host "Configuring build directory for x86..." -ForegroundColor Gray
    Invoke-MSVCCmd "x86" "cmake -B build_x86 -S . -G Ninja $CMakeArgs"
}
Write-Host "Compiling x86..." -ForegroundColor Gray
Invoke-MSVCCmd "x86" "cmake --build build_x86 --config $Config --parallel"

# 3. Consolidate Binaries
Write-Host "`n[3/3] Consolidating and packaging binaries..." -ForegroundColor Yellow
$dest_dir = "build_x64\bin"
if (Test-Path "build_x64\bin\$Config") {
    $dest_dir = "build_x64\bin\$Config"
}

if (!(Test-Path $dest_dir)) {
    New-Item -ItemType Directory -Force -Path $dest_dir | Out-Null
}

function Copy-BuiltBinary {
    param([string]$SourceDir, [string]$FileName, [string]$DestDir)
    $found = Get-ChildItem -Path $SourceDir -Filter $FileName -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) {
        $destPath = Join-Path (Resolve-Path $DestDir).Path $FileName
        if ($found.FullName -ne $destPath) {
            Copy-Item $found.FullName "$DestDir\" -Force
        }
        Write-Host "Copied $FileName successfully." -ForegroundColor Gray
    } else {
        Write-Host "[WARNING] $FileName not found in $SourceDir." -ForegroundColor DarkYellow
    }
}

Copy-BuiltBinary "build_x86\bin" "injector32.exe" $dest_dir
Copy-BuiltBinary "build_x86\bin" "overlay32.dll" $dest_dir

Write-Host "`n=========================================" -ForegroundColor Green
Write-Host "  BUILD SUCCESSFUL! Binaries packaged at: " -ForegroundColor Green
Write-Host "  $dest_dir" -ForegroundColor Green
Write-Host "=========================================" -ForegroundColor Green
