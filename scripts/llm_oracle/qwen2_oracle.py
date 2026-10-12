#!/usr/bin/env python3
"""Produce the Qwen2.5-3B-Instruct reference artifacts for corvid/llm.

The stage 6 counterpart of gpt2_oracle.py. It reads the published model
through PyTorch and Hugging Face once and writes what the C++ side diffs
against: the tokenizer's reference encodings, a rendered chat conversation,
and fp32 logits for a fixed prompt set. It is the oracle, not the engine.

Outputs (paths relative to the repository root):

tests/data/llm/qwen2.5-3b-instruct/   small, committed
  corpus_tokens.json         reference encoding of corpus.txt, whole and
                             per line
  special_tokens.json        the added tokens in ID order, each with its
                             text and whether it is special
  manifest.json              config, prompts with their IDs, the chat
                             sample with its rendered text and IDs, the
                             greedy continuation, tool versions, fixture
                             checksums

tests/.local/llm/qwen2.5-3b-instruct/ large, gitignored
  vocab.json, merges.txt     the tokenizer tables, copied from the hub
  config.json                the architecture's dimensions, copied
  model-0000N-of-00002.safetensors, model.safetensors.index.json
                             the bf16 weights and their shard index, copied
  logits.safetensors         per prompt: input_ids and fp32 logits
  dumps.json                 the dumps' checksums, with the interpreter
                             and platform that produced them

The reference logits are fp32, computed on the CPU in eval mode with
deterministic algorithms. The engine runs the model in bf16 on the device,
so the C++ comparison allows a bf16 tolerance rather than expecting the
bytes to agree.
"""

import argparse
import hashlib
import json
import os
import platform
import shutil
from pathlib import Path

# See gpt2_oracle.py: plain HTTP through the CDN host the container's
# firewall allows. Must be set before huggingface_hub is imported.
os.environ.setdefault("HF_HUB_DISABLE_XET", "1")

import torch
from huggingface_hub import hf_hub_download
from safetensors.torch import save_file
from transformers import AutoModelForCausalLM, AutoTokenizer

MODEL_ID = "Qwen/Qwen2.5-3B-Instruct"
ROOT = Path(__file__).resolve().parents[2]

# The hub files copied as they are. The tables and config are what the
# tokenizer and the loader read; the shards and index are the weights.
COPIED = [
    "vocab.json",
    "merges.txt",
    "config.json",
    "model.safetensors.index.json",
    "model-00001-of-00002.safetensors",
    "model-00002-of-00002.safetensors",
]

# Fixed prompts, tokenized as plain text (no chat template).
PROMPTS = [
    "Hello",
    "In 1969, humans first landed on the Moon. The mission was called",
    "The quick brown fox jumps over the lazy dog.",
    "def fibonacci(n):\n    if n < 2:\n        return n\n",
    "Unicode: naïve café, über, 日本語, and emoji \U0001F680.",
]

# The chat sample: one system turn and one user turn, rendered through the
# model's own template with the assistant turn opened, which is the shape
# the engine's greedy decoding has to reproduce.
CHAT = [
    {"role": "system", "content": "You are a terse assistant."},
    {"role": "user", "content": "Name the planet we live on, in one word."},
]
GREEDY_TOKENS = 20


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


def write_json(path: Path, value, *, compact: bool) -> None:
    text = json.dumps(value, separators=(",", ":")) if compact else json.dumps(
        value, indent=2
    )
    # LF on every platform, so the checksums and the committed text match
    # across machines.
    path.write_text(text + "\n", encoding="utf-8", newline="\n")


def tokenizer_fixtures(tok, fixtures: Path) -> dict:
    """Write corpus_tokens.json and special_tokens.json; return their summary."""
    text = (fixtures / "corpus.txt").read_text(encoding="utf-8")
    lines = text.split("\n")
    full = tok.encode(text)
    per_line = [tok.encode(line) for line in lines]
    write_json(
        fixtures / "corpus_tokens.json", {"full": full, "lines": per_line}, compact=True
    )
    # Decode must round-trip byte for byte; the tokenizer is byte-level.
    assert tok.decode(full) == text, "reference tokenizer failed round-trip"

    # The added tokens live outside vocab.json and merges.txt: their IDs
    # follow the last merge, in the order tokenizer.json lists them, which
    # is what the C++ loader relies on.
    added = tok.get_added_vocab()
    specials = [
        {"id": token_id, "content": content, "special": content in tok.all_special_tokens}
        for content, token_id in sorted(added.items(), key=lambda kv: kv[1])
    ]
    first = specials[0]["id"]
    assert [s["id"] for s in specials] == list(range(first, first + len(specials)))
    write_json(fixtures / "special_tokens.json", specials, compact=False)
    return {
        "corpus_tokens": len(full),
        "corpus_lines": len(lines),
        "added_tokens": len(specials),
        "first_added_id": first,
    }


def prompt_artifacts(model, tok, out: Path) -> dict:
    logits = {}
    info = []
    for i, prompt in enumerate(PROMPTS):
        ids = tok(prompt, return_tensors="pt").input_ids
        with torch.no_grad():
            out_logits = model(ids).logits.squeeze(0)
        logits[f"prompt_{i}/input_ids"] = ids.squeeze(0).to(torch.int32)
        logits[f"prompt_{i}/logits"] = out_logits.contiguous()
        info.append({"text": prompt, "ids": ids.squeeze(0).tolist()})
    save_file(logits, str(out / "logits.safetensors"))
    return info


def chat_artifacts(model, tok) -> dict:
    rendered = tok.apply_chat_template(
        CHAT, tokenize=False, add_generation_prompt=True
    )
    ids = tok(rendered, return_tensors="pt").input_ids
    with torch.no_grad():
        gen = model.generate(
            ids,
            max_new_tokens=GREEDY_TOKENS,
            do_sample=False,
            pad_token_id=tok.pad_token_id,
        )
    new = gen[0, ids.shape[1] :].tolist()
    return {
        "messages": CHAT,
        "rendered": rendered,
        "ids": ids.squeeze(0).tolist(),
        "greedy": {"tokens": new, "text": tok.decode(new, skip_special_tokens=False)},
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument(
        "--fixtures", type=Path, default=ROOT / "tests/data/llm/qwen2.5-3b-instruct"
    )
    ap.add_argument(
        "--out", type=Path, default=ROOT / "tests/.local/llm/qwen2.5-3b-instruct"
    )
    args = ap.parse_args()
    fixtures, out = args.fixtures, args.out
    out.mkdir(parents=True, exist_ok=True)

    torch.manual_seed(0)
    torch.use_deterministic_algorithms(True)

    revision = None
    for name in COPIED:
        revision = fetch(name, out / name)

    tok = AutoTokenizer.from_pretrained(MODEL_ID)
    model = AutoModelForCausalLM.from_pretrained(MODEL_ID, dtype=torch.float32)
    model.eval()
    # The published config.json rather than the parsed config object, whose
    # attribute names move between transformers releases.
    cfg = json.loads((out / "config.json").read_text(encoding="utf-8"))

    manifest = {
        "model": MODEL_ID,
        "revision": revision,
        "config": {
            key: cfg[key]
            for key in [
                "hidden_size",
                "intermediate_size",
                "num_hidden_layers",
                "num_attention_heads",
                "num_key_value_heads",
                "vocab_size",
                "max_position_embeddings",
                "rms_norm_eps",
                "rope_theta",
                "tie_word_embeddings",
                "bos_token_id",
                "eos_token_id",
            ]
        },
        # nn.Linear layout: W is [out, in], y = x @ W^T + b. The q, k, and v
        # projections carry a bias; the output projection and the MLP do not.
        "weight_layout": "linear: W is [out, in], y = x @ W^T + b",
        "dtype": "float32 logits from bf16 weights widened on load",
        "tokenizer": tokenizer_fixtures(tok, fixtures),
        "prompts": prompt_artifacts(model, tok, out),
        "chat": chat_artifacts(model, tok),
    }

    import safetensors
    import transformers

    manifest["versions"] = {
        "torch": torch.__version__,
        "transformers": transformers.__version__,
        "safetensors": safetensors.__version__,
    }
    manifest["sha256"] = {
        p.relative_to(ROOT).as_posix(): sha256(p)
        for p in [
            fixtures / "corpus.txt",
            fixtures / "corpus_tokens.json",
            fixtures / "special_tokens.json",
        ]
    }
    dumps = {
        "platform": platform.system(),
        "python": platform.python_version(),
        "sha256": {p.name: sha256(out / p.name) for p in [out / n for n in COPIED]}
        | {"logits.safetensors": sha256(out / "logits.safetensors")},
    }
    write_json(out / "dumps.json", dumps, compact=False)
    write_json(fixtures / "manifest.json", manifest, compact=False)
    print(f"chat greedy: {manifest['chat']['greedy']['text']!r}")
    print(f"wrote {fixtures} and {out}")


if __name__ == "__main__":
    main()
