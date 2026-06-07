param(
    [string]$Config = "Release",
    [switch]$ForceConfigure,
    [switch]$EnableASAN,
    [switch]$EnableAnalyze,
    [switch]$PGOInstrument,
    [switch]$PGOOptimize
)

$ErrorActionPreference = "Continue"

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "      DOVER OPTIMIZED BUILD SYSTEM       " -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

# 1. Environment Discovery
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvarsall = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"

if (-not (Test-Path $vcvarsall)) {
    Write-Host "[ERROR] MSVC Build Tools not found." -ForegroundColor Red
    Exit 1
}

# Helper to run commands in MSVC context
function Invoke-MSVC {
    param($arch, $cmd)
    $fullCmd = "`"$vcvarsall`" $arch > NUL && $cmd"
    cmd.exe /c $fullCmd
}

# 2. Build Context
$CMakeArgs = "-DCMAKE_BUILD_TYPE=$Config -DDOVER_STRICT_BUILD=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -Wno-dev -DCMAKE_WARN_DEPRECATED=OFF"
if ($EnableASAN) { $CMakeArgs += " -DDOVER_ENABLE_ASAN=ON" }
if ($EnableAnalyze) { $CMakeArgs += " -DDOVER_ENABLE_ANALYZE=ON" }
if ($PGOInstrument) { $CMakeArgs += " -DDOVER_PGO_INSTRUMENT=ON" }
if ($PGOOptimize) { $CMakeArgs += " -DDOVER_PGO_OPTIMIZE=ON" }

$BuildTask = {
    param($arch, $vcvars, $cmake_args, $force, $config, $root)
    Set-Location $root
    
    $buildDir = "out/intermediate/$arch"
    $configureCmd = if ($force -or !(Test-Path "$buildDir\CMakeCache.txt")) { "cmake -B $buildDir -S . -G Ninja $cmake_args" } else { "echo Skip Configure" }
    $buildCmd = "cmake --build $buildDir --config $config --parallel"
    
    # Run in CMD to preserve environment from vcvarsall
    $fullCmd = "`"$vcvars`" $arch > NUL && $configureCmd && $buildCmd"
    cmd.exe /c $fullCmd
    return $LASTEXITCODE
}

# 3. Execution Phase
Write-Host "Generating Assets Pipeline..." -ForegroundColor Yellow
python assets/build_assets.py

Write-Host "Building x64 Target..." -ForegroundColor Yellow
& $BuildTask "x64" $vcvarsall $CMakeArgs $ForceConfigure $Config $PSScriptRoot
if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] x64 Build failed." -ForegroundColor Red; Exit 1 }

Write-Host "Building x86 Target..." -ForegroundColor Yellow
& $BuildTask "x86" $vcvarsall $CMakeArgs $ForceConfigure $Config $PSScriptRoot
if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] x86 Build failed." -ForegroundColor Red; Exit 1 }

# 5. Consolidation & Verification
Write-Host "`n[3/3] Consolidating and packaging binaries..." -ForegroundColor Yellow
$dest_dir = "out/bin/$Config"
if (!(Test-Path $dest_dir)) { New-Item -ItemType Directory -Path $dest_dir -Force | Out-Null }

# Run install via MSVC context to ensure 'cmake' is found
Invoke-MSVC "x64" "cmake --install out/intermediate/x64 --config $Config --prefix $dest_dir"
Invoke-MSVC "x86" "cmake --install out/intermediate/x86 --config $Config --prefix $dest_dir"

$all_binaries_built = (Test-Path "$dest_dir/launcher.exe") -and (Test-Path "$dest_dir/overlay64.dll") -and (Test-Path "$dest_dir/overlay32.dll")

if ($all_binaries_built) {
    Write-Host "`n=========================================" -ForegroundColor Green
    Write-Host "  BUILD SUCCESSFUL! Binaries packaged at: " -ForegroundColor Green
    Write-Host "  $dest_dir" -ForegroundColor Green
    Write-Host "=========================================" -ForegroundColor Green
} else {
    Write-Host "`n[ERROR] Build failed. Some binaries are missing." -ForegroundColor Red
    Exit 1
}
