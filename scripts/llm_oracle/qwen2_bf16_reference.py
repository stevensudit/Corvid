# Corvid: A general-purpose modern C++ library extending std.
# https://github.com/stevensudit/Corvid
#
# Copyright 2022-2026 Steven Sudit
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""What Qwen2.5-3B-Instruct loses in bf16, as transformers runs it, on the
oracle's prompts.

The torch engine's bf16 gate has a tolerance measured against the fp32
oracle. This prints the same measurement for the reference implementation run
wholly in bf16 (residual stream included), so the gate's tolerance can be read
against what bf16 costs elsewhere: per prompt, the largest logit error, the
largest logit it is measured against, the largest relative error over logits
of magnitude above 1, and how many of the greedy picks the bf16 run shares
with the oracle's; then whether the bf16 run reproduces the manifest's greedy
chat continuation.

Runs offline on the CPU from the oracle's dumps, in the oracle's venv, after
"qwen2_oracle.py" has fetched them:

  tests/.local/llm/venv/Scripts/python scripts/llm_oracle/qwen2_bf16_reference.py
"""

import json
import os
import sys
from pathlib import Path

os.environ.setdefault("HF_HUB_DISABLE_XET", "1")
os.environ.setdefault("HF_HUB_OFFLINE", "1")
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch
from qwen2_oracle import ROOT
from safetensors.torch import load_file
from transformers import AutoModelForCausalLM

DUMPS = ROOT / "tests/.local/llm/qwen2.5-3b-instruct"
MANIFEST = ROOT / "tests/data/llm/qwen2.5-3b-instruct/manifest.json"


def main() -> None:
    logits = load_file(str(DUMPS / "logits.safetensors"))
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    bf16 = AutoModelForCausalLM.from_pretrained(DUMPS, dtype=torch.bfloat16)
    bf16.eval()

    with torch.no_grad():
        for i in range(len(manifest["prompts"])):
            ids = logits[f"prompt_{i}/input_ids"].long().unsqueeze(0)
            expected = logits[f"prompt_{i}/logits"].float()
            actual = bf16(ids).logits[0].float()
            error = (expected - actual).abs()
            magnitude = expected.abs()
            big = magnitude > 1
            rel = (error[big] / magnitude[big]).max().item()
            agreed = (expected.argmax(-1) == actual.argmax(-1)).sum().item()
            print(f"prompt_{i}: max abs error {error.max():.4f} against a largest "
                  f"{magnitude.max():.2f}, max rel error over |x| > 1 {rel:.4f}, "
                  f"greedy agrees on {agreed} of {expected.shape[0]} positions")

        chat_ids = torch.tensor(manifest["chat"]["ids"]).unsqueeze(0)
        expected_picks = manifest["chat"]["greedy"]["tokens"]
        picks = bf16.generate(
            chat_ids, max_new_tokens=len(expected_picks), do_sample=False
        )[0, chat_ids.shape[1]:].tolist()
        print("chat greedy, oracle:", expected_picks)
        print("chat greedy, bf16:  ", picks)


if __name__ == "__main__":
    main()
