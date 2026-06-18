# cleanbuild.ps1 - Windows build driver for the portable and CUDA test
# buckets. Mirrors cleanbuild.sh's role on Linux: configure a fresh, optimized
# (release) build against the MSVC STL and run ctest. Like the Linux clang/gcc
# split, the compiler is selectable: clang++ (default) or MSVC cl. clang++ uses
# a GNU-style driver targeting the MSVC ABI (the same driver as Linux clang),
# and is also what compiles the CUDA bucket. ASAN is supported under clang++,
# and it carries UBSAN with it (-fsanitize=address,undefined): standalone UBSAN
# ships a static-CRT-only runtime that collides with this /MD build, so it is
# not a separate Windows mode. The other Linux-only modes (libc++/libstdc++
# choice, tsan/msan, analyze-build scan, llvm-cov coverage, compiler-rt/lld)
# have no MSVC analog and are deliberately absent. See crossplatform.md.
#
# CUDA: when the CUDA toolkit is present and the build is the default clang++
# one in a plain (not asan) mode, the cuda/*.cu tests build alongside the
# portable suite as their own clang++ executables (MSVC ABI). CUDA does not ride
# with the cl build (CMake forbids a cl host + clang++ CUDA mix). Pass a *.cu
# filename to build and run just one. See crossplatform.md.
#
# Usage (args combine in any order):
#   ./cleanbuild.ps1                     clang++, portable + CUDA suite, ctest
#   ./cleanbuild.ps1 strings_test.cpp    build and run just that one test
#   ./cleanbuild.ps1 cuda_status_test.cu build and run just that one CUDA test
#   ./cleanbuild.ps1 cl                  portable suite via MSVC cl (no CUDA)
#   ./cleanbuild.ps1 asan                ASAN (+UBSAN) build + ctest (clang++ only)
[CmdletBinding()]
param([Parameter(ValueFromRemainingArguments = $true)][string[]] $Rest)

$ErrorActionPreference = 'Stop'

$repo = $PSScriptRoot
$srcDir = Join-Path $repo 'tests'
$bldDir = Join-Path $repo 'tests/build'
$llvmRoot = 'C:/Program Files/LLVM'
$clangXx = "$llvmRoot/bin/clang++.exe"

# Parse args: a *.cpp or *.cu name selects one test; clang|cl picks the
# compiler; asan picks the sanitizer.
$testName = ''
$compiler = 'clang'
$sanitizer = ''
foreach ($a in $Rest) {
  switch -Regex ($a) {
    '\.(cpp|cu)$' { $testName = $a }
    '^(clang\+\+|clang)$' { $compiler = 'clang' }
    '^(cl|msvc)$' { $compiler = 'cl' }
    '^asan$' { $sanitizer = $a }
    default { throw "Unrecognized argument '$a' (expected <name>_test.cpp, <name>_test.cu, clang|cl, or asan)" }
  }
}
if ($sanitizer -and $compiler -eq 'cl') {
  throw 'ASAN requires clang++, not MSVC cl.'
}

# Resolve the compiler. clang++ uses the full LLVM path; cl rides the dev-shell
# PATH set up below.
if ($compiler -eq 'clang') {
  if (-not (Test-Path $clangXx)) { throw "clang++ not found at $clangXx" }
  $cxx = $clangXx
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

# CUDA (.cu) targets build automatically when the CUDA toolkit is present and
# the build is the default clang++ one in a plain (non-asan) mode: each .cu is
# its own clang++ executable (MSVC ABI) sharing no link line with a .cpp binary.
# Windows compiles CUDA with clang, not nvcc: nvcc 13.3's MSVC device frontend
# has no C++23 dialect (it drops -std=c++23), so it cannot build Corvid's C++23
# headers, while clang's unified C++23 frontend can. clang-CUDA's `native` arch
# detection does not work on Windows, so resolve the GPU's compute capability
# explicitly via nvidia-smi (e.g. 8.9 -> sm_89). CUDA rides only with clang++,
# not cl: CMake forbids mixing a cl (MSVC) host with a clang++ (GNU) CUDA
# compiler. Skip under asan too: the sanitizer runtime is a .cpp-suite concern
# that would not match the clang-CUDA link. See crossplatform.md.
$cudaArgs = @()
if ($compiler -eq 'clang' -and -not $sanitizer -and
  (Get-Command nvcc -ErrorAction SilentlyContinue)) {
  if (-not (Test-Path $clangXx)) { throw "CUDA needs clang++ at $clangXx" }
  $cc = nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>$null | Select-Object -First 1
  if (-not $cc) { throw 'CUDA toolkit present but no GPU arch from nvidia-smi' }
  $arch = ($cc.Trim() -replace '\.', '')
  $cudaArgs = @('-DCORVID_ENABLE_CUDA=ON', "-DCMAKE_CUDA_COMPILER=$clangXx",
    "-DCMAKE_CUDA_ARCHITECTURES=$arch")
}
# A requested .cu source needs the toolkit, the clang++ compiler, and a plain
# mode; fail clearly rather than configuring a build that produces no target.
if ($testName -like '*.cu' -and -not $cudaArgs) {
  throw "'$testName' needs the CUDA toolkit, the clang++ compiler (not cl), and a plain build mode (not asan)"
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
$cfg += $cudaArgs
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
