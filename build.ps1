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
Write-Host "      DOVER OPTIMIZED BUILD SYSTEM       " -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

# 1. Environment Discovery (Cached)
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvarsall = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"

if (-not (Test-Path $vcvarsall)) {
    Write-Host "[ERROR] MSVC Build Tools not found." -ForegroundColor Red
    Exit 1
}

# 2. Build Context
$CMakeArgs = "-DCMAKE_BUILD_TYPE=$Config -DDOVER_STRICT_BUILD=ON `"-DCMAKE_POLICY_VERSION_MINIMUM=3.5`""
if ($EnableASAN) { $CMakeArgs += " -DDOVER_ENABLE_ASAN=ON" }
if ($EnableAnalyze) { $CMakeArgs += " -DDOVER_ENABLE_ANALYZE=ON" }
if ($PGOInstrument) { $CMakeArgs += " -DDOVER_PGO_INSTRUMENT=ON" }
if ($PGOOptimize) { $CMakeArgs += " -DDOVER_PGO_OPTIMIZE=ON" }

$BuildTask = {
    param($arch, $vcvars, $args, $force, $config, $root)
    $ErrorActionPreference = "Continue"
    Set-Location $root
    
    $buildDir = "build_$arch"
    
    # Minimize environment re-init by chaining commands in a single CMD session
    $configureCmd = if ($force -or !(Test-Path "$buildDir\CMakeCache.txt")) { "cmake -B $buildDir -S . -G Ninja $args" } else { "echo Skip Configure" }
    $buildCmd = "cmake --build $buildDir --config $config --parallel"
    
    $fullCmd = "`"$vcvars`" $arch > NUL && $configureCmd && $buildCmd"
    cmd.exe /c $fullCmd
    return $LASTEXITCODE
}

# 3. Execution Phase: Parallel Compilation & Asset Generation
Write-Host "Launching Parallel Build (x64 + x86)..." -ForegroundColor Yellow
$job64 = Start-Job -ScriptBlock $BuildTask -ArgumentList "x64", $vcvarsall, $CMakeArgs, $ForceConfigure, $Config, $PSScriptRoot
$job86 = Start-Job -ScriptBlock $BuildTask -ArgumentList "x86", $vcvarsall, $CMakeArgs, $ForceConfigure, $Config, $PSScriptRoot

Write-Host "Generating Assets Pipeline..." -ForegroundColor Yellow
python assets/build_assets.py

Write-Host "Waiting for compilation to complete..." -ForegroundColor Gray
$null = Wait-Job $job64, $job86

# 4. Result Processing
$out64 = Receive-Job $job64
$out86 = Receive-Job $job86

if ($job64.ChildJobs[0].ExitCode -ne 0 -or $job86.ChildJobs[0].ExitCode -ne 0) {
    Write-Host "`n[ERROR] Parallel build failed." -ForegroundColor Red
    Write-Host "--- x64 Output ---" -ForegroundColor DarkGray
    $out64
    Write-Host "--- x86 Output ---" -ForegroundColor DarkGray
    $out86
    Exit 1
}

# 5. Consolidation (Zero-Overhead via CMake Install)
Write-Host "`n[3/3] Consolidating and packaging binaries..." -ForegroundColor Yellow
$dest_dir = if ($Config -eq "Debug") { "build_x64\bin\Debug" } else { "build_x64\bin\Release" }

# Use CMake's built-in install mechanism to avoid manual I/O and disk scanning
cmake --install build_x64 --config $Config --prefix $dest_dir
cmake --install build_x86 --config $Config --prefix $dest_dir

Write-Host "`n=========================================" -ForegroundColor Green
Write-Host "  BUILD SUCCESSFUL! Binaries packaged at: " -ForegroundColor Green
Write-Host "  $dest_dir" -ForegroundColor Green
Write-Host "=========================================" -ForegroundColor Green
