# Set the environment variables to use clang. When running on Codex, use the
# default clang and libstdc++.
if ($Env:CODEX_PROXY_CERT) {
    Write-Host "Codex environment detected: using libstdc++"
    $env:CC = (Get-Command clang).Source
    $env:CXX = (Get-Command clang++).Source
    $env:RC = (Get-Command clang).Source
    $libStdOption = "-DUSE_LIBSTDCPP=ON"
} else {
    $env:CC="c:/program files/llvm/bin/clang"
    $env:CXX="c:/program files/llvm/bin/clang++"

    # This is a crude lie to shut it up about resource compilers.
    $env:RC="c:/program files/llvm/bin/clang"
    $libStdOption = "-DUSE_LIBSTDCPP=OFF"
}

# Define the build directory (assuming you're using an out-of-source build)
$buildDir = "build"

# If the build directory exists, delete it to clean the build
if (Test-Path $buildDir) {
    Write-Host "Cleaning the build directory..."
    Remove-Item -Recurse -Force $buildDir
} else {
    Write-Host "Build directory not found. Creating a new one."
}

# Create the build directory
New-Item -ItemType Directory -Path $buildDir

# Navigate to the build directory
Set-Location $buildDir

# Run cmake to configure the project with Ninja (or MinGW Makefiles) and clang
cmake -G "Ninja" .. $libStdOption

# Run the build (this will compile everything from scratch)
cmake --build . --config Release

# Navigate back to the original directory after building
Set-Location ..
