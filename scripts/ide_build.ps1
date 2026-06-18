# ide_build.ps1 - Windows single-file build dispatcher for the VSCode default
# build task (.vscode/tasks.json, windows override). The Windows counterpart of
# ide_build.sh: a .cpp or .cu compiles with clang++ (GNU-style driver, MSVC ABI)
# to <dir>/debug_bin/<stem>.exe, where launch.json and the "Run Executable" task
# find it (the .exe suffix matches the Linux side). A .cu adds clang's CUDA
# flags: the CUDA bucket builds with clang, not nvcc, because nvcc 13.3's MSVC
# device frontend has no C++23 dialect. See crossplatform.md.
[CmdletBinding()]
param([Parameter(Mandatory)][string] $Src)

$ErrorActionPreference = 'Stop'

$llvmRoot = 'C:/Program Files/LLVM'
$clang = "$llvmRoot/bin/clang++.exe"
if (-not (Test-Path $clang)) { throw "clang++ not found at $clang" }
if (-not (Test-Path $Src)) { throw "source not found: $Src" }

# scripts/ide_build.ps1 -> the repo root is the parent directory.
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$srcDir = Split-Path -Parent $Src
$stem = [IO.Path]::GetFileNameWithoutExtension($Src)
$outDir = Join-Path $srcDir 'debug_bin'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$out = Join-Path $outDir "$stem.exe"

# Bring in the MSVC environment (INCLUDE/LIB for clang++) unless already inside
# a Developer shell.
if (-not $env:VSCMD_VER) {
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vswhere)) { throw "vswhere not found at $vswhere" }
  # Put the Installer dir on PATH so Enter-VsDevShell's own internal vswhere
  # lookup succeeds instead of printing a benign "vswhere not found" error.
  $env:PATH = "$(Split-Path $vswhere);$env:PATH"
  $vsPath = & $vswhere -latest -property installationPath
  if (-not $vsPath) { throw 'No Visual Studio installation found' }
  Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
  Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
    -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

# Catch2 comes from the CMake build's persistent FetchContent cache (built by
# cleanbuild.ps1). catch2_main.h supplies main() via Catch::Session, so only the
# core Catch2 lib is needed, not Catch2Main. cleanbuild builds release, so the
# lib is Catch2.lib (/MD); fall back to Catch2d.lib (/MDd) if a debug cache is
# present. Run ./cleanbuild.ps1 once to populate the cache.
$fc = Join-Path $workspace 'tests/.fetchcontent'
$catch2 = Get-ChildItem (Join-Path $fc 'catch2-build/src') -Filter 'Catch2*.lib' -ErrorAction SilentlyContinue |
  Where-Object { $_.Name -notmatch 'Main' } | Select-Object -First 1
if (-not $catch2) {
  throw "Catch2 not built yet (under $fc). Run ./cleanbuild.ps1 first."
}
# Match the CRT cleanbuild used for Catch2 - a dynamic/static or release/debug
# CRT mismatch is a hard link error on Windows. clang++ defaults to the static
# CRT, so request the dynamic one (dll = /MD, dll_dbg = /MDd) to match Catch2.
$crt = if ($catch2.Name -match 'd\.lib$') { 'dll_dbg' } else { 'dll' }

# Debug single-file build: -g (CodeView debug info -> PDB, which launch.json's
# cppvsdbg config reads), -O0 (un-optimized stepping), C++23, and the CRT that
# matches Catch2. Test sources include corvid headers root-relative ("corvid/...")
# and catch2_main.h by bare name, so the repo root and tests/ both go on the
# include path.
$clArgs = @(
  '-std=c++23', '-g', '-O0', "-fms-runtime-lib=$crt",
  '-I', $workspace,
  '-I', (Join-Path $workspace 'tests'),
  '-I', (Join-Path $fc 'catch2-src/src'),
  '-I', (Join-Path $fc 'catch2-build/generated-includes')
)

if ($Src -like '*.cu') {
  # CUDA flags. clang's CUDA driver needs the target GPU arch (resolved from
  # nvidia-smi, e.g. 8.9 -> sm_89), the toolkit path (derived from nvcc's
  # location), and an explicit cudart link (clang does not auto-add it on
  # Windows). -Wno-unknown-cuda-version silences the note that CUDA 13.3 is
  # newer than clang 22's last fully supported toolkit. No -Wall/-Werror here,
  # matching the cleanbuild CUDA bucket.
  $nvcc = Get-Command nvcc -ErrorAction SilentlyContinue
  if (-not $nvcc) { throw 'CUDA (.cu) build needs nvcc on PATH' }
  $cudaPath = Split-Path (Split-Path $nvcc.Source)
  $cc = nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>$null | Select-Object -First 1
  if (-not $cc) { throw 'no GPU arch from nvidia-smi' }
  $arch = ($cc.Trim() -replace '\.', '')
  $clArgs += @(
    "--cuda-gpu-arch=sm_$arch", "--cuda-path=$cudaPath", '-Wno-unknown-cuda-version',
    "-L$cudaPath/lib/x64", '-lcudart')
  # Auto-link cuBLAS for sources that use it, mirroring the cleanbuild CUDA loop
  # (which adds CUDA::cublas when the source mentions cublas). It ships with the
  # toolkit, so the lib path above already covers it.
  if (Select-String -Path $Src -Pattern 'cublas' -Quiet) { $clArgs += '-lcublas' }
} else {
  # clang's real warning set, as errors, mirroring ide_build.sh.
  $clArgs += @('-Wall', '-Wextra', '-Werror', '-Wno-unused-variable')
}

# SDL3 (window/input) for sources that pull in a corvid/sdl header - the
# prebuilt drop fetched by scripts/fetch_sdl3.ps1 (mirrors the cuBLAS auto-link
# above). -isystem keeps SDL's headers clear of -Werror; link the import lib and
# stage SDL3.dll next to the exe so F5 can run it.
$sdl3Lib = $null
$sdl3Dll = $null
if (Select-String -Path $Src -Pattern 'corvid/sdl|SDL3/' -Quiet) {
  $sdl3 = Join-Path $workspace 'tests/.local/sdl3'
  $sdl3Lib = Join-Path $sdl3 'lib/x64/SDL3.lib'
  $sdl3Dll = Join-Path $sdl3 'lib/x64/SDL3.dll'
  if (-not (Test-Path $sdl3Lib)) {
    throw "SDL3 not found at $sdl3. Run ./scripts/fetch_sdl3.ps1 first."
  }
  $clArgs += @('-isystem', (Join-Path $sdl3 'include'))
}

$clArgs += @($Src, $catch2.FullName)
if ($sdl3Lib) { $clArgs += $sdl3Lib }
# Direct3D 11 (device/swapchain/present) for the Windows-only CUDA cell,
# mirroring the CMake windows-CUDA targets that link d3d11/dxgi for
# /cuda/windows/ sources. Both libs ship in the Windows SDK, on %LIB% from the
# dev shell entered above, so the linker resolves them with no explicit path.
if ($Src -match 'cuda[\\/]windows') { $clArgs += @('-ld3d11', '-ldxgi') }
$clArgs += @('-o', $out)
& $clang @clArgs
$rc = $LASTEXITCODE
if ($rc -eq 0 -and $sdl3Dll) { Copy-Item -Path $sdl3Dll -Destination $outDir -Force }
exit $rc
