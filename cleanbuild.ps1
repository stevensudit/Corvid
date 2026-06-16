# cleanbuild.ps1 - Windows build driver for the portable (and, later, CUDA) test
# buckets. Mirrors cleanbuild.sh's role on Linux: wipe tests/build, configure
# with clang-cl against the MSVC STL, build, and run ctest. The Linux-only modes
# (libc++/libstdc++ choice, msan/tsan, analyze-build scan, llvm-cov coverage,
# compiler-rt/lld) have no MSVC analog and are deliberately absent. See
# crossplatform.md.
#
# Usage:
#   ./cleanbuild.ps1                    clean-build and ctest the whole portable suite
#   ./cleanbuild.ps1 strings_test.cpp   build and run just that one test
[CmdletBinding()]
param([Parameter(ValueFromRemainingArguments = $true)][string[]] $Rest)

$ErrorActionPreference = 'Stop'

# clang-cl from the standalone LLVM install matches the clang-22 the Linux build
# uses; the MSVC STL and Windows SDK come from Visual Studio (set up below).
$repo = $PSScriptRoot
$clang = 'C:/Program Files/LLVM/bin/clang-cl.exe'
$srcDir = Join-Path $repo 'tests'
$bldDir = Join-Path $repo 'tests/build'

if (-not (Test-Path $clang)) { throw "clang-cl not found at $clang" }

# A *.cpp argument selects a single test; nothing else is recognized yet.
$testName = ''
foreach ($a in $Rest) {
  if ($a -like '*.cpp') { $testName = $a }
  else { throw "Unrecognized argument '$a' (expected a <name>_test.cpp)" }
}

# Bring in the MSVC environment (INCLUDE/LIB so clang-cl finds the STL and SDK)
# unless we are already inside a Developer shell.
if (-not $env:VSCMD_VER) {
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vswhere)) { throw "vswhere not found at $vswhere" }
  $vsPath = & $vswhere -latest -property installationPath
  if (-not $vsPath) { throw 'No Visual Studio installation found' }
  Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
  Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
    -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

# Wipe the build dir for a truly clean configure (matches cleanbuild.sh). The
# persistent Catch2 cache lives in tests/.fetchcontent, so this does not force a
# Catch2 rebuild.
if (Test-Path $bldDir) {
  Write-Host 'Cleaning tests/build ...'
  Remove-Item -Recurse -Force $bldDir
}

# Configure. C++23 comes from CMAKE_CXX_STANDARD; the WIN32 branch in
# tests/CMakeLists.txt selects clang-cl flags and excludes the Linux buckets.
$cfg = @('-S', $srcDir, '-B', $bldDir, '-G', 'Ninja', "-DCMAKE_CXX_COMPILER=$clang")
if ($testName) {
  Write-Host "Configuring single test: $testName"
  $cfg += "-DTEST_NAME=$testName"
} else {
  Write-Host 'Configuring full portable suite'
}
cmake @cfg
if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }

# Build everything, keep-going so all failures surface in one pass.
cmake --build $bldDir -- -k 0
if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }

# Run the tests.
ctest --test-dir $bldDir --output-on-failure
exit $LASTEXITCODE
