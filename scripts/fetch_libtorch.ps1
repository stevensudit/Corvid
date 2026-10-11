# Fetch the prebuilt LibTorch (release, CUDA) drop into tests/.local/libtorch/.
# One-time setup, the Windows half of scripts/fetch_libtorch.sh; the stage 6
# build findings in corvid/llm/roadmap.md explain the choices. The release zip
# is the right one in every Corvid configuration, the IDE debug build
# included: every Windows bucket links the release CRT (-fms-runtime-lib=dll),
# and the CRT is the only thing the debug/release LibTorch ABI split is about.
# Idempotent: skips the download if the install already exists. Delete that
# directory to force a fresh fetch.
$ErrorActionPreference = 'Stop'

$version = '2.14.1'
# cu130 is the lowest CUDA 13 build; minor-version compatibility covers the
# 13.3 toolkit here. cu132 is the alternative if the spike wants it.
$cuda = 'cu130'
$repoRoot = Split-Path -Parent $PSScriptRoot
$local = Join-Path $repoRoot 'tests/.local'
$dest = Join-Path $local 'libtorch'
$config = Join-Path $dest 'share/cmake/Torch/TorchConfig.cmake'

if (Test-Path $config) {
    Write-Host "LibTorch $version+$cuda already present at $dest"
    exit 0
}

$zipName = "libtorch-win-shared-with-deps-$version+$cuda.zip"
# The '+' in the version is percent-encoded in the download URL.
$url = "https://download.pytorch.org/libtorch/$cuda/libtorch-win-shared-with-deps-$version%2B$cuda.zip"
# Staged beside the destination (not in TEMP) so the final move is a rename on
# the same volume and the 4 GB zip never crosses drives.
$tmp = Join-Path $local 'libtorch-fetch'
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

# curl.exe and tar.exe (bsdtar, which reads zip) ship with Windows; both handle
# a multi-gigabyte archive where Invoke-WebRequest and Expand-Archive crawl.
$zipPath = Join-Path $tmp $zipName
Write-Host "Downloading $url"
curl.exe --fail --location --progress-bar --output $zipPath $url
if ($LASTEXITCODE -ne 0) { throw "Download failed with exit code $LASTEXITCODE" }

Write-Host "Extracting to $dest"
tar.exe -xf $zipPath -C $tmp
if ($LASTEXITCODE -ne 0) { throw "Extraction failed with exit code $LASTEXITCODE" }

# The zip expands to a libtorch/ folder; move it into place and drop the zip.
Move-Item -Path (Join-Path $tmp 'libtorch') -Destination $dest
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue

if (-not (Test-Path $config)) {
    throw "LibTorch install looks wrong: $config not found after extraction."
}
Write-Host "LibTorch $version+$cuda installed at $dest"
