# cleanbuild.ps1 - Windows build driver for the portable (and, later, CUDA) test
# buckets. Mirrors cleanbuild.sh's role on Linux: configure a fresh, optimized
# (release) build against the MSVC STL and run ctest. Like the Linux clang/gcc
# split, the compiler is selectable: clang-cl (default) or MSVC cl. ASAN is
# supported under clang-cl, and it carries UBSAN with it
# (-fsanitize=address,undefined): standalone UBSAN ships a static-CRT-only
# runtime that collides with this /MD build, so it is not a separate Windows
# mode. The other Linux-only modes (libc++/libstdc++ choice, tsan/msan,
# analyze-build scan, llvm-cov coverage, compiler-rt/lld) have no MSVC analog and
# are deliberately absent. See crossplatform.md.
#
# Usage (args combine in any order):
#   ./cleanbuild.ps1                     clang-cl, whole portable suite, ctest
#   ./cleanbuild.ps1 strings_test.cpp    build and run just that one test
#   ./cleanbuild.ps1 cl                  build with MSVC cl instead of clang-cl
#   ./cleanbuild.ps1 asan                ASAN (+UBSAN) build + ctest (clang-cl only)
[CmdletBinding()]
param([Parameter(ValueFromRemainingArguments = $true)][string[]] $Rest)

$ErrorActionPreference = 'Stop'

$repo = $PSScriptRoot
$srcDir = Join-Path $repo 'tests'
$bldDir = Join-Path $repo 'tests/build'
$llvmRoot = 'C:/Program Files/LLVM'
$clangCl = "$llvmRoot/bin/clang-cl.exe"

# Parse args: a *.cpp name selects one test; clang-cl|cl picks the compiler;
# asan picks the sanitizer.
$testName = ''
$compiler = 'clang-cl'
$sanitizer = ''
foreach ($a in $Rest) {
  switch -Regex ($a) {
    '\.cpp$' { $testName = $a }
    '^(clang-cl|clangcl|clang)$' { $compiler = 'clang-cl' }
    '^(cl|msvc)$' { $compiler = 'cl' }
    '^asan$' { $sanitizer = $a }
    default { throw "Unrecognized argument '$a' (expected <name>_test.cpp, clang-cl|cl, or asan)" }
  }
}
if ($sanitizer -and $compiler -eq 'cl') {
  throw 'ASAN requires clang-cl, not MSVC cl.'
}

# Resolve the compiler. clang-cl uses the full LLVM path; cl rides the dev-shell
# PATH set up below.
if ($compiler -eq 'clang-cl') {
  if (-not (Test-Path $clangCl)) { throw "clang-cl not found at $clangCl" }
  $cxx = $clangCl
} else {
  $cxx = 'cl'
}

# Bring in the MSVC environment (INCLUDE/LIB, and cl/link on PATH) unless we are
# already inside a Developer shell.
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

# Configure fresh. `cmake --fresh` clears the cache and CMakeFiles/ and
# reconfigures in place WITHOUT deleting tests/build - on Windows clangd keeps an
# open handle to compile_commands.json, which would block a directory wipe (a
# Remove-Item there fails with "in use"). The persistent Catch2 cache under
# tests/.fetchcontent is untouched, so this does not re-clone Catch2. The WIN32
# branch in tests/CMakeLists.txt picks the compiler flags and excludes the Linux
# buckets; Release gives /O2 + NDEBUG + the /MD CRT (ide_build.ps1 is the debug
# path).
$cfg = @('--fresh', '-S', $srcDir, '-B', $bldDir, '-G', 'Ninja',
  "-DCMAKE_CXX_COMPILER=$cxx", '-DCMAKE_BUILD_TYPE=Release')
if ($testName) { $cfg += "-DTEST_NAME=$testName" }
if ($sanitizer) { $cfg += "-DSANITIZER=$sanitizer" }
$mode = if ($sanitizer) { $sanitizer } else { 'plain' }
Write-Host "Compiler: $compiler   Mode: $mode (Release)$(if ($testName) { "   Test: $testName" })"
cmake @cfg
if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }

# Build everything, keep-going so all failures surface in one pass.
cmake --build $bldDir -- -k 0
if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }

# ASAN links the dynamic runtime, whose DLL must be on PATH for the test exes to
# start; llvm-symbolizer (in LLVM/bin) gives readable stack traces. UBSAN's
# runtime is static, so it needs nothing here. detect_odr_violation=0 silences
# ASAN's ODR checker, which false-positives on the linker pooling identical
# string literals (e.g. the u"1" UTF-16 literal in the string tests).
if ($sanitizer -eq 'asan') {
  $asanDll = Get-ChildItem "$llvmRoot/lib/clang/*/lib/windows/clang_rt.asan_dynamic-x86_64.dll" -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if ($asanDll) {
    $env:PATH = "$($asanDll.DirectoryName);$llvmRoot/bin;$env:PATH"
  } else {
    Write-Warning "ASAN runtime DLL not found under $llvmRoot/lib/clang; tests may fail to start."
  }
  $env:ASAN_OPTIONS = 'detect_odr_violation=0'
}

ctest --test-dir $bldDir --output-on-failure
exit $LASTEXITCODE
