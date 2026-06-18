# Fetch the official prebuilt SDL3 Visual C++ binaries into tests/.local/sdl3/.
# One-time setup, the Windows analog of scripts/build_openssl_quic.sh. SDL is a
# C API, so the MSVC-built binaries link cleanly from the clang++ (MSVC-ABI)
# CUDA build and a single SDL3.dll serves both the Release and debug build
# paths. See crossplatform.md section 11.
$ErrorActionPreference = 'Stop'

$version = '3.4.10'
$repoRoot = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $repoRoot 'tests/.local/sdl3'
$config = Join-Path $dest 'cmake/SDL3Config.cmake'

if (Test-Path $config) {
    Write-Host "SDL3 $version already present at $dest"
    exit 0
}

$zipName = "SDL3-devel-$version-VC.zip"
$url = "https://github.com/libsdl-org/SDL/releases/download/release-$version/$zipName"
$tmp = Join-Path $env:TEMP "sdl3-fetch-$version"
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$zipPath = Join-Path $tmp $zipName
Write-Host "Downloading $url"
Invoke-WebRequest -Uri $url -OutFile $zipPath

Write-Host "Extracting to $dest"
Expand-Archive -Path $zipPath -DestinationPath $tmp -Force

# The VC zip expands to a SDL3-<version>/ folder; flatten its contents into
# tests/.local/sdl3/ so SDL3Config.cmake lands at a stable path.
$extracted = Join-Path $tmp "SDL3-$version"
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item -Path (Join-Path $extracted '*') -Destination $dest -Recurse -Force
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue

if (-not (Test-Path $config)) {
    throw "SDL3 install looks wrong: $config not found after extraction."
}
Write-Host "SDL3 $version installed at $dest"
