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
#   ./cleanbuild.ps1 tidy                run clang-tidy during the build, then a
#                                        warning summary (clang++ only)
#   ./cleanbuild.ps1 cudacheck           CUDA tests under compute-sanitizer (the
#                                        device analog of asan; memcheck etc.)
[CmdletBinding()]
param([Parameter(ValueFromRemainingArguments = $true)][string[]] $Rest)

$ErrorActionPreference = 'Stop'

$repo = $PSScriptRoot
$srcDir = Join-Path $repo 'tests'
$bldDir = Join-Path $repo 'tests/build'
$llvmRoot = 'C:/Program Files/LLVM'
$clangXx = "$llvmRoot/bin/clang++.exe"

# Parse args: a *.cpp or *.cu name selects one test; clang|cl picks the
# compiler; asan picks the sanitizer; tidy runs clang-tidy during the build;
# cudacheck runs the CUDA tests under compute-sanitizer (the device analog of asan).
$testName = ''
$compiler = 'clang'
$sanitizer = ''
$cudacheck = $false
$tidy = $false
foreach ($a in $Rest) {
  switch -Regex ($a) {
    '\.(cpp|cu)$' { $testName = $a }
    '^(clang\+\+|clang)$' { $compiler = 'clang' }
    '^(cl|msvc)$' { $compiler = 'cl' }
    '^asan$' { $sanitizer = $a }
    '^cudacheck$' { $cudacheck = $true }
    '^(tidy|--tidy)$' { $tidy = $true }
    default { throw "Unrecognized argument '$a' (expected <name>_test.cpp, <name>_test.cu, clang|cl, asan, tidy, or cudacheck)" }
  }
}
if ($sanitizer -and $compiler -eq 'cl') {
  throw 'ASAN requires clang++, not MSVC cl.'
}
if ($cudacheck -and $sanitizer) {
  throw 'cudacheck and asan are separate modes (host ASAN vs device compute-sanitizer).'
}
if ($cudacheck -and $compiler -eq 'cl') {
  throw 'cudacheck needs clang++ (CUDA does not build under cl).'
}
# tidy mirrors Linux: clang-tidy is clang-based, so it rides the clang++ build,
# not cl; and it is its own analysis mode, exclusive with the sanitizer/check modes.
if ($tidy -and $compiler -eq 'cl') {
  throw 'tidy requires clang++ (clang-tidy is the clang analyzer; cl has no analog here).'
}
if ($tidy -and ($sanitizer -or $cudacheck)) {
  throw 'tidy is exclusive with asan/cudacheck (separate analysis passes).'
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
if ($cudacheck -and -not $cudaArgs) {
  throw 'cudacheck needs the CUDA toolkit (nvcc + a GPU) in the default clang++ build.'
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
if ($tidy) {
  $clangTidy = "$llvmRoot/bin/clang-tidy.exe"
  if (-not (Test-Path $clangTidy)) { throw "clang-tidy not found at $clangTidy" }
  # LLVM is not on PATH (we use full tool paths), so the find_program in
  # tests/CMakeLists.txt would miss clang-tidy; pre-seed the cache var it reads.
  #
  # Keep asserts live, like the Linux tidy run, so assert facts still reach
  # clang-analyzer: override the Release flags to drop the default -DNDEBUG
  # (keeping -O so the /MD CRT and layout match the normal Release build; the
  # CRT comes from -fms-runtime-lib=dll, not the build type). The build type
  # MUST stay Release: an empty build type makes the GNU-clang platform pull the
  # debug CRT (msvcrtd), which then fails to link against the /MD Catch2.
  $cfg += @('-DUSE_CLANG_TIDY=ON', "-DCLANG_TIDY_EXE=$clangTidy",
    '-DCMAKE_CXX_FLAGS_RELEASE=-O2')
}
$cfg += $cudaArgs
$mode = if ($tidy) { 'tidy' } elseif ($cudacheck) { 'cudacheck' } elseif ($sanitizer) { $sanitizer } else { 'plain' }
$cfgLabel = if ($tidy) { 'Release, asserts-live' } else { 'Release' }
Write-Host "Compiler: $compiler   Mode: $mode ($cfgLabel)$(if ($testName) { "   Test: $testName" })"

# Signature of everything that determines the configuration (the configure args
# themselves). When a single-file build is requested and the build dir already
# carries a matching signature, the configuration cannot have changed, so skip
# the cold reconfigure (the ~20s of nvcc/find_package probing that would only
# reproduce identical build files) and rebuild just that target. A full build, a
# changed option, a different file, or a first run all fall through to configure.
# Editing CMakeLists.txt still reconfigures: `cmake --build` re-runs CMake when
# the lists file is newer than the cache. Mirrors cleanbuild.sh.
$sigFile = Join-Path $bldDir '.cleanbuild-config'
$configSig = ($cfg -join '|')
$reuse = $testName -and (Test-Path $sigFile) -and
  (Test-Path (Join-Path $bldDir 'CMakeCache.txt')) -and
  ((Get-Content $sigFile -Raw).Trim() -eq $configSig)

if ($reuse) {
  Write-Host "Reusing configured build dir for $testName (config unchanged); skipping reconfigure."
  # "Clean" still means a fresh executable: drop just this target's binary so it
  # relinks, leaving the cached objects and configuration in place.
  $stem = [IO.Path]::GetFileNameWithoutExtension($testName)
  Remove-Item (Join-Path $bldDir "release_bin/$stem.exe") -Force -ErrorAction SilentlyContinue
} else {
  cmake @cfg
  if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }
  # Record the signature so the next matching single-file run can reuse this.
  Set-Content -Path $sigFile -Value $configSig
}

# The .cu test stems cudacheck will sanitize: one if a .cu was named, else all
# registered cuda/*.cu (notest_ excluded). Also scopes the build, so cudacheck
# does not rebuild the whole portable suite just to check the CUDA bucket.
$cudaTests = @()
if ($cudacheck) {
  if ($testName -like '*.cu') {
    $cudaTests = @([IO.Path]::GetFileNameWithoutExtension($testName))
  } else {
    $cudaTests = @(Get-ChildItem (Join-Path $srcDir 'cuda') -Filter '*.cu' |
      Where-Object { $_.Name -notmatch '^notest_' } | ForEach-Object { $_.BaseName })
  }
}

# Build, keep-going so all failures surface in one pass. cudacheck (without a
# single named test) builds only the cuda targets. tidy tees the build output to
# a log (clang-tidy warnings stream during compilation) and does NOT throw on a
# nonzero result: a .clang-tidy WarningsAsErrors fails the build, but we still
# want to reach the summary, and a real compile break surfaces via ctest below.
$tidyLog = Join-Path $bldDir 'tidy.log'
if ($tidy) {
  cmake --build $bldDir -- -k 0 2>&1 | Tee-Object $tidyLog
} elseif ($cudacheck -and -not $testName) {
  cmake --build $bldDir --target $cudaTests -- -k 0
  if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }
} else {
  cmake --build $bldDir -- -k 0
  if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }
}

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

# cudacheck: run each registered cuda test under compute-sanitizer (the device
# analog of ASAN), instead of plain ctest. The four tools are cheap on kernels
# that use no shared memory or barriers (racecheck/synccheck become no-ops
# there). --error-exitcode 1 makes a device fault fail the run; the build's
# -gline-tables-only maps a fault to its source line.
if ($cudacheck) {
  $cudaRoot = Split-Path (Split-Path (Get-Command nvcc).Source)
  $cs = Join-Path $cudaRoot 'compute-sanitizer/compute-sanitizer.exe'
  if (-not (Test-Path $cs)) { throw "compute-sanitizer not found at $cs" }
  $tools = @('memcheck', 'racecheck', 'synccheck', 'initcheck')
  $failed = @()
  $checked = 0
  foreach ($t in $cudaTests) {
    $exe = Join-Path $bldDir "release_bin/$t.exe"
    if (-not (Test-Path $exe)) { $failed += "$t (not built)"; continue }
    foreach ($tool in $tools) {
      $out = & $cs --tool $tool --error-exitcode 1 $exe 2>&1
      $rc = $LASTEXITCODE
      # A test that launches no kernel and touches no device memory (host-only,
      # or device functions exercised on the host) makes no instrumentable CUDA
      # call. compute-sanitizer flags that; it is a skip, not a device fault.
      if ($out | Select-String 'terminated before first instrumented API call') {
        Write-Host ("  {0,-22} {1}" -f $t, 'skip - no device activity to check')
        break
      }
      $summary = ($out | Select-String 'SUMMARY' | Select-Object -Last 1).Line -replace '^=+ ', ''
      $status = if ($rc -eq 0) { 'ok' } else { 'FAIL' }
      Write-Host ("  {0,-22} {1,-10} {2,-5} {3}" -f $t, $tool, $status, $summary)
      $checked++
      if ($rc -ne 0) {
        $failed += "$t/$tool"
        $out | Select-String '=========' |
          Where-Object { $_.Line -notmatch 'SUMMARY|COMPUTE-SANITIZER' } |
          Select-Object -First 6 | ForEach-Object { Write-Host "      $($_.Line)" }
      }
    }
  }
  if ($failed) { Write-Host "cudacheck FAILED: $($failed -join ', ')"; exit 1 }
  Write-Host "cudacheck clean: $checked tool-run(s) across $($cudaTests.Count) test(s), no device errors."
  exit 0
}

# notest_* sources opt out of CTest, so a build-everything run skips them (the
# point of the prefix). But an explicit single-file request means "build AND run
# this one", so run the binary directly, mirroring cleanbuild.sh.
$stem = if ($testName) { [IO.Path]::GetFileNameWithoutExtension($testName) } else { '' }
if ($stem -like 'notest_*') {
  $bin = Join-Path $bldDir "release_bin/$stem.exe"
  if (Test-Path $bin) {
    Write-Host "Running $bin"
    & $bin
    $ctestRc = $LASTEXITCODE
  } else {
    Write-Error "built binary not found: $bin"
    $ctestRc = 1
  }
} else {
  ctest --test-dir $bldDir --output-on-failure
  $ctestRc = $LASTEXITCODE
}

# tidy mode: clang-tidy warnings streamed past during the build and got buried
# under the ctest run. Summarize them at the end so the bottom-line state is
# visible, mirroring cleanbuild.sh. Filter out .fetchcontent (the FetchContent'd
# Catch2) so only Corvid-owned findings appear; group by check.
if ($tidy) {
  Write-Host ''
  Write-Host '==================== clang-tidy summary ===================='
  $warn = @(Select-String -Path $tidyLog -Pattern ': warning: .*\[[A-Za-z][A-Za-z0-9._-]*\]' |
    Where-Object { $_.Line -notmatch '\.fetchcontent' } | ForEach-Object { $_.Line })
  if ($warn.Count) {
    Write-Host "$($warn.Count) warning(s):"
    Write-Host ''
    $warn | ForEach-Object { Write-Host $_ }
    Write-Host ''
    Write-Host 'By check:'
    $warn | ForEach-Object { if ($_ -match '(\[[A-Za-z][A-Za-z0-9._-]*\])\s*$') { $matches[1] } } |
      Group-Object | Sort-Object Count -Descending |
      ForEach-Object { Write-Host ('  {0,5}  {1}' -f $_.Count, $_.Name) }
  } else {
    Write-Host 'clean: no warnings'
  }
  Write-Host ''
  Write-Host "Full log: $tidyLog"
  Write-Host '============================================================'
}

exit $ctestRc
