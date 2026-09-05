#!/usr/bin/env bash
# Create the oracle venv under tests/.local/llm/venv and install its
# dependencies. Run once per container; the venv lives on the workspace
# mount, so it survives a container rebuild.
#
# The firewall allowlist has download.pytorch.org (the torch CPU wheel
# index) and download-r2.pytorch.org (where that index sends the wheel
# bytes), plus pypi.org and files.pythonhosted.org (everything else). The
# oracle runs on the CPU: GPT-2 124M in fp32 takes seconds there, and the
# CPU wheel is a fraction of the size of the CUDA one.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VENV="$ROOT/tests/.local/llm/venv"

python3 -m venv "$VENV"
"$VENV/bin/pip" install --upgrade pip
"$VENV/bin/pip" install --index-url https://download.pytorch.org/whl/cpu torch
"$VENV/bin/pip" install -r "$ROOT/scripts/llm_oracle/requirements.txt"

echo
echo "Oracle venv ready. Generate the GPT-2 artifacts with:"
echo "  $VENV/bin/python $ROOT/scripts/llm_oracle/gpt2_oracle.py"
