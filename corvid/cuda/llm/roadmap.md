# LLM roadmap

Plan for `corvid/cuda/llm`: a transformer inference and adapter-training
stack written from scratch in Corvid-flavored C++23 and CUDA, targeting one
RTX 4090. The destination is the "weights versus context" experiment from
the personalized-memory design: compile plain-English dispositional traces
into a LoRA and measure whether the adapted model behaves differently from
the same base model given the traces in context.

## Why from scratch, and why in C++

The point is to learn the machinery, not to get a result cheaply. A framework
hides exactly the parts worth understanding: what a forward pass allocates,
where the time goes, how a gradient is actually assembled. Writing each op by
hand and checking it against a reference forces the understanding, and doing
it in C++ means the knowledge lands in a language that will be kept, not in a
notebook that will be abandoned.

Two rules keep this from becoming another open-ended polish hole:

- **Every stage has a numerical oracle.** A stage is done when its output
  matches the reference within a stated tolerance. Not "looks right", not
  "generates plausible text": matches. The oracle is generated once, by a
  throwaway script, and stored as data.
- **Naive before fast.** Each op gets a plain, readable CPU version first.
  That version is the reference for the CUDA version and is never optimized.
  Performance work happens only in the CUDA path, and only after it matches.

## Working mode

The educational goal is the one that failed in the game demo, where leaning
on Claude for the rendering algorithms turned the engineer into a project
manager. So the division of labor is fixed up front:

- **Steven writes the model code.** The tokenizer scanner, every op in the
  CPU forward pass, every kernel, the backward pass, the training loop.
  That is where the learning is, and it is not delegated.
- **Claude writes the scaffolding.** The oracle script, the build and
  CMake wiring, the test files that load fixtures and diff against the
  oracle, and this file's upkeep. None of that teaches anything worth
  learning by hand.
- **Before each stage, a primer.** What the op computes, why it is shaped
  that way, and the traps that are known in advance (column-major BLAS,
  softmax overflow, the attention mask off-by-one). Delivered in
  conversation; what survives into the code as comments is Steven's call.
- **When stuck, explanation over patches.** Claude diagnoses, points at the
  oracle diff, and explains the mechanism. Claude edits model code only on
  an explicit request for that edit.
- **Review after each stage**, the same way the module review quest reviews
  a band.

## Layout

- `corvid/cuda/llm/`: model code. CPU-only pieces (tokenizer, safetensors
  reader, CPU forward) are plain `.h` in `corvid::llm`; anything that touches
  the device is `.cuh` in `corvid::cuda::llm`, resting on `cuda_ptr`,
  `cuda_cublas`, `cuda_event`, and friends.
- **A new `cuda` band** in `deps.md`, high in the stack so it may depend on
  `filesys` and `proto`. This puts the CPU-only `.h` files under the layering
  lint, which skips `.cuh` today.
- `tests/portable/`: `.cpp` tests for the CPU pieces (clang, libc++).
- `tests/cuda/`: `.cu` tests for the device pieces. Linux under nvcc +
  g++-15 is the target; the code stays Windows-clean in principle (no
  Linux-only headers in device code, no assumptions clang's CUDA frontend
  would reject) but the Windows leg is not built or run until the staircase
  is done.
- `tests/data/llm/`: small committed fixtures, on the order of a megabyte
  in total: the tokenizer tables, the tokenizer corpus and its expected
  encoding, and the oracle manifest. Never a model.
- `tests/.local/llm/`: weights and activation dumps. Gitignored; too large
  to commit and reproducible from the oracle script. Tests that need them
  skip with a clear message when the directory is absent.
- `scripts/llm_oracle/`: the one Python script, `gpt2_oracle.py`, with
  `setup.sh` (venv under `tests/.local/llm/venv`, torch from the CPU wheel
  index) and `requirements.txt`. PyTorch is allowed here and nowhere else:
  it reads the published weights and writes the reference artifacts. It
  never runs inference for the project, which would defeat the purpose. The
  container's firewall allows `huggingface.co`, its download hosts, and
  `download.pytorch.org`, so the script runs in place after a rebuild.

## Side quests

Two pieces of general library infrastructure fall out of the early stages.
Each is a proper Corvid module with its own tests, not an LLM-private helper.

- **UTF-8 in `corvid/strings`.** A codepoint decoder over `std::string_view`
  (iteration, not transcoding), plus a classifier for the Unicode Letter and
  Number general categories and the White_Space property. The range tables
  are data, generated into `strings/unicode_tables.h` by
  `scripts/gen_unicode_tables.py` from Python's Unicode database (15.0.0,
  no download); the classifier that searches them is the hand-written part.
  Sized to what the tokenizer needs; transcoding to UTF-16 or UTF-32 waits
  for a consumer.
- **A file mapping in `corvid/filesys`.** RAII over `mmap` of a file,
  read-only, Linux-only, reusing the `mmap_prot` and `mmap_mask` enums that
  `os_enums.h` already has for the io_uring buffer pools. No cross-platform
  mapped-file abstraction: there is no basis yet for knowing what the
  abstraction may hide. Hugepages are not offered (WSL does not support
  them, learned earlier in the io_uring work).

## Stages

### 0. Oracle and data

Produce the reference artifacts for GPT-2 124M:

- tokenizer fixtures: `vocab.json`, `merges.txt`, and a corpus of mixed text
  (ASCII, punctuation runs, numbers, contractions, Unicode whitespace,
  non-Latin scripts, combining marks, emoji) with its reference encoding
  from the Hugging Face GPT-2 tokenizer, which is built from the same two
  tables (tiktoken would need one more download host for no gain);
- the model weights as `model.safetensors`;
- for a fixed set of prompts: the logits, and for one prompt, every
  sublayer boundary (embedding sum, then per block the residual entering,
  the attention output, the residual after attention, the MLP output; then
  the residual leaving the last block and the logits) so a mismatch can be
  bisected to the op that introduced it;
- the greedy continuation of that prompt, as the oracle for decoding;
- for one small batch: the loss and every parameter gradient, saved now so
  stage 5 does not need Python again.

Done when: the artifacts exist and a checksum manifest is written beside them.

Status (2026-09-05): DONE. The oracle ran on torch 2.14.0+cpu with
transformers 5.16.1 (recorded in the manifest) and wrote the committed
fixtures plus about 1 GB of dumps under `tests/.local/llm/gpt2`. The corpus
is 1375 tokens over 85 lines; the gradient batch is 4 x 64 with loss
5.1973. Rerun with `scripts/llm_oracle/setup.sh` once per container, then
the command it prints. Firewall notes from the first run: the torch index
hands wheel bytes to `download-r2.pytorch.org`, and the Hugging Face resolve
redirect for large files lands on `us.aws.cdn.hf.co`; both are in the
allowlist, the superseded `cdn-lfs*` and `xethub` names are dropped, and
the oracle disables the xet transport so the plain path is the only one to
keep open. Known primer item recorded in the manifest: HF GPT-2 stores
projections as Conv1D (`W` is `[in, out]`, `y = x @ W + b`), the transpose
of `nn.Linear`.

### 1. Tokenizer

GPT-2's byte-level BPE, in `corvid::llm`, built on `corvid/strings` and the
UTF-8 side quest:

- the byte-to-printable-codepoint table and its inverse;
- the pre-tokenizer: GPT-2's split rule (contractions, letter runs, digit
  runs, punctuation runs, whitespace handling). The original is a regex over
  Unicode categories. Here it is a hand-written scanner: a small state
  machine over decoded codepoints that implements that one rule directly.
  No regex engine, which is a preference as much as a necessity;
- merge ranking and the encode loop;
- decode.

Done when: encode matches `tiktoken` on the whole corpus, and decode of the
encoding reproduces the corpus byte for byte.

The pre-tokenizer is the first place a "just get it working" shortcut would
be tempting (ASCII-only classes pass most English text). Do not take it; the
corpus is built to catch it. Every later tokenizer (Llama 3, Qwen, GPT-4's)
uses the same shape of rule with more clauses, so this scanner is reused
with different tables, not replaced.

### 2. Weights

A `safetensors` reader: parse the header with `proto/misc/json_parser.h`,
map the file with the filesys side quest, and expose each tensor as a typed
view (dtype, shape, strides, `std::span` of the bytes).

Done when: every tensor in GPT-2's file is found with the expected shape and
dtype, and a spot check of values matches the oracle manifest.

### 3. CPU forward pass

GPT-2 in fp32, naive and readable: token and position embeddings, layernorm,
causal multi-head attention, GELU MLP, residuals, tied output head. One
struct per op, one function per op, no cleverness. Then greedy decoding, so
it produces text.

Done when: per-layer activations match the oracle within tolerance for the
bisect prompt, and logits match for the full prompt set. Text generation is
a demonstration, not the gate.

This is the stage where the learning is densest, and the code stays as the
reference for everything after. It is allowed to be slow.

### 4. CUDA forward pass

The same model on the device:

- matmuls through `cuda_cublas.cuh`, which gets its first real consumer.
  Row-major weights against a column-major BLAS is the first lesson;
- hand-written kernels for embedding gather, layernorm, GELU, softmax,
  attention, and residual add;
- a KV cache, so generation stops recomputing the prefix;
- then a bf16 variant on the tensor cores, with its own looser tolerance;
- tokens per second measured and recorded here, per variant.

Done when: fp32 device logits match the CPU pass within tolerance, and bf16
matches within its stated tolerance.

Kernel fusion, tiling, and the like are explicitly deferred until the whole
staircase is complete. The measurement is recorded so there is a baseline to
beat later. llama.cpp, already on this machine, is the yardstick: its
tokens per second on the same model and precision goes in the same table,
so the gap is a number rather than an impression.

### 5. Backward pass and LoRA

Backward kernels for every op in stage 4, a LoRA on the attention
projections, AdamW over the adapter parameters only, and a training loop over
a small text file.

The first oracle is the stage-0 gradient dump: full-model gradients for one
batch, compared op by op. LoRA is then a restriction of that machinery, not a
separate implementation. Only the adapter parameters carry optimizer state,
but activations still flow backward through the whole network.

Done when: gradients match the oracle, then the LoRA training loss goes down
on the sample text and the merged adapter changes generation in the expected
direction.

### 6. A model that can hold an opinion

GPT-2 124M is too weak for a preference to mean anything. The experiment
needs a small instruct model, which means the Llama family's parts: RoPE,
RMSNorm, SwiGLU, grouped-query attention, and a second BPE configuration
(larger vocabulary, more split clauses; the stage-1 scanner with different
tables). The oracle script is re-run for the new model.

On precision: the quantized GGUF files that llama.cpp runs are a
distribution convenience for models too large to fit otherwise. A 3B model
is 6 GB in bf16, the format its publisher ships in `safetensors`, and it
fits the 4090 for inference and for LoRA training with room to spare. So no
quantization enters this project; bf16 is the only step past fp32, and
stage 4 already takes it.

Candidate: Qwen2.5-3B-Instruct (ungated download, bf16 safetensors, the
standard Llama-style architecture plus a QKV bias). Llama 3.2 3B Instruct is
the alternative if a gated download is acceptable. Nothing before this stage
depends on the choice.

Done when: the new model's logits match its oracle, and it answers a prompt
sensibly under greedy decoding.

### 7. The experiment

Weights versus context, in miniature:

1. write a handful of dispositional traces in plain English;
2. control: base model with the traces in the system prompt;
3. treatment: sample the control's outputs on a training prompt set, then
   train a LoRA on those outputs with the traces removed (context
   distillation), using the stage-5 loop;
4. compare control and treatment on a held-out probe set by output
   distribution divergence, and by a simple recognition probe: does the
   adapted model bring up a trace's topic unprompted where the control does
   not.

Done when: the numbers are in this file, whatever they say. Matching the
control is a legitimate outcome; it means the architecture reduces to a
well-built context memory, which is worth knowing before anyone builds the
rest of it.

## Deferred

Not planned, recorded so they are not re-litigated as scope:

- adapter hot-swap and multi-adapter serving in one process;
- pretraining from scratch (llm.c-style GPT-2 reproduction);
- quantized weights and a GGUF reader. They arrive together: reading GGUF
  is mostly implementing the dequantization of each block format, which is
  a self-contained study of its own. If a GGUF-only model is ever needed
  sooner, the oracle script can unpack it to bf16 safetensors instead;
- a cross-platform mapped-file abstraction, and the Windows CUDA leg;
- kernel-level performance work beyond what stage 4 measures;
- the consolidator, auditor, and narrative layers of the memory design.
  They are prompting over text and need nothing built here.

## Rulings

Settled 2026-09-05, recorded so they are not reopened:

- PyTorch is permitted in the oracle script only, never for inference.
- The mmap wrapper is Linux-only and read-only; no cross-platform
  abstraction yet.
- UTF-8 support goes in `corvid/strings` as a side quest, sized to the
  tokenizer's needs.
- No regex engine anywhere in the tokenizer.
- A `cuda` band is added to `deps.md`, above `filesys` and `proto`.
- Small fixtures and the Python oracle script live in the repo; models and
  activation dumps live in `tests/.local/`.
- `huggingface.co` and its download hosts are in the firewall allowlist.
- Linux CUDA is the target; the code stays Windows-clean without being
  built there.
- GPT-2 124M stays as the stage 1 to 5 model: fp32 weights, no quantization,
  small enough that the naive CPU pass runs in seconds. Its tokenizer
  needs the same UTF-8 classes every newer tokenizer needs, so a newer
  model would not avoid that work, only add RoPE, GQA, and bf16 to the
  first mile.

## Open

- Stage-6 model choice, deferrable until stage 6.

## Done

- 2026-09-05: plan and working mode approved. Scaffolding in place: `cuda`
  band (lint scripts and `deps.md`), firewall hosts, oracle script and
  corpus, Unicode range tables. Nothing has run against real weights yet.
