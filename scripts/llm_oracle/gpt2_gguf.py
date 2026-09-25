#!/usr/bin/env python3
"""Pack the GPT-2 124M oracle weights as GGUF files for llama.cpp.

Stage 4's tokens-per-second table uses llama.cpp as its yardstick, and a
yardstick only counts when it runs the same weights the tests gate against.
This reads the oracle's model.safetensors and the committed tokenizer tables
and writes one GGUF per precision the device engine runs at:

tests/.local/llm/gpt2/gpt2-f32.gguf     every tensor fp32
tests/.local/llm/gpt2/gpt2-bf16.gguf    matrices bf16, vectors fp32, the
                                        split the bf16 engine makes

Run the oracle first; this never touches the hub. Then, for example:

  llama-bench -m tests/.local/llm/gpt2/gpt2-bf16.gguf -p 1023 -n 0
"""

import argparse
import json
from pathlib import Path

import gguf
import numpy as np
from safetensors import safe_open

ROOT = Path(__file__).resolve().parents[2]

# HF stores the projections as Conv1D, [in, out]; llama.cpp wants [out, in].
CONV1D = {"attn.c_attn", "attn.c_proj", "mlp.c_fc", "mlp.c_proj"}
BLOCK_PARTS = {
    "ln_1": "attn_norm",
    "attn.c_attn": "attn_qkv",
    "attn.c_proj": "attn_output",
    "ln_2": "ffn_norm",
    "mlp.c_fc": "ffn_up",
    "mlp.c_proj": "ffn_down",
}
TOP_LEVEL = {
    "wte.weight": "token_embd.weight",
    "wpe.weight": "position_embd.weight",
    "ln_f.weight": "output_norm.weight",
    "ln_f.bias": "output_norm.bias",
}


def gguf_name(name: str) -> tuple[str, bool] | None:
    """The GGUF name of the safetensors tensor `name` and whether it needs
    transposing, or None to skip it."""
    if name in TOP_LEVEL:
        return TOP_LEVEL[name], False
    # h.<block>.<part>.<weight|bias>; the attn.bias mask is not a parameter.
    _, block, *part, kind = name.split(".")
    part = ".".join(part)
    if part not in BLOCK_PARTS:
        return None
    return f"blk.{block}.{BLOCK_PARTS[part]}.{kind}", (
        part in CONV1D and kind == "weight"
    )


def load_tensors(path: Path) -> list[tuple[str, np.ndarray]]:
    tensors = []
    with safe_open(str(path), "np") as f:
        for name in f.keys():
            mapped = gguf_name(name)
            if mapped is None:
                continue
            new_name, transpose = mapped
            data = f.get_tensor(name)
            if transpose:
                data = np.ascontiguousarray(data.T)
            tensors.append((new_name, data))
    return tensors


def load_vocab(fixtures: Path) -> tuple[list[str], list[int], list[str]]:
    """The token strings by ID, their types, and the merges, as GPT-2 has them."""
    vocab = json.loads((fixtures / "vocab.json").read_text(encoding="utf-8"))
    tokens: list[str | None] = [None] * len(vocab)
    for text, token_id in vocab.items():
        tokens[token_id] = text
    assert all(t is not None for t in tokens), "vocab has gaps"
    # The one special token is <|endoftext|>, the last one.
    types = [int(gguf.TokenType.NORMAL)] * len(tokens)
    types[-1] = int(gguf.TokenType.CONTROL)
    merges = (fixtures / "merges.txt").read_text(encoding="utf-8").splitlines()
    assert merges[0].startswith("#version"), "merges.txt lacks its header"
    return tokens, types, merges[1:]


def write(
    path: Path,
    file_type: gguf.LlamaFileType,
    cfg: dict,
    tensors: list[tuple[str, np.ndarray]],
    vocab: tuple[list[str], list[int], list[str]],
) -> None:
    tokens, types, merges = vocab
    end_of_text = len(tokens) - 1
    w = gguf.GGUFWriter(str(path), gguf.MODEL_ARCH_NAMES[gguf.MODEL_ARCH.GPT2])
    w.add_name("gpt2")
    w.add_block_count(cfg["n_layer"])
    w.add_context_length(cfg["n_ctx"])
    w.add_embedding_length(cfg["n_embd"])
    w.add_feed_forward_length(4 * cfg["n_embd"])
    w.add_head_count(cfg["n_head"])
    w.add_layer_norm_eps(cfg["layer_norm_epsilon"])
    w.add_file_type(file_type)
    w.add_tokenizer_model("gpt2")
    w.add_tokenizer_pre("gpt-2")
    w.add_token_list(tokens)
    w.add_token_types(types)
    w.add_token_merges(merges)
    w.add_bos_token_id(end_of_text)
    w.add_eos_token_id(end_of_text)
    w.add_add_bos_token(False)
    for name, data in tensors:
        # "Mostly" bf16: the matrices; vectors (norms and biases) stay fp32.
        if file_type == gguf.LlamaFileType.MOSTLY_BF16 and data.ndim == 2:
            bf16 = gguf.GGMLQuantizationType.BF16
            w.add_tensor(name, gguf.quants.quantize(data, bf16), raw_dtype=bf16)
        else:
            w.add_tensor(name, data)
    w.write_header_to_file()
    w.write_kv_data_to_file()
    w.write_tensors_to_file()
    w.close()
    print(f"wrote {path} ({path.stat().st_size >> 20} MiB)")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--fixtures", type=Path, default=ROOT / "tests/data/llm/gpt2")
    ap.add_argument("--out", type=Path, default=ROOT / "tests/.local/llm/gpt2")
    args = ap.parse_args()
    fixtures, out = args.fixtures, args.out

    cfg = json.loads((fixtures / "manifest.json").read_text(encoding="utf-8"))[
        "config"
    ]
    tensors = load_tensors(out / "model.safetensors")
    vocab = load_vocab(fixtures)
    assert len(vocab[0]) == cfg["vocab_size"]
    write(out / "gpt2-f32.gguf", gguf.LlamaFileType.ALL_F32, cfg, tensors, vocab)
    write(out / "gpt2-bf16.gguf", gguf.LlamaFileType.MOSTLY_BF16, cfg, tensors, vocab)


if __name__ == "__main__":
    main()
