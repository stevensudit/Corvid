# Format all C++ source and header files in the project using clang-format.
# Windows PowerShell counterpart of format_all.sh.

$ErrorActionPreference = 'Stop'

# Locate clang-format: a plain clang-format on PATH, then the LLVM install
# scripts/ide_build.ps1 uses (Windows LLVM ships an unversioned
# clang-format.exe, so no versioned name is needed).
$clangFormat = $null
$cmd = Get-Command 'clang-format' -ErrorAction SilentlyContinue
if ($cmd) { $clangFormat = $cmd.Source }
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
# caches (`.fetchcontent`, `.fetchcontent-debug`, ...), and the .local sandbox
# (used for MSAN-instrumented LLVM source).
$exclude = '[\\/](build|CMakeFiles|\.fetchcontent[^\\/]*|\.local)[\\/]'
Get-ChildItem -Path $PSScriptRoot -Recurse -File -Include *.cpp, *.h, *.cu, *.cuh |
  Where-Object { $_.FullName -notmatch $exclude } |
  ForEach-Object {
    Write-Host "Formatting: $($_.FullName)"
    if ($_.Extension -eq '.h') {
      # clang-format guesses a .h file's language, and a C++26 reflection
      # splice (`[: ... :]`) reads to it as an Objective-C message send, which
      # the repo style does not cover, so it silently falls back to its default
      # style. Formatting through stdin under an assumed .cpp name pins C++.
      # The style file is still found from the assumed name's directory. cmd
      # does the redirection so the bytes pass through untouched.
      $assumed = [System.IO.Path]::ChangeExtension($_.FullName, '.cpp')
      $tmp = [System.IO.Path]::GetTempFileName()
      & cmd /c "`"$clangFormat`" --style=file `"--assume-filename=$assumed`" < `"$($_.FullName)`" > `"$tmp`""
      if ($LASTEXITCODE -ne 0) { throw "clang-format failed on $($_.FullName)" }
      Copy-Item -Path $tmp -Destination $_.FullName -Force
      Remove-Item $tmp
    } else {
      & $clangFormat -i $_.FullName
    }
  }

Write-Host 'Done formatting all files.'
