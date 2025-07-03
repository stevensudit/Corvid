# Choose which standard library to use. Pass 'libstdcpp' or 'libcxx' as the
# first argument to override the default. Outside of Codex, the default is
# libc++.
param(
    [string]$StdLib
)

if ($StdLib -and $StdLib -ne 'libstdcpp' -and $StdLib -ne 'libcxx') {
    Write-Host "Usage: cleanbuild.ps1 [libstdcpp|libcxx]" -ForegroundColor Red
    exit 1
}

if (-not $StdLib) {
    if ($Env:CODEX_PROXY_CERT) {
        $StdLib = 'libstdcpp'
    } else {
        $StdLib = 'libcxx'
    }
}

if ($StdLib -eq 'libstdcpp') {
    Write-Host "Using libstdc++"
    $env:CC = (Get-Command clang).Source
    $env:CXX = (Get-Command clang++).Source
    $env:RC = (Get-Command clang).Source
    $libStdOption = "-DUSE_LIBSTDCPP=ON"
} else {
    Write-Host "Using libc++"
    $env:CC="c:/program files/llvm/bin/clang"
    $env:CXX="c:/program files/llvm/bin/clang++"

    # This is a crude lie to shut it up about resource compilers.
    $env:RC="c:/program files/llvm/bin/clang"
    $libStdOption = "-DUSE_LIBSTDCPP=OFF"
}

# Define the build directory (assuming you're using an out-of-source build)
$buildDir = "build"
$buildRoot = "$buildDir/cmake"

# If the build directory exists, delete it to clean the build
if (Test-Path $buildDir) {
    Write-Host "Cleaning the build directory..."
    Remove-Item -Recurse -Force $buildDir
} else {
    Write-Host "Build directory not found. Creating a new one."
}

# Create the build directory
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

# Run cmake to configure the project with Ninja and clang
cmake -S tests -B $buildRoot -G "Ninja" $libStdOption

# Run the build (this will compile everything from scratch)
cmake --build $buildRoot --config Release
