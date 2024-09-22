# Set the environment variables to use clang
$env:CC="c:/program files/llvm/bin/clang"
$env:CXX="c:/program files/llvm/bin/clang++"

# This is a crude lie to shut it up about resource compilers.
$env:RC="c:/program files/llvm/bin/clang"

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
cmake -G "Ninja" ..

# Run the build (this will compile everything from scratch)
cmake --build . --config Release

# Navigate back to the original directory after building
Set-Location ..
