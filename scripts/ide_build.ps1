# ide_build.ps1 - Windows single-file IDE build for the VSCode default build
# task (.vscode/tasks.json, windows override). Drives a dedicated debug CMake
# tree (tests/build-debug) rather than invoking clang++ directly, so CMake stays
# the single source of truth for include paths AND link libraries; this script
# names none of its own. The requested target builds to
# tests/build-debug/debug_bin/<stem>.exe, where launch.json and the "Run
# Executable" task look. The Windows counterpart of ide_build.sh. See
# crossplatform.md.
[CmdletBinding()]
param([Parameter(Mandatory)][string] $Src)

$ErrorActionPreference = 'Stop'

# scripts/ide_build.ps1 -> the repo root is the parent directory.
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$srcRoot = Join-Path $repo 'tests'
$bldDir = Join-Path $repo 'tests/build-debug'
$fcDebug = Join-Path $repo 'tests/.fetchcontent-debug'
$llvmRoot = 'C:/Program Files/LLVM'
$clangXx = "$llvmRoot/bin/clang++.exe"
if (-not (Test-Path $clangXx)) { throw "clang++ not found at $clangXx" }
if (-not (Test-Path $Src)) { throw "source not found: $Src" }
$stem = [IO.Path]::GetFileNameWithoutExtension($Src)

# Bring in the MSVC environment (INCLUDE/LIB for clang++, and cmake/ninja) unless
# already inside a Developer shell. Same block as cleanbuild.ps1.
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

# CUDA (.cu) targets compile with clang (not nvcc) on Windows; mirror
# cleanbuild.ps1's setup (GPU arch from nvidia-smi). Enabling CUDA whenever the
# toolkit is present keeps the configure signature stable across .cpp and .cu
# files, so switching files never forces a reconfigure.
$cudaArgs = @()
if (Get-Command nvcc -ErrorAction SilentlyContinue) {
  $cc = nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>$null | Select-Object -First 1
  if (-not $cc) { throw 'CUDA toolkit present but no GPU arch from nvidia-smi' }
  $arch = ($cc.Trim() -replace '\.', '')
  $cudaArgs = @('-DCORVID_ENABLE_CUDA=ON', "-DCMAKE_CUDA_COMPILER=$clangXx",
    "-DCMAKE_CUDA_ARCHITECTURES=$arch")
} elseif ($Src -like '*.cu') {
  throw "'$Src' is a .cu file but the CUDA toolkit (nvcc + a GPU) was not found"
}

# Configure the debug tree: Release build type for the always-on /MD CRT (an
# empty/Debug type pulls the debug CRT, which fails to link the Release-CRT
# Catch2), with IDE_DEBUG flipping the flags to -O0 -g. Its own FetchContent
# base keeps it from fighting cleanbuild's Release tree over the shared Catch2
# build. No TEST_NAME: all targets configure once, then a single --target builds
# on demand. Reuse the configured tree when the signature is unchanged, as
# cleanbuild does, so only the first build pays the configure cost.
$cfg = @('-S', $srcRoot, '-B', $bldDir, '-G', 'Ninja',
  "-DCMAKE_CXX_COMPILER=$clangXx", '-DCMAKE_BUILD_TYPE=Release',
  '-DIDE_DEBUG=ON', "-DFETCHCONTENT_BASE_DIR=$fcDebug") + $cudaArgs
$sigFile = Join-Path $bldDir '.ide-build-config'
$configSig = ($cfg -join '|')
$reuse = (Test-Path $sigFile) -and (Test-Path (Join-Path $bldDir 'CMakeCache.txt')) -and
  ((Get-Content $sigFile -Raw).Trim() -eq $configSig)
if (-not $reuse) {
  cmake --fresh @cfg
  if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }
  Set-Content -Path $sigFile -Value $configSig
}

# Build just the requested target. CMake supplies its flags and link libraries;
# the SDL3.dll stage and PDB land next to the exe in debug_bin/ on their own.
cmake --build $bldDir --target $stem
exit $LASTEXITCODE
