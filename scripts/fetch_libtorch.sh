#!/bin/bash
# Fetch the prebuilt LibTorch (release, CUDA) drop into tests/.local/libtorch/.
# One-time setup, the Linux half of scripts/fetch_libtorch.ps1; the stage 6
# build findings in corvid/llm/roadmap.md explain the choices. The zip is the
# cxx11-ABI libstdc++ build, which is what the `.cu` bucket links (the toolkit
# refuses libc++ on x86), so a torch TU lives in that bucket. Idempotent:
# skips the download if the install already exists. Delete that directory to
# force a fresh fetch.
set -euo pipefail

version=2.14.1
# cu130 is the lowest CUDA 13 build; minor-version compatibility covers the
# 13.4 toolkit here. cu132 is the alternative if the spike wants it.
cuda=cu130
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
local_dir="$repo_root/tests/.local"
dest="$local_dir/libtorch"
config="$dest/share/cmake/Torch/TorchConfig.cmake"

if [ -f "$config" ]; then
    echo "LibTorch $version+$cuda already present at $dest"
    exit 0
fi

zip_name="libtorch-shared-with-deps-$version+$cuda.zip"
# The '+' in the version is percent-encoded in the download URL.
url="https://download.pytorch.org/libtorch/$cuda/libtorch-shared-with-deps-$version%2B$cuda.zip"
# Staged beside the destination so the final move is a rename.
tmp="$local_dir/libtorch-fetch"
rm -rf "$tmp"
mkdir -p "$tmp"

zip_path="$tmp/$zip_name"
echo "Downloading $url"
curl --fail --location --progress-bar --output "$zip_path" "$url"

echo "Extracting to $dest"
unzip -q "$zip_path" -d "$tmp"

# The zip expands to a libtorch/ folder; move it into place and drop the zip.
mv "$tmp/libtorch" "$dest"
rm -rf "$tmp"

if [ ! -f "$config" ]; then
    echo "LibTorch install looks wrong: $config not found after extraction." >&2
    exit 1
fi
echo "LibTorch $version+$cuda installed at $dest"
