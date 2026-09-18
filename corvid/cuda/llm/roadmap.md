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
- **Refined 2026-09-07, stage 3:** Steven asked Claude for the first draft
  of each CPU op as well, starting with `layer_norm` and the projection,
  because reviewing a draft line by line teaches the same things as writing
  it, in less time. Claude names every decision in the draft; the review
  reshapes it. The kernels and the backward pass keep the original split
  until Steven says otherwise.

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

- **UTF-8 in `corvid/strings`.** A codepoint decoder over `std::u8string_view`
  (iteration, not transcoding), plus a classifier for the Unicode Letter and
  Number general categories and the White_Space property. The range tables
  are data, generated into `strings/unicode_tables.h` by
  `scripts/gen_unicode_tables.py` from Python's Unicode database (15.0.0,
  no download) as a two-stage page table with a property bitmap per code
  point; the classifier that indexes it is the hand-written part.
  Sized to what the tokenizer needs; transcoding to UTF-16 or UTF-32 waits
  for a consumer. DONE 2026-09-05: `strings/unicode.h` (`decode` and
  `extract` over `std::u8string_view` into a `char32_t`, rejecting
  overlongs, surrogates and values past U+10FFFF; the `classifier`
  namespace with `is_letter`, `is_number`, `is_white_space`, and `classify`
  into `code_point_class`), tested in `tests/portable/unicode_test.cpp`.
  The header knows nothing about tokenizers; that is the layering, not an
  oversight.
- **A file mapping in `corvid/filesys`.** RAII over `mmap` of a file,
  read-only, Linux-only, reusing the `mmap_prot` and `mmap_mask` enums that
  `os_enums.h` already has for the io_uring buffer pools. No cross-platform
  mapped-file abstraction: there is no basis yet for knowing what the
  abstraction may hide. Hugepages are not offered (WSL does not support
  them, learned earlier in the io_uring work). DONE 2026-09-05:
  `filesys/linux_mmap.h` holds the `mmap_*` enums (moved out of the OS enums,
  closing the TODO there) and the RAII `memory_map` class: `create` over
  the raw call, `map` and `map_all` over an `os_file`, `map_file` over a
  path, `advise` over `madvise`, plus `unmap` and `release`. General rather
  than read-only, since the io_uring pools want the same wrapper; tested in
  `tests/linux/linux_mmap_test.cpp`. Named `memory_map` rather than `mmap`
  because the bare class name was ambiguous with `::mmap` under
  `using namespace corvid`.

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

Status (2026-09-05): primer delivered in conversation. Fixture facts it
rests on: vocab.json is pure ASCII with `\u` escapes for every remapped
byte above 0x7F (`json_parser.h` decodes them); merges.txt has a
`#version` header line and then one space-separated pair per line, rank
equals line order; `corpus_tokens.json` holds `full` (the whole file,
newlines included) and `lines` (each line without its newline, from a
split on `\n`); the reference encoder adds no special tokens.

Design decisions (2026-09-05), agreed before the user writes the code:

- The byte-to-code-point table is a serialization detail of the two files.
  The loader applies only its inverse (UTF-8 decode each key or merge piece,
  map each code point back to its byte); the tokenizer never applies the
  forward direction. In memory, symbols are bytes.
- The split rule runs over decoded code points but yields byte offsets into
  the input, so a chunk's bytes are the initial symbol sequence with no copy.
- The merge loop runs over token IDs, not strings. Verified in the fixture:
  the merge at rank r yields ID 256 + r, every merge piece and result is in
  vocab.json, and IDs 0-255 are the 256 mapped code points in sorted order
  (the 188 identity bytes in byte order, then the 68 remapped bytes in byte
  order). So vocab.json is redundant for encode and decode; the tokenizer
  builds the byte-to-ID table from the mapping and reads only merges.txt,
  and `<|endoftext|>` is the constant 50256. The fixture test loads
  vocab.json once and asserts the invariant, so the assumption is checked
  rather than trusted.
- Merge table: `std::flat_map<uint32_t, token_id>` keyed on the packed pair
  (left ID << 16 | right ID), value the result ID; rank is ID - 256.
  Probed on libc++, clang + libstdc++, and nvcc + g++-15; Windows still to
  probe. Built once from a vector, never inserted into per element.
- `token_id` is `uint32_t`, the type the model side indexes embeddings and
  logits with; a 16-bit public type would leak GPT-2's vocabulary size into
  every consumer. The 16-bit limit lives only in the pair key, and `load`
  refuses a table that would overflow it.
- Per-chunk memoization is postponed; measure before adding it.

Status (2026-09-05): WRITTEN, tests green on libc++ and libstdc++. Reviewed
line by line and accepted 2026-09-06.
`gpt2_tokenizer.h` holds `corvid::llm::token_id` and class
`gpt2_tokenizer`: the byte escaping tables (consteval, in `details`),
`escape_byte` / `unescape_byte` / `unescape_piece`, the static
`split` (the regex as a hand-written scanner over `unicode.h`), `load`
(merges text only, strong guarantee), `encode`, `decode`, `size`, and
`piece`. Merging runs in place on the output vector's tail. The test is
`tests/portable/gpt2_tokenizer_test.cpp`: table and split cases with
inline expectations, a small inline merge table for the loop, and the
fixtures found relative to `__FILE__` (the first test to read
`tests/data`, so no build wiring was added): every vocab.json entry names
the piece at its ID, the whole corpus and every line encode as the
reference did, and the reference IDs decode to the file byte for byte.
Open: the end-of-text ID (50256) is not a piece, so `decode` rejects it;
stage 4 decides how generation stops before calling `decode`. Not yet run
on the Windows leg.

### 2. Weights

A `safetensors` reader: parse the header with `proto/misc/json_parser.h`,
map the file with the filesys side quest, and expose each tensor as a typed
view (dtype, shape, strides, `std::span` of the bytes).

Done when: every tensor in GPT-2's file is found with the expected shape and
dtype, and a spot check of values matches the oracle manifest.

Rulings (2026-09-06):

- The hub's `model.safetensors` has an unpadded header, so its buffer
  starts at an odd file offset and a typed view into the mapping would be
  misaligned. The oracle now rewrites the file through the current
  safetensors writer, which pads the header to an 8-byte boundary; the
  three oracle dumps were already padded. The reader still checks
  alignment before handing out a typed view, so an arbitrary file cannot
  produce undefined behavior. Uploading to the device never needed the
  alignment: `cudaMemcpy` has no host alignment requirement.
- The reader maps through `filesys/os_mmap_file.h`, the portable read-only
  facade over the Linux `memory_map` and the Windows `mapped_view` (added
  2026-09-07, replacing the original Linux-only `linux_mmap.h` dependency).
  `load` takes an open `os_file`, so the reader holds no platform code and
  its test lives in tests/portable.
- The 548 MB model is never committed. The reader's format cases use
  tiny files built inline in the test; the GPT-2 checks run only when the
  gitignored file is present and skip otherwise.

Status (2026-09-06): WRITTEN, tests green with clang-tidy. `safetensors.h`
holds `tensor_dtype` (a named sequence enum using the header's names),
`dtype_size`, the `TensorElement` concept with `dtype_of`, and class
`safetensors_file`: `parse` over a caller-owned image, `load` over a kept
`os_mmap_file`, `find` by name, `tensors` in header order, `metadata`, and a
`tensor` record whose `is<T>` checks dtype and alignment before `as<T>` hands
out a typed span. Validation follows the reference implementation, including
the exact tiling of the buffer. The test is
`tests/portable/safetensors_test.cpp`: inline images for the format and every
rejection, a temp file for `load`, and, when the oracle dumps exist, the 160
GPT-2 tensors' shapes and dtypes plus a value-level spot check that `embed/out`
for the bisect prompt equals `wte[id] + wpe[pos]` to 1e-6, which ties stages 1
and 2 to the oracle. The oracle gained `align`, and the manifest's model hash
was updated for the rewritten file.

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

Status (2026-09-06): primer delivered in conversation. Oracle facts it
rests on, read from the installed transformers 5.16.1 source and the dump
headers rather than remembered: the activation is `gelu_new` (the tanh
approximation, not erf); attention scales scores by `1/sqrt(64)`, the head
dimension, and heads are contiguous 64-wide column blocks of the 768-wide
q, k and v thirds of `c_attn`'s 2304 output columns, in that order; the
causal rule is `j <= i`; layernorm uses the biased variance with `eps =
1e-5` inside the square root; the output head is `wte` transposed with no
bias; `h.N.attn.bias` in the weights file is the saved causal mask buffer,
not a parameter, and is ignored. `activations.safetensors` holds, for the
bisect prompt (T = 14), `embed/out`, per block `ln_1/in`, `ln_1/out`,
`attn/out` (after `c_proj`, before the residual add), `ln_2/in`,
`ln_2/out`, `mlp/out` (likewise), then `ln_f/in`, `ln_f/out`, and
`logits`; `logits.safetensors` holds `prompt_N/input_ids` (I32) and
`prompt_N/logits` for all five prompts. Tolerance is the allclose shape,
`|a - b| <= atol + rtol * |b|`, because the residual stream has a few
coordinates in the thousands; the gate is set from the first measured run
and then stated here.

Design decisions (2026-09-07), settled before the code:

- Activations are `matrix_view<T>` from
  "corvid/containers/utils/matrix_view.h": a row-major, non-owning view
  with a row stride, so a head's block of columns is a `subview` over the
  projection output rather than a copy. Rows and columns are indexed by the
  sequence enums `row_ndx` and `col_ndx` (aliased into the class with
  `coord` and `extent`), so the two cannot be swapped by accident; that
  enum dependency is why it lives in `containers/utils` rather than `math`
  (2026-09-07 redesign). Nothing in it is LLM-specific. Tested in
  `tests/portable/matrix_view_test.cpp`. `std::mdspan` was considered
  and rejected because libstdc++ on g++-15, the CUDA host compiler, does
  not ship it.
- The ops are free functions on views, each writing into a caller-owned
  output, in `corvid::llm`: `layer_norm`, then the projection, GELU in
  place, attention, one block, the forward pass with an observer called at
  each dump name, and greedy decoding last. Shape mismatches are contract
  violations (asserted), so the ops return `void`. Accumulation is in
  `float`, so the CPU pass stays a faithful reference for the device pass.
- Weights stay in the mapping: each parameter is a `std::span<const float>`
  from the reader, viewed with the shape from the header.
- The test in `tests/portable/` has two modes. Isolated feeds each op the
  oracle's dumped input and compares its output alone, so an error in a
  late block cannot hide behind drift from an early one. Chained runs the
  whole model on the bisect prompt and compares every dump in order. Plus
  the five prompts' logits, from their dumped IDs and from the tokenizer,
  and the greedy continuation from the manifest.

Status (2026-09-07): `layer_norm` drafted in
`corvid/cuda/llm/gpt2_forward.h` (namespace `corvid::llm`, the header that
will hold every CPU op), with hand-computed cases in
`tests/portable/gpt2_forward_test.cpp`. The row reductions it rests on
(`sum`, `mean`, `squared_deviation_sum`, `variance`, `inverse_std_dev`) and
the elementwise steps (`standardize`, `scale_shift`) are public functions in
the same header, candidates for `corvid/math` once a second consumer appears.

Status (2026-09-07, later): the oracle runs on Windows too
(`scripts/llm_oracle/setup.ps1`, pinned to the manifest's torch 2.14.0+cpu,
transformers 5.16.1, safetensors 0.8.0; greedy text and loss reproduce
exactly, while the dump checksums differ from the Linux run at the ulp
level, as expected across BLAS backends). The forward test has the oracle
loader, an allclose helper reporting the largest absolute and relative
errors, and the isolated layer norm gate over all 25 dumped sites (each
block's `ln_1` and `ln_2`, plus `ln_f`). First measured run: every site
passes at `atol = rtol = 1e-5`; the largest absolute error is 9.2e-5 on a
large-magnitude residual coordinate, and the typical one is a few 1e-6. That
tolerance is the gate. Next: the projection, written by Steven.

Status (2026-09-08): `layer_norm` split into two row ops it composes per
row, so the row is still in cache for the second step: `standardize_row`
(the statistics and the z-scores) and `scale_shift_row` (the affine step).
Either runs in place when the output span is the input span. Per-feature
parameters (`weight` and `bias` of both `layer_norm` and `linear`) are now
`const_float_row_span`, indexed by `col_ndx` directly instead of through
`*c` on a `size_t` span. The aliasing contracts are asserted rather than
described: the row ops and `layer_norm` require the output to be the same
memory as the input or none of it, and `linear` requires its output apart
from all three inputs. The predicates behind those asserts,
`is_disjoint` and `is_same_or_disjoint` over any two contiguous ranges,
live in `corvid/meta/containers.h`.

Status (2026-09-17): `linear` done. It takes `weight` in the Conv1D layout
GPT-2 stores (one row per input feature, one column per output feature),
copies `bias` into each output row and then adds each input feature's
scaled weight row with `add_scaled`, so the inner loop is one axpy per
input feature rather than one dot product per output feature. Its doc
block carries the formula, a table of which operand each index is a row or
column of, and the four GPT-2 call sites with their feature counts; that
table is what made the op legible, so the op headers keep call-site shape
tables even though comments elsewhere face inward. The hand-computed test
writes into a strided subview of a wider buffer. There is no dumped
activation on either side of a projection alone, so the projections are
gated through the MLP path below.

Status (2026-09-17): `gelu_new` drafted, as a scalar overload holding the
tanh formula and a matrix overload applying it elementwise with the same
in-place-or-disjoint contract as `layer_norm`. The two constants are named:
`gelu_tanh_scale` is `sqrt(2 / pi)` built from `std::numbers` (`sqrt2` times
`inv_sqrtpi`, which rounds to the same `float` as the direct square root),
and `gelu_cubic_coeff` is the paper's 0.044715. Hand-computed cases take
their reference values from torch's `gelu(approximate="tanh")`. There is no
dumped activation between `c_fc` and `c_proj`, so the oracle gate for it is
the MLP path as a whole: each block's dumped `ln_2/out` through `c_fc`,
`gelu_new` in place, and `c_proj`, against its dumped `mlp/out`. First
measured run over all 12 blocks: every block passes at `atol = rtol =
1e-4`, and that is the gate. At the layer norm's 1e-5, blocks 3 and 11
fail, with largest absolute errors of 3.5e-4 and 8.4e-5; the sums here run
over 768 and 3072 terms in a different order than the oracle's BLAS, so
the looser gate is the accumulation order, not a defect. The largest
absolute error anywhere is 9.8e-4 in block 2, exactly 2^-10, one ulp on a
residual coordinate near 1000. Next: attention.

Status (2026-09-17, later): attention drafted, after a worked example on
three tokens of width two (in conversation; the numbers are now the
hand-computed test). Two ops: `attention_head` runs one head on its
`q`, `k`, `v` views, scoring each token against the tokens at or before it
with `dot` scaled by `1 / sqrt(width)`, taking `softmax_row` over those
scores, and accumulating the value rows with `add_scaled`; `attention`
splits the `c_attn` output into its query, key, and value thirds, cuts each
into `head_count` column slices, and runs `attention_head` per slice into
the matching slice of the output. Decisions: the causal mask is the loop
bound (only tokens 0 through `i` are ever scored, nothing is set to minus
infinity); the scratch is one row of `token_count` scores passed in by the
caller, since each token's weights are consumed before the next token's are
computed, and the ops stay allocation-free; `softmax_row` takes plain spans
because its row is indexed by token here and by vocabulary entry at the
head, so neither `row_span` nor `col_span` fits; and the max-subtraction
is a body comment, not contract. The oracle gate is the attention path,
`ln_1/out` through `c_attn`, the heads, and `c_proj` against `attn/out`,
since no dump sits inside it. First measured run over all 12 blocks: every
block passes at `atol = rtol = 1e-4`, the same gate as the MLP path; at
1e-5, blocks 10 and 11 fail with largest absolute errors of 2.8e-5 and
2.4e-4 (the latter exactly 2^-12, one ulp near 512). The reference
"gpt-2.md" beside this file tabulates every step, shape, and tensor name.
Next: the residual add, the embedding gather, the block, the forward pass
with an observer, the head, and greedy decoding.

Status (2026-09-18): every index loop in the header now walks its spans
in lockstep with `std::views::zip`, Steven's suggestion and the first use
of it in the repository. `matrix_view` gained `rows()`, a random-access
range of `row_span` that copies the view so it survives a temporary, which
lets the row loops of `layer_norm`, `gelu_new`, and `linear` zip two views'
rows, and `linear`'s inner loop zip the token's row against the weight's
rows, retiring the `in_as_col` retype that existed only to share an index.
In `attention_head` the causal mask became the length of the `weights`
prefix, since zipping it against every key or value row stops there. With
`zip` the equal-size asserts are load-bearing rather than redundant,
because a mismatch would silently process the shorter range. The cl leg
surfaced a pre-existing C4723 (potential divide by zero) in `mean` and
`variance`, whose empty-span NaN is the contract; it is bracketed with
`PRAGMA_MSVC_IGNORED` from "corvid/meta/crossplatform.h", the ruling being
that a global `/wd` would hide real divisions and a raw pragma is not
cross-platform.

Codegen notes from the same day, checked in the assembly rather than
assumed. The `zip` and `rows()` machinery dissolves completely at `-O2`:
no `zip_view`, `transform_view`, or `iota_view` survives, and the hot axpy
loop of `linear` is instruction for instruction the loop the index version
produced. The one artifact is the MSVC STL's out-of-line `__std_min_8u`,
called once per `zip` construction to take the minimum of the sizes; that
is once per token per input feature in `linear`, each ahead of a 3072-wide
axpy, so well under a percent, and libc++ and libstdc++ inline their
`std::min`. Prompted by this, the Windows clang leg gained `-march=native`
and cl `/arch:AVX2` (see "crossplatform.md"), so the loops use fused
multiply-add and 256-bit registers on both. On `inverse_std_dev`: it is a
square root and a division like any other, and the saving is that they
happen once per row while `standardize` multiplies 768 times, in place of
768 divisions; a packed divide runs at roughly a tenth the throughput of a
multiply and cannot be fused, the compiler may not make the substitution
itself without fast-math, and torch's layer norm computes `rstd` the same
way, which is part of why the gate holds at 1e-5. The deferred
`-fno-math-errno` decision, which would remove the `sqrtf` fallback branch
from each row, is analyzed in "crossplatform.md" under Building.

Review notes on the zip pass (2026-09-18, Steven): the zip code is better
on the whole. By removing the need for indexes it also removed much of the
point of `row_span` and `col_span`, whose type-safe indexes no longer guard
anything in these ops, and it removed the `in_as_col` retype; both count
as positives. `attention_head` lost its last index loop too: the position
rides along as a third leg of the zip, an `iota` beside the query and
output rows, since it is also the count of tokens the row may read.
Deferred, not rejected: going further with ranges, in particular
`std::views::transform` in place of the elementwise `for` loops. Not
wanted yet.

Status (2026-09-18, later): the residual add is drafted as `add`, an
elementwise `out = a + b` over three matrix views, with `out` allowed to be
either input for the in-place form the block will use. It is the one op with
an exact oracle gate: both adds of every block, 24 sites, fed the dumped
residual and the dumped sublayer output, match the dumped result at `atol =
rtol = 0`, since adding two fp32 values is a single IEEE operation on both
sides and no accumulation order is involved. The sites chain across blocks,
so the sum after block N's MLP add is compared against block N+1's
`ln_1/in`, and the last against `ln_f/in`, which also confirms those dumps
are the same tensor. Next: the embedding gather, the block, the forward pass
with an observer, the head, and greedy decoding.

Status (2026-09-18, embed): the embedding gather is drafted as `embed`,
which writes row `t` of the output as the `wte` row that `ids[t]` selects
plus row `t` of `wpe`. The IDs are a span of the tokenizer's `token_id`, so
the forward header now includes "gpt2_tokenizer.h"; the ID type is the
interface between the two, and inventing a second one would only add a cast
at the seam. The position rows ride along as a third leg of the zip, which
stops at the IDs, so the context-length check (no more IDs than `wpe` rows)
is the load-bearing assert. The oracle gate is exact, like the residual
add's: the bisect prompt's IDs are read from "logits.safetensors"
(`prompt_1/input_ids`, int32), the only dump that holds them, so the test
file's `oracle_dumps` now loads that file too and gained an `ids_of` reader,
which the five-prompt logits gate will reuse. Next: the block, the forward
pass with an observer, the head, and greedy decoding.

Status (2026-09-18, block): the block is drafted as `block`, which runs both
sublayers in place on the residual: `layer_norm`, `linear` to qkv,
`attention`, `linear` back, `add`; then `layer_norm`, `linear` to the hidden
width, `gelu_new` in place, `linear` back, `add`. No new math. Two
aggregates carry the plumbing: `block_params` holds the twelve parameter
views of one `h.N`, named after the tensors with the sublayer prefixed so
the two `c_proj` read apart, and `block_scratch` holds six caller-owned
working views, with `normed` and `sublayer_out` shared by the two sublayers
since each is consumed before the other writes it. Ownership stays outside
the header: the test has an `owned_block_scratch` that allocates the vectors
and hands out the views, and the forward pass decides where owning storage
lives for real. `block` asserts only what the ops cannot: that `normed` and
`sublayer_out` are disjoint from the residual, since a layer norm into the
residual would pass the op's own same-or-disjoint check and destroy the
stream. The oracle gate feeds each block its dumped `ln_1/in` and compares
the residual that leaves it against the next block's `ln_1/in`, or `ln_f/in`
for the last; all twelve pass at `atol = rtol = 1e-4`. Largest absolute
errors are 9.8e-4 (block 2) and 7.3e-4 (block 1), the same one-ulp-near-1000
residual coordinates the MLP path showed, and the rest are under 2.5e-4. Not
decided yet: how the forward pass's observer reaches `attn/out`, `ln_2/in`,
and `mlp/out`, which exist only inside the block; the choices are a callback
parameter on `block` or separate scratch buffers the caller reads afterward,
and the mid-block residual rules out the second on its own. Next: the
forward pass with an observer, the head, and greedy decoding.

Status (2026-09-18, block, second pass): the observer question is closed
without an observer. Steven's proposal: give every intermediate its own
buffer, allowing but not requiring the pairs that were shared to alias, so
the caller reads each dump point back after the call. Applied one step
further to the residual: `block` now takes `out` and `in` like the other
ops, and the residual between the two adds is its own activation, so in
place means passing one view as `out`, `in`, and that buffer.
`block_scratch` became `block_activations`, its fields named after the dump
points (`ln_1_out`, `qkv`, `heads_out`, `attn_out`, `ln_2_in`, `ln_2_out`,
`hidden`, `mlp_out`, `scores`), which also, as Steven noted, removes the
ambiguity a shared `normed` had: the body of `block` shows `ln_1` and `ln_2`
at a glance. The doc block states which pairs may share storage (`ln_1_out`
with `ln_2_out`, `attn_out` with `mlp_out`, `ln_2_in` with `in` or `out`),
and `block` asserts the four overlaps the ops would pass: a layer norm or
projection output on the residual it is later added to. The gate now checks
the five dumped activations too, `ln_1/out` at the layer norm's 1e-5 since
it reads the dumped input directly and the rest at 1e-4, and runs the block
a second time in place, pinning that result bit for bit against the out-of-
place one. Worst absolute errors per dump point: `ln_1/out` 4.3e-6,
`ln_2/out` 4.3e-6, `attn/out` 7.3e-4, `ln_2/in` 7.3e-4, `mlp/out` 9.8e-4,
the block exit 9.8e-4; the large ones are the one-ulp-near-1000 residual
coordinates again. The forward pass therefore needs no callback: its own
gate is the final logits, and per-block inspection is calling `block`
directly, as this test does. Next: the forward pass, the head, and greedy
decoding.

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
  abstraction yet. Superseded 2026-09-07: `os_mmap_file` is the portable
  read-only facade, while the per-OS wrappers stay separate and dissimilar.
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
- The backward pass is hand-derived per op, checked against the stage-0
  gradient dump. No autodiff: not forward-mode dual numbers, not a
  reverse-mode tape. Either would make the backward pass fall out of the
  forward code with no derivation, and the derivation is the learning
  target. Considered and declined after reading
  [tinymind](https://github.com/danmcleran/tinymind), whose `Dual` and
  `RevVar` types are the compact reference for what was declined.

## Open

- Stage-6 model choice, deferrable until stage 6.

## Done

- 2026-09-05: plan and working mode approved. Scaffolding in place: `cuda`
  band (lint scripts and `deps.md`), firewall hosts, oracle script and
  corpus, Unicode range tables. Nothing has run against real weights yet.
