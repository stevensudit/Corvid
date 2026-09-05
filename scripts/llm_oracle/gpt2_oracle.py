#!/usr/bin/env python3
"""Produce the GPT-2 124M reference artifacts for corvid/cuda/llm.

This is the only Python in the LLM quest. It reads the published weights
through PyTorch and Hugging Face once and writes everything the C++ side
diffs against. It never runs inference for the project; it is the oracle,
not the engine.

Outputs (paths relative to the repository root):

tests/data/llm/gpt2/           small, committed
  vocab.json, merges.txt         the tokenizer tables, copied from the hub
  corpus_tokens.json             reference encoding of corpus.txt
  manifest.json                  shapes, config, prompts, checksums,
                                 greedy continuation, tool versions

tests/.local/llm/gpt2/         large, gitignored
  model.safetensors              the weights, copied from the hub
  logits.safetensors             per prompt: input_ids and fp32 logits
  activations.safetensors        every sublayer boundary for the bisect prompt
  grads.safetensors              loss and every parameter gradient for one
                                 batch drawn from the corpus

All reference tensors are fp32, computed on the CPU in eval mode with
deterministic algorithms, so the numbers are reproducible bit for bit on
the same torch version. The manifest records that version.
"""

import argparse
import hashlib
import json
import os
import platform
import shutil
import sys
from pathlib import Path

# Fetch through plain HTTP rather than the xet transport that hf_xet enables
# by default. The plain path is one redirect from huggingface.co to the CDN
# host in the container's firewall allowlist (us.aws.cdn.hf.co); xet would
# add cas-server.xethub.hf.co, another rotating-address host to pin. Must be
# set before huggingface_hub is imported.
os.environ.setdefault("HF_HUB_DISABLE_XET", "1")

import torch
from huggingface_hub import hf_hub_download
from safetensors.torch import save_file
from transformers import GPT2LMHeadModel, GPT2TokenizerFast

MODEL_ID = "openai-community/gpt2"
ROOT = Path(__file__).resolve().parents[2]

# Fixed prompts. Index 1 is the bisect prompt: moderate length, plain text.
PROMPTS = [
    "Hello",
    "In 1969, humans first landed on the Moon. The mission was called",
    "The quick brown fox jumps over the lazy dog.",
    "def fibonacci(n):\n    if n < 2:\n        return n\n",
    "Unicode: na\u00efve caf\u00e9, \u00fcber, \u65e5\u672c\u8a9e, and emoji \U0001F680.",
]
BISECT_PROMPT = 1
GREEDY_TOKENS = 20

# Gradient batch: B sequences of T tokens, consecutive slices of the corpus.
GRAD_BATCH = 4
GRAD_SEQ = 64


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(filename: str, dest: Path) -> str:
    """Copy one file of the model repo into dest and return its revision."""
    cached = Path(hf_hub_download(MODEL_ID, filename))
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(cached, dest)
    # The cache layout is .../snapshots/<commit sha>/<filename>.
    return cached.parent.name


def tokenizer_fixtures(tok: GPT2TokenizerFast, fixtures: Path) -> dict:
    corpus_path = fixtures / "corpus.txt"
    text = corpus_path.read_text(encoding="utf-8")
    lines = text.split("\n")
    full = tok.encode(text)
    per_line = [tok.encode(line) for line in lines]
    (fixtures / "corpus_tokens.json").write_text(
        json.dumps({"full": full, "lines": per_line}, separators=(",", ":"))
        + "\n",
        encoding="utf-8",
    )
    # Decode must round-trip byte for byte; the tokenizer is byte-level.
    assert tok.decode(full) == text, "reference tokenizer failed round-trip"
    return {"corpus_tokens": len(full), "corpus_lines": len(lines)}


class Capture:
    """Forward hooks that record sublayer inputs and outputs by name."""

    def __init__(self):
        self.tensors: dict[str, torch.Tensor] = {}
        self.handles = []

    def tap(self, module, name: str, *, inputs=False, outputs=True):
        def hook(_mod, args, out):
            if inputs:
                self.tensors[name + "/in"] = args[0].detach().squeeze(0).clone()
            if outputs:
                o = out[0] if isinstance(out, tuple) else out
                self.tensors[name + "/out"] = o.detach().squeeze(0).clone()

        self.handles.append(module.register_forward_hook(hook))

    def release(self):
        for h in self.handles:
            h.remove()


def prompt_artifacts(model, tok, out: Path) -> dict:
    logits = {}
    activations = {}
    info = []
    for i, prompt in enumerate(PROMPTS):
        ids = tok(prompt, return_tensors="pt").input_ids
        cap = Capture()
        if i == BISECT_PROMPT:
            t = model.transformer
            # drop is the identity in eval mode; its output is wte + wpe.
            cap.tap(t.drop, "embed")
            for n, block in enumerate(t.h):
                # ln_1 input is the residual stream entering the block.
                cap.tap(block.ln_1, f"block_{n}/ln_1", inputs=True)
                cap.tap(block.attn, f"block_{n}/attn")
                # ln_2 input is the residual stream after the attention add.
                cap.tap(block.ln_2, f"block_{n}/ln_2", inputs=True)
                cap.tap(block.mlp, f"block_{n}/mlp")
            # ln_f input is the residual stream leaving the last block.
            cap.tap(t.ln_f, "ln_f", inputs=True)
        with torch.no_grad():
            out_logits = model(ids).logits.squeeze(0)
        cap.release()
        logits[f"prompt_{i}/input_ids"] = ids.squeeze(0).to(torch.int32)
        logits[f"prompt_{i}/logits"] = out_logits.contiguous()
        if i == BISECT_PROMPT:
            activations = cap.tensors
            activations["logits"] = out_logits.contiguous()
        info.append({"text": prompt, "tokens": ids.shape[1]})
    save_file(logits, str(out / "logits.safetensors"))
    save_file(activations, str(out / "activations.safetensors"))
    return {"prompts": info, "bisect_prompt": BISECT_PROMPT}


def greedy(model, tok) -> dict:
    ids = tok(PROMPTS[BISECT_PROMPT], return_tensors="pt").input_ids
    with torch.no_grad():
        gen = model.generate(
            ids,
            max_new_tokens=GREEDY_TOKENS,
            do_sample=False,
            pad_token_id=tok.eos_token_id,
        )
    new = gen[0, ids.shape[1] :].tolist()
    return {"prompt": BISECT_PROMPT, "tokens": new, "text": tok.decode(new)}


def grad_artifacts(model, corpus_tokens: list[int], out: Path) -> dict:
    need = GRAD_BATCH * GRAD_SEQ
    if len(corpus_tokens) < need:
        sys.exit(f"corpus has {len(corpus_tokens)} tokens; need {need}")
    ids = torch.tensor(corpus_tokens[:need]).view(GRAD_BATCH, GRAD_SEQ)
    model.zero_grad(set_to_none=True)
    # HF shifts labels internally: position t predicts token t+1.
    loss = model(ids, labels=ids).loss
    loss.backward()
    grads = {"input_ids": ids.to(torch.int32), "loss": loss.detach().view(1)}
    for name, p in model.named_parameters():
        assert p.grad is not None, name
        grads[name] = p.grad.detach().contiguous().clone()
    save_file(grads, str(out / "grads.safetensors"))
    return {"batch": GRAD_BATCH, "seq": GRAD_SEQ, "loss": loss.item()}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--fixtures", type=Path, default=ROOT / "tests/data/llm/gpt2")
    ap.add_argument("--out", type=Path, default=ROOT / "tests/.local/llm/gpt2")
    args = ap.parse_args()
    fixtures, out = args.fixtures, args.out
    out.mkdir(parents=True, exist_ok=True)

    torch.manual_seed(0)
    torch.use_deterministic_algorithms(True)

    revision = fetch("vocab.json", fixtures / "vocab.json")
    fetch("merges.txt", fixtures / "merges.txt")
    fetch("model.safetensors", out / "model.safetensors")

    tok = GPT2TokenizerFast.from_pretrained(MODEL_ID)
    model = GPT2LMHeadModel.from_pretrained(MODEL_ID, torch_dtype=torch.float32)
    model.eval()
    cfg = model.config

    manifest = {
        "model": MODEL_ID,
        "revision": revision,
        "config": {
            "n_layer": cfg.n_layer,
            "n_head": cfg.n_head,
            "n_embd": cfg.n_embd,
            "n_ctx": cfg.n_positions,
            "vocab_size": cfg.vocab_size,
            "layer_norm_epsilon": cfg.layer_norm_epsilon,
        },
        # HF GPT-2 stores projection weights as Conv1D: shape
        # [in_features, out_features], applied as y = x @ W + b. This is the
        # transpose of nn.Linear's [out, in]. The attention c_attn projects to
        # 3 * n_embd, laid out as q, k, v blocks in that order.
        "weight_layout": "conv1d: W is [in, out], y = x @ W + b",
        "dtype": "float32",
        "tokenizer": tokenizer_fixtures(tok, fixtures),
        "activations": (
            "for the bisect prompt, sequence-major [T, C]: embed/out, then per "
            "block ln_1/in (residual in), attn/out, ln_2/in (residual after "
            "attention), mlp/out; then ln_f/in (residual out) and logits [T, V]"
        ),
    }
    manifest.update(prompt_artifacts(model, tok, out))
    manifest["greedy"] = greedy(model, tok)
    corpus_tokens = json.loads(
        (fixtures / "corpus_tokens.json").read_text(encoding="utf-8")
    )["full"]
    manifest["grads"] = grad_artifacts(model, corpus_tokens, out)

    import safetensors
    import transformers

    manifest["versions"] = {
        "python": platform.python_version(),
        "torch": torch.__version__,
        "transformers": transformers.__version__,
        "safetensors": safetensors.__version__,
    }
    manifest["sha256"] = {
        str(p.relative_to(ROOT)): sha256(p)
        for p in [
            fixtures / "vocab.json",
            fixtures / "merges.txt",
            fixtures / "corpus.txt",
            fixtures / "corpus_tokens.json",
            out / "model.safetensors",
            out / "logits.safetensors",
            out / "activations.safetensors",
            out / "grads.safetensors",
        ]
    }
    (fixtures / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(f"greedy: {manifest['greedy']['text']!r}")
    print(f"loss: {manifest['grads']['loss']:.6f}")
    print(f"wrote {fixtures} and {out}")


if __name__ == "__main__":
    main()
