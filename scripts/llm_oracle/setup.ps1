# Create the oracle venv under tests/.local/llm/venv and install its
# dependencies. Windows counterpart of setup.sh; run once per machine.
#
# Torch comes from the CPU-only wheel index: the oracle runs on the CPU,
# where GPT-2 124M in fp32 takes seconds, and the CPU wheel is a fraction of
# the size of the CUDA one. Everything else comes from PyPI.
#
# Pass -Python to pick the interpreter (default: the py launcher's 3.12).
param(
  [string]$Python = "py -3.12"
)
$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Venv = Join-Path $Root "tests\.local\llm\venv"

Invoke-Expression "$Python -m venv `"$Venv`""
& "$Venv\Scripts\python.exe" -m pip install --upgrade pip
& "$Venv\Scripts\pip.exe" install --index-url https://download.pytorch.org/whl/cpu torch
& "$Venv\Scripts\pip.exe" install -r "$Root\scripts\llm_oracle\requirements.txt"

Write-Host ""
Write-Host "Oracle venv ready. Generate the GPT-2 artifacts with:"
Write-Host "  $Venv\Scripts\python.exe $Root\scripts\llm_oracle\gpt2_oracle.py"
