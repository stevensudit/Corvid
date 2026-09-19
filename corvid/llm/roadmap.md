# LLM roadmap

Plan for `corvid/linalg`, `corvid/llm`, and `corvid/cuda`: a transformer inference and adapter-training
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

- Three layers, each with a CPU header and a device counterpart (since
  2026-09-18; before that everything sat in `corvid/cuda/llm/gpt2_forward.h`
  and `.cuh`):
  - `corvid/linalg/linear_algebra.h` (namespace `corvid::linalg`): row and
    matrix arithmetic over `matrix_view` that knows nothing about models:
    reductions, elementwise steps, `linear`, softmax, `add`. Its device
    counterpart is `corvid/cuda/linalg/linear_algebra.cuh`
    (`corvid::cuda::linalg`): launch geometry and the same ops over device
    memory, on `cuda_matrix` from `corvid/cuda/cuda_matrix.cuh`.
  - `corvid/llm/llm_ops.h` (`corvid::llm`): the transformer ops shared by
    every model, `layer_norm`, `gelu_new`, `attention`, `embed_tokens`,
    `logits`, `pick_greedy`, plus `token_id.h`, the tokenizer, and the
    safetensors reader.
    Device counterpart `corvid/cuda/llm/llm_ops.cuh` (`corvid::cuda::llm`).
  - `corvid/llm/gpt2.h`: the GPT-2 architecture, `block` and `forward` with
    their parameter and activation bundles. A later model gets its own file
    beside it and reuses the ops.
- **Two bands in `deps.md`**: `linalg` rests on `containers/utils`; `llm` is
  an apex band so the reader may reach `filesys` and `proto`. The `.cuh`
  files stay under `corvid/cuda/`, outside the layering lint.
- **Rulings on the linalg layer** (2026-09-18, Steven's review of the split):
  the named functions are canonical, since they write into caller-owned
  storage with no temporaries, and operators are sugar for the in-place
  cases only (`matrix_view::add` and `subtract`, also spelled `+=` and
  `-=`); a matrix product operator waits for an owning type. Names say
  what they are: `dot_product`, `linear_projection` (an affine map, the
  bias making it so), `population_variance` beside `sample_variance`, and
  `subtract` beside `add`. The layer knows nothing about tokens or GPT-2:
  the projection widths and the Conv1D versus `nn.Linear` layout note live
  in "gpt-2.md". Ops arrive with their first consumer, so no median or
  geometric mean; the root mean square comes with RMSNorm in stage 6. A
  transpose flag on `linear_projection` comes with the first `nn.Linear`
  checkpoint, also stage 6.
- `tests/portable/`: `.cpp` tests for the CPU pieces (clang, libc++).
- `tests/cuda/`: `.cu` tests for the device pieces. Since 2026-09-18 the
  Windows leg (clang++ as the CUDA compiler, see crossplatform.md) is where
  stage 4 is built and measured, because it is the more convenient box and
  the more direct route to CUDA. The code stays cross-platform: no
  Windows-only or Linux-only headers in device code, and nothing nvcc or
  clang's CUDA frontend would reject, so the Linux leg (nvcc + g++-15)
  keeps building it.
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
with `dot` scaled by `1 / sqrt(width)`, taking `softmax` over those
scores, and accumulating the value rows with `add_scaled`; `attention`
splits the `c_attn` output into its query, key, and value thirds, cuts each
into `head_count` column slices, and runs `attention_head` per slice into
the matching slice of the output. Decisions: the causal mask is the loop
bound (only tokens 0 through `i` are ever scored, nothing is set to minus
infinity); the scratch is one row of `token_count` scores passed in by the
caller, since each token's weights are consumed before the next token's are
computed, and the ops stay allocation-free; `softmax` takes plain spans
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

Status (2026-09-18, forward): the forward pass is drafted as `forward`,
which is everything before the head: `embed` into `out`, the twelve blocks
in place on `out`, then `layer_norm` with `ln_f` in place, so `out` is
`ln_f/out` and doubles as the residual with no extra buffer. `gpt2_params`
holds the model as views: `wte`, `wpe`, a span of `block_params` in `h.N`
order, and `ln_f`; head count stays an argument, as it is for `block` and
`attention`. One `block_activations` is reused by every block, so on return
it holds the last block's. No observer and no callback, per the block
ruling. Loading `gpt2_params` from a safetensors file by name is still test-
side (`oracle_params`, which owns the twelve `block_params` the span points
at); a library loader with a failure return is infrastructure for when
generation needs it. The gate is the first chained one: the bisect prompt's
IDs from "logits.safetensors" through the whole trunk against `ln_f/out`, at
the block's 1e-4. It passes with a largest absolute error of 2.7e-4 and a
largest relative error of 1.1e-2, the latter on a coordinate near zero;
twelve blocks of accumulated one-ulp residual differences wash out through
the final layer norm rather than compound. Next: the head (`ln_f/out`
against every `wte` row, the transposed-weight decision), greedy decoding,
then the five-prompt logits gate and the greedy text from the manifest.

Status (2026-09-18, head): the head is drafted as two ops. `token_logits`
scores one token: its row of `ln_f/out` dotted against every row of `wte`,
the same table that embedded the input, with no bias, into a row of one
logit per vocabulary entry. `logits` loops it over every row of `ln_f/out`
into [T, V]. Steven's ruling: the per-token op is the one generation calls,
on the last row only; the all-rows op exists because the oracle dumps every
row, the same split as the activations struct, where the tests want every
step and generation does not. Not a transposed-weight variant of `linear`:
the math is the same projection with the weight read the other way round
(`logits[t][v] = sum over c of h[t][c] * wte[v][c]`, against `linear`'s
`weight[i][j]`), which is what tied weights means and becomes one transpose
flag on the BLAS call in stage 4, but on the CPU the loop follows the
storage, a `dot` per vocabulary row, and the intent is scoring, not
projecting. Gates: the head alone on the dumped `ln_f/out` against the
dumped logits, largest absolute error 3.5e-4 on values of magnitude around
100 (largest relative error 2.6e-6); and the first end-to-end gate, IDs
through `forward` and `logits` against the dumped logits of all five prompts
(1, 14, 10, 27, and 25 tokens), every one passing at `atol = rtol = 1e-4`
with largest absolute errors of 3.4e-4, 4.0e-4, 4.3e-4, 7.9e-4, and 2.9e-4.

Performance finding from the same run: the head is the hot spot. Fourteen
tokens of `logits` take 0.44 s while the whole twelve-block trunk for the
same tokens takes 0.19 s. The cause is `dot`'s single accumulator: strict
IEEE ordering (no fast-math, by policy) forbids the compiler from
reassociating the 768-term sum, so it runs as one serial chain of fused
multiply-adds at one instruction latency per element, and the head runs
50257 such chains per token. A standalone measurement with the project's
flags (clang, `-O3 -march=native`) over 14 tokens: the serial `dot` costs 28
ms per token; eight independent partial sums in an index loop cost 3.5 ms
per token, an 8x speedup, and sixteen lanes 3.3 ms; the same eight lanes
written with `views::chunk` and `zip` cost 38 ms, because the view machinery
hides the fixed lane count from the vectorizer, so this is the one loop
where the zip ruling yields to an index loop with a body comment saying why.
The change alters summation order, so the attention and logit gates shift
within their tolerances. Applied on Steven's go-ahead, below. `sum`, `mean`,
and `variance` have the same shape but run once per row, not 50257 times per
token, and are not worth touching. Next: greedy decoding and the greedy text
from the manifest.

Status (2026-09-18, dot and greedy): `dot` is the plain zipped reduction it
always was, now under `PRAGMA_FP_REASSOCIATE`, a new helper in
"crossplatform.h" that expands to `#pragma clang fp reassociate(on)` for the
rest of the block and to nothing elsewhere. Steven's ruling over the eight-
lane index loop that was drafted first: a compiler-specific pragma around a
simple loop is localized, the lanes are not, and cl is supported
reluctantly, so its serial `dot` is accepted. Measured in the standalone
bench with the project's flags, the pragma ties the lanes exactly (3.4 ms
per token against 28 serial) and the global alternative, `-fassociative-
math` and kin, was not wanted because it changes every floating-point
expression in the build. Measured on the clang leg: the head on 14 tokens
went from 0.44 s to 0.10 s, the five-prompt end- to-end gate from 1.0 s to
0.38 s, and the gates moved within tolerance (head alone: largest absolute
error 3.5e-4 to 1.7e-4; attention path: 2.4e-4). Greedy decoding is
`greedy`, the vocabulary entry with the largest logit, first on a tie, which
is also what `torch.argmax` returns. The demonstration is the manifest's
greedy continuation: from prompt 1's IDs, twenty steps of `forward`,
`token_logits` on the last row, and `greedy`, appending each pick; the
twenty IDs match the manifest exactly and decode through `gpt2_tokenizer` to
its text, ' the "Moon Express" and was a test of the technology that would
eventually lead to the first human'. The generation loop lives in the test,
since every step re-runs the whole model over the IDs so far and the stage 4
KV cache changes that loop anyway. With this, the stage's done-when is met:
every per-layer activation of the bisect prompt and the logits of all five
prompts match within tolerance, and the text comes out.

Timing notes for the record. The greedy demonstration costs 2.3 s on the
clang leg, twenty full passes over 14 to 33 tokens with no cache, which is
the one test in the file over a second; Steven decides whether that stands.
The trunk's 0.19 s for the 14-token pass is mostly not compute: an op-by-op
profile on the real weights shows 151 ms for the first pass and 64 ms for
every later one, the difference being first-touch page faults on the memory-
mapped weight file, so a warm pass costs about 4.6 ms per token and `linear`
is 91 percent of it (the two MLP projections 30 percent each, `c_attn` 22,
the attention `c_proj` 7), `gelu_new` 8 percent, everything else under 2.
Feature-major loop order in `linear`, each weight row read once per pass and
applied to every token, measured only 15 percent faster than the current
token-major order at T = 14, so the axpy form is not bandwidth-starved at
this size; a real speedup there means register tiling over several tokens or
several threads, neither a small change. The cl leg is slower throughout,
and with the pragma a no-op there its `dot` is the serial one again: the
whole test binary takes 15.4 s against 4.8 s on clang. Steven's ruling: cl
is supported reluctantly and is not optimized for; the stage 4 comparison is
measured against the clang leg. Deferred by ruling (2026-09-18):
parallelizing `linear` across tokens or output columns, which could be an
order of magnitude on this machine but is an architectural shift and out of
stage 3's scope; and replacing `std::tanh` in `gelu_new` with a vectorizable
approximation, low-hanging but a tolerance question, so also deferred.

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

Status (2026-09-18, device linear): the first device op is `linear` in
"gpt2_forward.cuh", namespace `corvid::cuda::llm`, over a new `cuda_matrix`
(a `cuda_buffer<float>` plus a `matrix_extent`, packed, uploaded from and
downloaded to a packed `matrix_view`) and the general GEMM overload of
`cublas_handle::multiply`, which the square one now delegates to. The
row-major lesson, recorded in the op's body: cuBLAS reads each row-major
buffer as its column-major transpose, and transposing `out = in * weight`
gives `out^T = weight^T * in^T`, so the buffers multiply as stored with no
transpose flags, `m` the output features, `n` the tokens, `k` the input
features, and each leading dimension the row-major row length. The bias is a
small kernel that writes it across every output row, and the GEMM accumulates
onto it with `beta = 1`, the same shape as the CPU op's copy then
`add_scaled`; a rank-1 update or a `k = 1` GEMM against a ones vector would
do the same and were not chosen because the fill is the shape every later
kernel takes. The op returns a `bool`, consuming a refused launch's error;
cuBLAS has no last-error channel, so a richer status waits for a second
consumer. The gate is the CPU test's MLP path with both projections on the
device and `gelu_new` on the host between them, since no dump sits between
`c_fc` and `c_proj`; every block matches `mlp/out` at 1e-4, and a
hand-computed two-by-three case pins the exact values. The shared oracle
helpers (paths, tensor views, allclose, dimensions) moved from the CPU test
into "tests/gpt2_oracle.h" so both tests read them. Test executables are named
by file stem, so the device test is "cuda_gpt2_forward_test.cu", matching
"cuda_saxpy_test.cu". Timing: the two cases take 0.32 s in-process (8 to 11
ms per block, dominated by the weight uploads), 1.7 s under ctest with CUDA
context and cuBLAS initialization. `cublasSgemm` under the default math mode
is full fp32, not TF32, so the tolerance is the CPU one.

Status (2026-09-18, device gelu_new): the first hand-written kernel. The
scalar `gelu_new(float)` in "gpt2_forward.h" is shared rather than copied:
it carries `CUDA_HOST_DEVICE`, a new macro in "crossplatform.h" that expands
to `__host__ __device__` under a CUDA compiler and to nothing elsewhere, so
the CPU header is the one definition of the formula and the kernel calls it.
The kernel is one thread per element, and the launch geometry (256 threads
per block, `blocks_for(size)`) moved into its own region since `linear`'s
bias fill uses the same shape. In-place is allowed, as on the CPU. Gated on
the CPU test's seven reference values, an in-place check, and the MLP path,
which now runs all three ops on the device with no host round trip. The
`cuda_ptr` review that preceded this op renamed it `cuda_buffer`, since it
owns a sized allocation and is no pointer; `count()` became `size()`, the
span and array transfer overloads assert the host side fits, and the LLM's
`device_matrix` became `cuda_matrix` to match the band's `cuda_` prefix.

Status (2026-09-19, review pass on the split): `inverse_std_dev` is the
reciprocal of a new `std_dev`, and `softmax_row` became `softmax` over
`span_in` and `span_out`, since it takes plain spans. `layer_norm` lost its
default epsilon; `layer_norm_eps` lives in "gpt2.h", where `block` and
`forward` pass it. `embed` split into the generic `embed_tokens`, a row
lookup by ID, with `forward` adding the first T rows of `wpe` through `add`.
`token_logits` and `logits` take `vocab` in place of `wte`, and `greedy`
became `pick_greedy`. `cuda_matrix` moved out of the device linalg header
into "corvid/cuda/cuda_matrix.cuh" beside `cuda_buffer`, ahead of the
strided view of checkpoint 3. The `multiply` doc in "cuda_cublas.cuh"
describes column-major GEMM only; the row-major reading is
`multiply_row_major`'s. Rulings: `token_id` stays 32-bit until a vocabulary
needs more, and the element-type dispatch of checkpoint 2 is an overload
set, one per cuBLAS routine, with a traits class deferred until bf16 needs
`cublasGemmEx`.

Status (2026-09-19, checkpoint 2, CPU half): "linear_algebra.h" is a
template on the element type. The row ops take any contiguous range and read
the element type from it, so arrays, vectors, spans, and `enum_span` rows
all pass unchanged; the matrix ops deduce it from `out` alone and take their
inputs as `const_view_t<T>`, a `matrix_view<const T>` behind
`std::type_identity_t` so a mutable view converts. The constraints are
`Arithmetic` and `Floating` in "concepts.h" beside `Integer`, with the range
forms `ArithmeticRange`, `FloatingRange`, `MutableRange` (which
`MutableByteRange` now composes), and `SameElement` beside `ByteRange`, and
`element_of_t<R>` in "traits.h"; only `const_view_t` stays in the linalg
header, since it names `matrix_view`. Sums, products, `add_scaled`,
`linear_projection`, `add`, and `subtract` take `Arithmetic`; the mean,
variances, standard deviations, `standardize`, and `softmax` take
`Floating`. Return types stay spelled as `element_of_t<R>` rather than
`auto`, and `std::ranges::size` is accepted as the price of taking ranges.
Deferred: an optional accumulator-type parameter (`mean<float>(int_span)`),
which the first quantized consumer brings, with the promotion inside the
loop. The test is a `TEMPLATE_TEST_CASE` over float and double, 16 cases,
green on clang and cl, with asserts live under the IDE debug tree. The
device half (cuda_matrix<T>, the Sgemm/Dgemm overloads, the device
linear_projection) is next.

Status (2026-09-19, checkpoint 2, device half): `cuda_matrix<T>` in
"cuda_matrix.cuh" takes any element type, with `element_t`, `view_t`, and
`const_view_t` aliases and the float TODO retired. In "cuda_cublas.cuh" the
`GemmElement` concept names the types cuBLAS has a GEMM routine for, float
and double, and `multiply`, `multiply_row_major`, and the square `multiply`
are templates on it that dispatch through a private `gemm` overload pair to
`cublasSgemm` and `cublasDgemm`; `alpha` and `beta` are
`std::type_identity_t<T>` so a float literal serves a double call. An
integral element type is a constraint failure at the call until
`cublasGemmEx` arrives with the quantized path. The device
`linear_projection` and its `fill_rows` kernel are templates on
`GemmElement` too, leaving only the non-packed-view TODO for checkpoint 3,
and the device `gelu_new` stays `cuda_matrix<float>`. The CUDA linalg test
is a `TEMPLATE_TEST_CASE` over float and double, so the Dgemm path runs; it,
the CUDA llm ops test, and the notest matmul program are green. Checkpoint 2
is complete.

Status (2026-09-19, gelu_new templated): the one op that had stayed float
follows the linalg layer. The scalar `gelu_new` is a template on `Floating
T`, with `gelu_tanh_scale_v<T>` and `gelu_cubic_coeff_v<T>` as variable
templates in the style of "arithmetic.h", so the CPU matrix form and the
device kernel and op are templates for free; the scalar test runs over float
and double. The other LLM ops and the GPT-2 layer stay float until the bf16
stage changes the activation type.

Status (2026-09-19, cuda_handle const policy): reviewing `cuda_matrix`'s
accessors surfaced that a const `cuda_buffer` handed out a mutable device
pointer, since `cuda_handle::get()` returned the raw handle regardless. The
fix is a policy on the base rather than a shadow in the derived class:
`cuda_handle<H, Destroy, const_propagation>` with `const_propagation {
shallow, deep }` in "bool_enums.h", shallow (the default) for opaque handles
the API takes by value, deep for a pointer to data, where a const owner
hands out `const_handle_t`, a pointer to const. `get()`, the conversion, and
`operator*` are mutable/const pairs; `cuda_buffer` derives with deep, and
`cuda_matrix::get()` and `buffer()` are deducing-this on top. Pinned by
static_asserts in the buffer test; the full suite of 86 is green.

Status (2026-09-19, review round on checkpoint 2): the LLM op names lead
with a verb: `attention_head` and `attention` are `attend_head` and
`attend`, and `token_logits` and `logits` are `compute_token_logits` and
`compute_all_logits`, the last saying that it scores every position where
the other scores one. The device linalg layer gained `gemm` in
"linear_algebra.cuh", the mid-level wrapper between `cublas_handle` and the
ops: `out = scale * op(a) * op(b) + bias_scale * bias` over `cuda_matrix`,
with an optional `bias` row (null makes the prior contents of `out` the
addend), `scale` and `bias_scale` defaulting to one, and the transpose flags
last; `linear_projection` is one call to it. The bias broadcast is explained
in the body: cuBLAS has no vector addend, so the row is written across `out`
before the GEMM and `beta` reaches it there. The `multiply` doc in
"cuda_cublas.cuh" states the operand shapes and leading dimensions directly,
`multiply_row_major` derives its swap from `(A * B)^T = B^T * A^T`, and
`GemmElement` names the half and complex GEMMs it leaves out.
"reflow_comments.py" also flags orphan tails now.

Status (2026-09-19, gemm split): `gemm` is two overloads. The matrix-addend
form, `gemm(blas, out, a, b, options = {}, addend = empty)`, computes
`out = scale * op(a) * op(b) + addend_scale * addend`, with the scalars and
transpose flags in a `gemm_options<T>` aggregate named at the call site
(`{.scale = 2}`, `{.op_a = transpose}`), so the addend can trail with a
default and a transposed product with no addend spells no sentinel;
an empty `addend` (the default) is the plain product, `out` itself
accumulates, and any other matrix is copied into `out` first, since cuBLAS's
`C` is the one matrix it reads and writes. The vector-bias form takes a
required `cuda_buffer` bias before the options, broadcasts it into `out`,
and calls the matrix form with `out` as the addend; `linear_projection` is
that call. The split
retired the earlier footgun where a plain product needed an explicit zero
scale. Supporting pieces: `matrix_extent::transposed()` in "matrix_view.h",
`cuda_matrix(std::nullptr_t)` for the empty addend, and the device-to-device
`cuda_buffer::load(const cuda_buffer&)` that retires the buffer's TODO.

Status (2026-09-19, checkpoint 3, cuda_matrix_view): `cuda_matrix_view<T>` in
"cuda_matrix.cuh" is the non-owning, possibly strided window the device ops
take: a device pointer, an extent, and a stride in elements between row
starts, with `subview(coord, extent)` cutting a window that carries the
parent's stride, so the QKV split is three subviews of one buffer. Transfers
live on the view (`load` from a host `matrix_view` or another device view,
`store` to a host view) and go through `cudaMemcpy` when both sides are
packed and `cudaMemcpy2D` otherwise, so copying through a packed view costs
nothing extra; `cuda_matrix` lost its own `load`/`store` and gained `view()`
plus implicit conversions to views. `gemm`, `linear_projection`, and the
device `gelu_new` take `cuda_matrix_view<T>` for `out`, which deduces `T`,
and `const_view_t<T>` (a read-only view behind `std::type_identity_t`) for
inputs, so an owning matrix converts at the call; an owning `out` is spelled
`out.view()`, since deduction cannot see through a conversion. Leading
dimensions are the strides, closing the non-packed TODO, and the elementwise
kernels map their flat index through `cuda_kernel::strided_offset`. The
`cublas_handle` multiplies take device pointers now, as cuBLAS does, since a
view has no buffer to hand over. The empty addend is a default-constructed
view, so the one-round `cuda_matrix(std::nullptr_t)` and the buffer-level
device copy went away. Tests: the linalg test gained a strided case (product
into a window of a wider matrix with the neighboring column untouched, a
bias broadcast into it, pitched store and load of the window, and a strided
device-to-device addend).

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

## Candidate quests

Fun, not blockers, and not small; recorded so they are considered on their
own merits later rather than slipping in as side quests:

- expression templates over `matrix_view` and spans, so `in * weight + bias`
  can be written as operators without a temporary. Until then the named
  functions are canonical and the operators are the in-place ones only;
- a `number_span` that subsumes `enum_span` and carries the arithmetic
  operators for rows, the same question as the matrix operators one level
  down.

## Deferred

Not planned, recorded so they are not re-litigated as scope:

- adapter hot-swap and multi-adapter serving in one process;
- pretraining from scratch (llm.c-style GPT-2 reproduction);
- quantized weights and a GGUF reader. They arrive together: reading GGUF
  is mostly implementing the dequantization of each block format, which is
  a self-contained study of its own. If a GGUF-only model is ever needed
  sooner, the oracle script can unpack it to bf16 safetensors instead;
- a cross-platform mapped-file abstraction;
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
  built there. Superseded 2026-09-18: stage 4 is built and measured on the
  Windows leg, and the code is cross-platform with CUDA required on both.
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
