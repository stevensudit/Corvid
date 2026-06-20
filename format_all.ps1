# Format all C++ source and header files in the project using clang-format.
# Windows PowerShell counterpart of format_all.sh.

$ErrorActionPreference = 'Stop'

# Locate clang-format, preferring a versioned clang-format-22 on PATH, then a
# plain clang-format on PATH, then the LLVM install scripts/ide_build.ps1 uses
# (Windows LLVM ships an unversioned clang-format.exe).
$clangFormat = $null
foreach ($name in 'clang-format-22', 'clang-format') {
  $cmd = Get-Command $name -ErrorAction SilentlyContinue
  if ($cmd) { $clangFormat = $cmd.Source; break }
}
if (-not $clangFormat) {
  $fallback = 'C:/Program Files/LLVM/bin/clang-format.exe'
  if (Test-Path $fallback) { $clangFormat = $fallback }
}
if (-not $clangFormat) {
  throw 'clang-format not found. Install LLVM or add clang-format to PATH.'
}

Write-Host "Using $clangFormat"
Write-Host 'Formatting all .cpp, .h, .cu, and .cuh files...'

# Find all .cpp, .h, .cu, and .cuh files under the repo root (this script's
# directory), excluding build directories, CMake internals, the FetchContent
# cache, and the .local sandbox (used for MSAN-instrumented LLVM source).
$exclude = '[\\/](build|CMakeFiles|\.fetchcontent|\.local)[\\/]'
Get-ChildItem -Path $PSScriptRoot -Recurse -File -Include *.cpp, *.h, *.cu, *.cuh |
  Where-Object { $_.FullName -notmatch $exclude } |
  ForEach-Object {
    Write-Host "Formatting: $($_.FullName)"
    & $clangFormat -i $_.FullName
  }

Write-Host 'Done formatting all files.'
