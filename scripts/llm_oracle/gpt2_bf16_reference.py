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
"""What GPT-2 loses in bf16, as transformers runs it, on the oracle's prompts.

The device engine's bf16 gate has a tolerance measured against the fp32
oracle. This prints the same measurement for the reference implementation run
wholly in bf16 (residual stream included), so the gate's tolerance can be read
against what bf16 costs elsewhere: per prompt, the largest logit error, the
largest logit it is measured against, and the largest relative error over
logits of magnitude above 1; then how many greedy picks the bf16 run shares
with the fp32 one on the bisect prompt.

Runs offline on the CPU from the cached model, in the oracle's venv:

  tests/.local/llm/venv/Scripts/python scripts/llm_oracle/gpt2_bf16_reference.py
"""

import os
import sys
from pathlib import Path

os.environ.setdefault("HF_HUB_DISABLE_XET", "1")
os.environ.setdefault("HF_HUB_OFFLINE", "1")
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch
from gpt2_oracle import BISECT_PROMPT, GREEDY_TOKENS, MODEL_ID, PROMPTS
from transformers import GPT2LMHeadModel, GPT2TokenizerFast


def main() -> None:
    tok = GPT2TokenizerFast.from_pretrained(MODEL_ID)
    f32 = GPT2LMHeadModel.from_pretrained(MODEL_ID, torch_dtype=torch.float32)
    bf16 = GPT2LMHeadModel.from_pretrained(MODEL_ID, torch_dtype=torch.bfloat16)
    f32.eval()
    bf16.eval()

    with torch.no_grad():
        for i, prompt in enumerate(PROMPTS):
            ids = tok(prompt, return_tensors="pt").input_ids
            expected = f32(ids).logits[0].float()
            actual = bf16(ids).logits[0].float()
            error = (expected - actual).abs()
            magnitude = expected.abs()
            big = magnitude > 1
            rel = (error[big] / magnitude[big]).max().item()
            print(f"prompt_{i}: max abs error {error.max():.4f} against a largest "
                  f"{magnitude.max():.2f}, max rel error over |x| > 1 {rel:.4f}")

        ids = tok(PROMPTS[BISECT_PROMPT], return_tensors="pt").input_ids
        prompt_count = ids.shape[1]
        picks = [
            model.generate(ids, max_new_tokens=GREEDY_TOKENS, do_sample=False)[
                0, prompt_count:].tolist()
            for model in (f32, bf16)
        ]
        followed = 0
        for a, b in zip(*picks):
            if a != b:
                break
            followed += 1
        print(f"greedy: bf16 follows fp32 for {followed} of {len(picks[0])} IDs")
        print("fp32:", repr(tok.decode(picks[0])))
        print("bf16:", repr(tok.decode(picks[1])))


if __name__ == "__main__":
    main()
