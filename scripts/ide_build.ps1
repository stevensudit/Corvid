# ide_build.ps1 - Windows single-file build dispatcher for the VSCode default
# build task (.vscode/tasks.json, windows override). The Windows counterpart of
# ide_build.sh: a .cpp compiles with clang-cl against the MSVC STL; the output
# lands at <dir>/debug_bin/<stem>.exe, where launch.json and the "Run
# Executable" task find it (the .exe suffix matches the Linux side, which now
# also appends it). CUDA (.cu) on Windows is Phase 4 and not wired yet. See
# crossplatform.md.
[CmdletBinding()]
param([Parameter(Mandatory)][string] $Src)

$ErrorActionPreference = 'Stop'

$clang = 'C:/Program Files/LLVM/bin/clang-cl.exe'
if (-not (Test-Path $clang)) { throw "clang-cl not found at $clang" }
if (-not (Test-Path $Src)) { throw "source not found: $Src" }

# scripts/ide_build.ps1 -> the repo root is the parent directory.
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$srcDir = Split-Path -Parent $Src
$stem = [IO.Path]::GetFileNameWithoutExtension($Src)
$outDir = Join-Path $srcDir 'debug_bin'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$out = Join-Path $outDir "$stem.exe"

if ($Src -like '*.cu') {
  throw 'CUDA single-file build on Windows is not wired yet (Phase 4); build .cu under WSL/Linux for now.'
}

# Bring in the MSVC environment (INCLUDE/LIB for clang-cl) unless already inside
# a Developer shell.
if (-not $env:VSCMD_VER) {
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vswhere)) { throw "vswhere not found at $vswhere" }
  $vsPath = & $vswhere -latest -property installationPath
  if (-not $vsPath) { throw 'No Visual Studio installation found' }
  Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
  Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
    -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

# Catch2 comes from the CMake build's persistent FetchContent cache (built with
# the same clang-cl and debug CRT). catch2_main.h supplies main() via
# Catch::Session, so only Catch2d.lib is needed, not Catch2Maind.lib. Run
# ./cleanbuild.ps1 once to populate the cache.
$fc = Join-Path $workspace 'tests/.fetchcontent'
$catch2Lib = Join-Path $fc 'catch2-build/src/Catch2d.lib'
if (-not (Test-Path $catch2Lib)) {
  throw "Catch2 not built yet ($catch2Lib). Run ./cleanbuild.ps1 first."
}

# Debug single-file build: /MDd (debug CRT, matching Catch2d.lib), /Zi (PDB debug
# info), /Od (un-optimized stepping). C++23 via /std:c++latest. Test sources
# include corvid headers root-relative ("corvid/...") and catch2_main.h by bare
# name, so the repo root and tests/ both go on the include path.
$clArgs = @(
  '/nologo', '/std:c++latest', '/EHsc', '/MDd', '/Zi', '/Od',
  '-I', $workspace,
  '-I', (Join-Path $workspace 'tests'),
  '-I', (Join-Path $fc 'catch2-src/src'),
  '-I', (Join-Path $fc 'catch2-build/generated-includes'),
  $Src,
  $catch2Lib,
  "/Fe:$out",
  "/Fo:$outDir\",
  "/Fd:$outDir\$stem.pdb"
)
& $clang @clArgs
exit $LASTEXITCODE
