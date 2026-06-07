# Builds the TikTool Live Captions OBS plugin on Windows.
#
# Prereqs (download once):
#   * Visual Studio 2022 Build Tools (Desktop C++) - https://visualstudio.microsoft.com/downloads/
#   * CMake 3.28+                                   - https://cmake.org/download/
#   * Qt 6.8+ for MSVC2022 64-bit                   - https://www.qt.io/download
#   * OBS Studio 31.x prebuilt deps from obsproject - https://github.com/obsproject/obs-deps
#
# Set these once in your shell before running:
#   $env:QT_DIR    = "C:\Qt\6.8.1\msvc2022_64"
#   $env:OBS_DEPS  = "C:\obs-deps\windows-x64"
#   $env:OBS_SRC   = "C:\obs-studio"   # cloned obsproject/obs-studio @ v31.0.0
#
# Then:
#   pwsh ci/build-windows.ps1
#
# Output ends up under ./build_x64/RelWithDebInfo/obs-tiktool-captions.dll
# plus the data/ folder ready for install into
#   %ProgramFiles%\obs-studio\obs-plugins\64bit\
#   %ProgramData%\obs-studio\plugins\obs-tiktool-captions\bin\64bit\
#   %ProgramData%\obs-studio\plugins\obs-tiktool-captions\data\

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if (-not $env:QT_DIR)   { throw "Set QT_DIR (e.g. C:\Qt\6.8.1\msvc2022_64)" }
if (-not $env:OBS_DEPS) { throw "Set OBS_DEPS (obs-deps prebuilt root)" }
if (-not $env:OBS_SRC)  { throw "Set OBS_SRC (path to obsproject/obs-studio checkout)" }

$buildDir = Join-Path $repoRoot "build_x64"
if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir | Out-Null }

cmake -G "Visual Studio 17 2022" -A x64 `
  -S $repoRoot -B $buildDir `
  -DCMAKE_PREFIX_PATH="$env:QT_DIR;$env:OBS_DEPS" `
  -DCMAKE_MODULE_PATH="$env:OBS_SRC/cmake/Modules" `
  -Dlibobs_DIR="$env:OBS_SRC/libobs" `
  -Dobs-frontend-api_DIR="$env:OBS_SRC/UI/obs-frontend-api"

cmake --build $buildDir --config RelWithDebInfo --parallel

$out = Join-Path $buildDir "RelWithDebInfo\obs-tiktool-captions.dll"
if (-not (Test-Path $out)) { throw "Build did not produce $out" }

Write-Host ""
Write-Host "Built:" -ForegroundColor Green
Write-Host "  $out"
Write-Host ""
Write-Host "Install to (admin shell required for the first path):" -ForegroundColor Yellow
Write-Host "  Copy-Item '$out' 'C:\Program Files\obs-studio\obs-plugins\64bit\'"
Write-Host "  Copy-Item -Recurse data 'C:\Program Files\obs-studio\data\obs-plugins\obs-tiktool-captions\'"
