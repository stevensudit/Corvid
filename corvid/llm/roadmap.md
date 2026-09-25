# LLM roadmap

Plan for `corvid/linalg`, `corvid/llm`, and `corvid/cuda`: a transformer
inference and adapter-training stack written from scratch in Corvid-flavored
C++23 and CUDA, targeting one RTX 4090. The destination is the "weights
versus context" experiment from the personalized-memory design: compile
plain-English dispositional traces into a LoRA and measure whether the
adapted model behaves differently from the same base model given the traces
in context.

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
  - `corvid/llm/gpt2.h`: `gpt2_model`, the parsed weights file (parameter
    views, dimensions, the head-count table), shared by both engines.
    `corvid/llm/gpt2_engine.h` (`gpt2_engine`, CPU) and
    `corvid/cuda/llm/gpt2_engine.cuh` (`corvid::cuda::llm::gpt2_engine`,
    device) run it: `apply_block`, `forward`, `next_token`, `generate`,
    with their activation bundles. A later model gets its own files beside
    them and reuses the ops.
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
view, so the one-round `cuda_matrix(std::nullptr_t)` went away, while the
buffer-level device copy stays as functionality buffers deserve on their
own. Tests: the linalg test gained a strided case (product into a window of a
wider matrix with the neighboring column untouched, a bias broadcast into it,
pitched store and load of the window, and a strided device-to-device addend).

Status (2026-09-19, view base and owning overloads): the shape math of the
two views lives once in `matrix_view_base` ("matrix_view.h"): extent and
stride, the count accessors, `is_packed`, the index intervals, and the
protected offset, footprint, and window helpers that both `subview` forms
resolve through, so `matrix_view` and `cuda_matrix_view` add only their
storage (a host span, a device pointer) and what touches it. Every device op
also has an overload taking an owning `cuda_matrix<T>&` for `out` that
forwards to the view form, so `gemm(blas, out, a, b)` reads the same for a
matrix and a view, and `cuda_matrix::subview` passes through to its view for
the same reason.

Status (2026-09-19, span in the view base): `matrix_view_base<T>` holds the
storage as well, a `std::span<T>`, so the constructors, the read-only
conversion, `as_span`, and both `subview` forms live once, and `subview`
returns the derived type through a deducing-`this` template. `matrix_view`
keeps only what dereferences (element access, rows, the in-place arithmetic)
and `cuda_matrix_view` only `get` and the transfers. Each derived class
inherits the base constructors, which is what lets the base build one. The
device view gained the host view's bounds asserts at construction and
slicing, since a span carries the length a bare pointer did not, and
`cuda_matrix::view()` builds it from the new `cuda_buffer::as_span()`.

Status (2026-09-19, device add): `add` in "linear_algebra.cuh" is the
elementwise `out = a + b` over three device views, one thread per element
through `strided_offset` as `gelu_new` maps its elements, with the CPU op's
in-place-or-disjoint contract and, since the view base now carries a span,
the CPU op's aliasing asserts word for word. Gated on the CPU test's
hand-computed rows in all three placements plus a strided-window case, and
on the same exact 24-site residual gate at `atol = rtol = 0`, since one fp32
add is one IEEE operation on the device as well.

Status (2026-09-19, device layer_norm): the first block reduction. The kernel
takes one block per row with the elementwise ops' 256 threads, each thread
walking the columns at its index and every 256 after it (3 each at 768), and
sums twice across the block, the values for the mean and then the squared
deviations from that mean for the variance, so the arithmetic is the CPU
op's two-pass form rather than the mean of squares minus the squared mean.
The reduction plumbing is new and is what softmax will reuse: `cuda_warp::sum`,
an xor butterfly that leaves the warp total in every lane, and
`cuda_block::sum` in the new "cuda_block.cuh", which parks one partial per
warp in shared memory and has warp 0 fold them, with two block syncs per
call so back-to-back calls are safe. The scalar `standardize` and
`scale_shift` gained `CUDA_HOST_DEVICE` so the kernel calls the CPU formulas,
as `gelu_new` does. `weight` and `bias` are `cuda_buffer`s, as the GEMM's
bias is, and `eps` sits behind `std::type_identity_t` so a float literal
converts. Gated on the CPU test's hand-computed rows in float and double, in
place, and on the same 25-site oracle gate at 1e-5. The reductions have their
own unit tests, one warp and eight warps, two sums in a row.

Status (2026-09-19, review round on add and layer_norm): the reductions moved
out of the primitive wrappers into "cuda_reduce.cuh", as
`cuda_reduce::warp_sum` and `cuda_reduce::block_sum`, so `cuda_warp` and
`cuda_block` stay wrappers over intrinsics. `cuda_block` is now the home of
`sync()` over `__syncthreads`, and `cuda_warp` gained `warps_in_block()` and
`max_warps_per_block` beside `warp_id()`. The block sum's doc no longer
states the 1024-thread limit, which is CUDA's own and now lives with the
constant it explains. `subtract` arrived on the device beside `add`, both as
two-line wrappers over one `combine_elements` kernel templated on the
operator, which is passed as `std::plus<>` or `std::minus<>`; clang treats
constexpr functions as callable from device code, so the standard functors
work in a kernel without a device-marked copy. The kernel's doc now says how
threads map to elements (thread `i` takes element `i` in row-major order, so
a warp reads 32 consecutive columns), and the layer norm's says that a thread
past the last column still joins both block sums.

Status (2026-09-19, DeviceMatrixLike): the owning overloads are gone. Every
device op takes its output as a forwarding reference constrained by
`DeviceMatrixLike` ("cuda_matrix.cuh"), which admits a `cuda_matrix<T>` or a
`cuda_matrix_view<T>` for a non-const `T`, and coerces it on the first line
with `const auto& out_view = out.as_view();`, the idiom "splitting.h" uses for
`StringViewLike`. `cuda_matrix::view()` became `as_view()`, and the view gained
an `as_view()` that returns itself, so the local binds a reference either way.
The element type comes from `device_element_t<Out>` rather than deduction, so
`std::type_identity_t` left the CUDA headers, `const_view_t` is a plain
`cuda_matrix_view<const T>`, the inputs are spelled `input_view_t<Out>` over it
and the bias and weight rows `input_buffer_t<Out>`, and the element constraint
moved to a trailing `requires`. Deep const survives: a `const cuda_matrix`
fails the concept because its mutable conversion is a non-const member, while a
`const cuda_matrix_view` passes, which is right for a shallow-const view; a
static_assert table in the linalg test pins both sides.

Status (2026-09-19, device embed_tokens): the embedding gather is
`embed_tokens` in "llm_ops.cuh", taking `out`, the IDs as a
`cuda_buffer<token_id>` the caller uploads (generation will keep them on the
device), and the table as a view. One thread per output element, each computing
its row and column once and reading the table row that the row's ID names, so a
warp reads and writes 32 consecutive columns. The enum's `operator*` is
`constexpr`, which clang CUDA lets device code call, so the kernel reads the
underlying index the way the CPU op does. There is no element constraint beyond
`DeviceMatrixLike`, since a gather is a copy. The per-ID bound is a
precondition with no device-side check: no kernel in the tree asserts, and a
device `assert` would fault the context with `cudaErrorAssert` rather than the
process. Tests: the hand rows over float and double, and the oracle case, which
adds the first T rows of the position table through the device `add` and
matches `embed/out` exactly.

Status (2026-09-19, kernel views): the flat-index kernels are gone. Every
elementwise kernel now launches a 2-D grid, rows along y and columns along x,
from `grid_for(extent)` in "linear_algebra.cuh" (overloaded for anything with
an `extent()`), so a thread's row and column are its coordinates and no kernel
divides. Two kernel-side types in "cuda_matrix.cuh" carry that: `kernel_coord`,
whose default constructor reads the calling thread's indices and whose
`is_within(matrix_extent)` is the bounds guard, and `kernel_matrix_view<T>`,
the pointer and stride a `cuda_matrix_view` converts to at a launch, indexed as
`out[at]` or `out[row, col]` with the C++23 multidimensional subscript.
Launches spell the conversion, `kernel_matrix_view{out_view}`, so the kernel's
element type deduces. `cuda_kernel::strided_offset` is deleted. The
one-block-per-row `apply_layer_norm` takes the same views and indexes `in[row,
c]`. The grid's y dimension caps at 65535 rows, asserted in `grid_for` as
`max_grid_rows`; a backward pass over Qwen's 151936-row embedding will need a
multi-row block or a row loop. The 1-D versus 2-D launch question is thereby
settled in favor of 2-D.

Status (2026-09-19, block max): `cuda_reduce` gained `warp_max` and `block_max`
beside the sums, for the row max that softmax subtracts before exponentiating.
The four public reductions are two-line wrappers over two private folds,
`warp_reduce(value, op, mask)` (the xor butterfly) and `block_reduce(value, op,
identity)` (per-warp partials in shared memory, warp 0 folds them), with
`std::plus<>` and `std::ranges::max` as the ops; clang CUDA calls both from
device code because they are constexpr. The identity is what a thread with
nothing to contribute brings, and what warp 0's spare lanes read: zero for a
sum, `numeric_limits<T>::lowest()` for a max. The test contributes each
thread's index less 1000 to the maxes, all negative, so a lane that wrongly
brought zero would win.

Status (2026-09-19, device softmax): `softmax` in "linear_algebra.cuh" turns
each row of `in` into weights that sum to 1, one block per row as
`apply_layer_norm` launches, with the row max from `block_max` (threads past
the last column bring `lowest()`), the exponentials summed by `block_sum`, and
a final divide. In place is allowed. Causality is not its business: the CPU
`attend_head` runs softmax over each row's causal prefix, and the device
`attend` will instead mask the columns after the diagonal with `lowest()`
before calling it, so their exponential is zero. Tests cover the CPU test's
rows, in place, a strided window whose filler column survives, and a
1000-column ramp where each thread folds four columns, checked by the row sum
and the exp(0.01) ratio of neighbors.

Status (2026-09-19, kernel column range): the block-stride loop that
`apply_layer_norm` and `apply_softmax` spelled by hand six times, `for (auto c
= first; c < cols; c += step)`, is now `for (const auto c : columns)` over a
`kernel_col_range` (the user's design, in "cuda_matrix.cuh" beside
`kernel_coord`). It is an aggregate whose `cols` the kernel supplies and whose
`first` and `step` default from the thread index and block width, walked by a
range-for whose `end()` is a sentinel of a different type than the iterator,
compared as "column at or past the end". It costs nothing: the PTX of both
kernels at O2 is identical before and after, and a probe kernel gave the same
result. A test walks four threads over ten columns and pins each thread's
visits.

Status (2026-09-19, device attend): `attend(blas, out, qkv, head_count,
scores)` in "llm_ops.cuh" has the CPU op's contract and shape table, with
`scores` a caller-owned T x T scratch instead of the CPU's one row. Where the
CPU op walks each token's causal prefix, the device takes a head's whole T x T
score matrix in four launches over column subviews of `qkv` and `out`: a `gemm`
of the queries against the transposed keys with `.scale = 1 / sqrt(D)` and
`.op_b = transpose`, the new `causal_mask` (every element whose column exceeds
its row becomes negative infinity, the well-known -inf mask, so the row softmax
gives it zero weight), the row `softmax` in place, and a `gemm` of the weights
against the values into the head's slice of `out`. Twelve heads are 48 launches
per block; a fused attention kernel stays deferred with the other fusions until
the staircase is complete. Tests: the causal mask on a 3 x 3 of ones, the CPU
napkin example for one head of width two and two heads of width one from the
same `qkv`, and the oracle case, which runs `c_attn`, the heads, and `c_proj`
on the device for every block and matches `attn/out` at 1e-4.

Status (2026-09-20, device block, forward, and head): the composition is
"gpt2.cuh" (namespace `corvid::cuda::llm`), mirroring "gpt2.h" op for op.
`block_params` and `gpt2_params` are owning twins of the CPU bundles, one
`cuda_matrix<float>` or `cuda_buffer<float>` per weight, each constructed from
its CPU views so the weights upload once; `cuda_buffer` gained the
allocate-and-upload constructor from a host span that `cuda_matrix` already
had. `block_activations` stays caller-owned views, as on the CPU, with
`scores` the device `attend`'s T x T scratch. `block(blas, out, in, params,
acts, head_count)` and `forward(blas, out, ids, params, acts, head_count)`
run the same steps as their CPU twins (the CPU docs keep the step tables),
with the IDs a `cuda_buffer<token_id>` as `embed_tokens` takes them and
`layer_norm_eps` shared from "gpt2.h"; each returns the ops' `bool`, so a
refused launch stops the chain. The head is one op, `compute_logits` in
"llm_ops.cuh", a `gemm` of the trunk against the transposed embedding
(`.op_b = transpose`); the CPU's per-token form is the same call over a
one-row subview, so there is no second op. Greedy picking stays on the CPU:
the test downloads the last token's row of logits (200 KB) and calls
`pick_greedy`, since cuBLAS's `amax` takes absolute values and a device argmax
needs a pair reduction the KV-cache step can bring with the generation loop.
The oracle loaders (`block_params_of`, `oracle_params`, the manifest's greedy
continuation, decoding through the tokenizer) moved from the CPU test into
"gpt2_oracle.h" so both tests read them. The stage-4 gate, "cuda_gpt2_test.cu",
mirrors the CPU one and passes at the CPU tolerances on the first run: every
block from its dumped `ln_1/in` (exits within 6.7e-4, block 11; `ln_1/out`
within 1.4e-6 at 1e-5), in place bit for bit; the forward pass against
`ln_f/out` (6.9e-5); the five prompts' logits (worst 3.5e-4, prompt 3); and
the twenty greedy IDs and their text. Timing, in-process: a 14-token pass with
logits takes 10 to 15 ms, the model upload 0.23 s per test case, and the
twenty-step greedy loop 0.42 s against the CPU's 2.3 s. Next: batched-GEMM
attend, the KV cache, bf16, and tokens per second against llama.cpp.

Status (2026-09-20, the model): the generation loop moved out of the tests
into `gpt2_model`, one in "gpt2.h" and one in "gpt2.cuh" with the same
surface. Steven's rulings: single-phase construction from a
`safetensors_file` that throws on a missing or misshaped tensor, since the
failure is fatal and a two-phase object would carry an unusable state (a
"marsupial object"); the head count comes from a four-entry table keyed on
the block count, since the tensor shapes give every other dimension but not
how the width divides into heads, and it is fine for the GPT-2 engine to
know GPT-2's sizes; the tokenizer stays the separate `gpt2_tokenizer`, used
with the model rather than part of it, so the model takes and produces token
IDs alone; `generate` stops at `end_of_text` without appending it, so the
result always decodes; and the device model downloads the last token's row
of logits and picks on the host, reusing `pick_greedy`. The CPU model views
the file, which must outlive it; the device model uploads at construction and
the file can then be closed. `next_token(out, ids)` runs one pass and picks,
failing on no IDs, more than the context holds, or (on the device) a refused
launch or transfer; `generate(ids, count)` appends up to `count` picks in
place. The block-count probe (`h.N.ln_1.weight` until absent) and the shape
checks are the deferred library loader. The activation storage the tests
owned moved into the library as `block_activation_buffers` in each namespace,
sized by token count, width, and MLP width, and allocated per `next_token`
call until the KV cache brings a persistent scratch. `matrix_extent` gained a
defaulted equality, so the paired row-and-column asserts became one
comparison. The tests' own loaders (`block_params_of`, `oracle_params`) are
gone; the gates read `model.params()`, and the greedy gates call `generate`.
`end_of_text` (50256) lives beside `layer_norm_eps`.

Status (2026-09-20, model and engines): Steven's structural review of the
above found two smells, both fixed the same day. First, `block` and
`forward` were free functions with no caller but the model, so they are now
engine methods, `apply_block(out, in, block_index, acts)` and `forward(out,
ids, acts)`, and the parameter bundle and head count left their signatures;
the ops in "llm_ops.h" stay free because stage 6 reuses them. Second, the
device model was built through the CPU model to get its views, which is the
wrong dependency. The fix is one shared `gpt2_model` in "gpt2.h" that owns
the `safetensors_file` (moved in), views every parameter, derives the
dimensions from the tensors, and holds the head-count table; it knows the
file layout and nothing about running it. The engines are `gpt2_engine` in
"gpt2_engine.h" (CPU, holding a reference to a model that must outlive it)
and "gpt2_engine.cuh" (device, uploading at construction and keeping
nothing, so the model may then be destroyed). No close method: destroying
the model closes the file, and a model that had closed its file but still
existed would be the two-phase object's mirror image. The typed lookups
moved to the reader as `safetensors_file::find_matrix<T>(name, expected)`
and `find_vector<T>(name, size)`, returning empty on a missing, mistyped, or
misshaped tensor, since they know nothing about GPT-2; the model throws
with the tensor's name on an empty result, and the test header's
`matrix_of` and `vector_of` are thin wrappers over them. The host
`gpt2_params` struct folded into the model's members and the device one
into the engine's. The device five-prompt gate uploads `wte` for itself,
since the engine scores only the last token. Timing is unchanged.

Status (2026-09-20, `gpt2_model` as a struct): Steven's second pass on the
model. `block_params` is nested inside it, since it is only the format the
blocks are stored in. Every data member is public and const, `weights`
included, so the model is a struct of views with the code that fills it
behind `private:`; the four derived sizes (`width`, `hidden_width`,
`vocab_size`, `context_length`) stay functions, since a derived quantity is
not stored beside its source, while `head_count` is a stored member because
it comes from the table, not the views. Construction is the static factory
`gpt2_model::load(safetensors_file&&)`, which computes every view into
locals and then returns a designated aggregate, moving the file in last;
the views stay valid because they point into the mapping, whose address the
move preserves. With every member const the struct is immovable, and the
prvalue return lands in the caller's object by guaranteed elision, so the
result can only initialize a new model. That suits it, since the CPU engine
binds it by reference and moving it from under the engine was already a
bug. The engines read the members directly (`model.wte`,
`model_.blocks[block_index]`) in place of the accessors. The two constants,
`layer_norm_eps` and `end_of_text`, are public statics of the struct, so
they read as GPT-2's rather than the `llm` namespace's, and a hand-built
`gpt2_model` (an aggregate, so a test could assemble one) is allowed rather
than fenced off.

Status (2026-09-20, safetensors factories): `safetensors_file` lost its
two-phase construction, the same marsupial shape the model shed. `load(const
os_file&)` and `parse(std::span<const std::byte>)` are static factories that
return a fully built reader or throw `std::runtime_error`; there is no
default constructor and no way to hold an unloaded reader. `load` still maps
the file and keeps the mapping, and `parse` still views a caller-owned image,
so the two sources stay distinct, which is why they are named factories
rather than two constructors told apart by parameter type. The validation
walk is unchanged, now the private `do_parse` behind `parse`; the "leaves the
object as it was" guarantee has nothing left to guarantee. The test fixture
followed: `oracle_dumps::load()` returns the three readers or skips, and the
malformed-file cases in "safetensors_test.cpp" check for the throw. The
GPT-2 empty-weights test parses a ten-byte image with an empty header, which
is a valid file with no tensors.

Status (2026-09-21, batched-GEMM attend): the device `attend` no longer loops
over the heads. Its four launches now cover every head at once, down from
four per head (48 per block for GPT-2): a batched GEMM of the queries against
the transposed keys, the causal mask, the row softmax, and a batched GEMM of
the weights against the values. `cublas_handle` gained `multiply_batched` and
`multiply_batched_row_major` over `cublasSgemmStridedBatched` and its double
twin. The linalg layer gained `gemm_batched(blas, out, a, b, batch,
options)`, where each operand is one matrix holding every instance as an
equal piece and `gemm_batch` names the count and the `matrix_axis` (`rows` or
`cols`, beside `matrix_extent`, which gained `count(axis)`) each operand
divides along. A head is a block of columns in `q`, `k`, `v`, and `out`, and
a block of rows in `scores`, so the batch stride is the head width D for the
column-split operands, the leading dimension stays the stored stride (3C for
`qkv`), and nothing is permuted or copied. The scratch `scores` grew from T x
T to HT x T, every head's square stacked, and `causal_mask` takes a stack by
masking each row against its index within its own square. The device
`block_activation_buffers` constructor takes the head count to size it. One
point rests on a reading of the cuBLAS documentation, which says the
instances of `C` must not overlap: the second GEMM writes interleaved column
blocks of `out`, whose elements are disjoint though their address ranges
interleave. The hand-computed batched test, the two-head napkin test, and the
twelve-block oracle gate all pass at the old tolerances. Next: the KV cache,
bf16, and tokens per second against llama.cpp.

Status (2026-09-21, KV cache, slice 1: CPU attend over cached tokens):
Steven's rulings for the cache. The layout is [T, 3C] per block in the first
cut, the `c_attn` output as it stands, so the projection writes a new token's
row straight into the cache and the unused query columns ride along; dropping
to [T, 2C] trades a copy for a third of the memory and waits for a model with
a longer context. The engine caches a single token list, matched by common
prefix, so an unrelated list clears it, which timing tests can use on
purpose. The CPU engine goes first. Slice 1 is the op: `attend` and
`attend_head` take a `qkv` (or `k` and `v`) with more rows than `out`, the
extra rows at the top being M cached tokens. Row `i` of the N new queries is
token M + `i` and reads from tokens 0 through M + `i`, which moves the causal
mask right by M, and the scratch row holds M + N scores. With nothing cached
the op is what it was. The vocabulary is cached and new, with total for their
sum; prefix is kept for the matching of token lists, where it is literal. A
napkin test and the twelve-block oracle test check that attending the last
tokens over the cached rest gives the full pass's rows for those tokens bit
for bit. Next: the cache and prefix matching in the CPU engine, then the
device twin (offset mask, rectangular batched attend), then the device
argmax.

Status (2026-09-21, lens and view): Steven's naming ruling, applied across
the library and the tests. A view is read-only and a lens reads and writes,
the pairing the ECS rows and the websocket frames already use, and the one
`std::string_view` and `std::span` draw between them; "span" was passed over
for a matrix because a strided rectangle is not a contiguous run. What was
`matrix_view<T>` is `matrix_lens<T>`, and `matrix_view<T>` is now an alias
for `matrix_lens<const T>`, so nothing spells `matrix_view<const float>` any
more. The same split covers `matrix_lens_base`, `cuda_matrix_lens` and
`cuda_matrix_view`, `kernel_matrix_lens` and `kernel_matrix_view`, and the
aliases `float_matrix_lens` and `float_matrix_view` (formerly
`float_matrix_view` and `const_float_matrix_view`). `cuda_matrix` has
`as_lens()` beside a read-only `as_view()`, `DeviceMatrixLike` asks for
`as_lens()`, the activation buffers hand out `lenses()`, and the device
`const_view_t` went away as a plain synonym of `cuda_matrix_view`. In the
same pass `subview` became `slice`, spelled `m[from, to]` or `m[from, size]`
by preference, with the one-argument form named only since `m[from]` is an
element; `matrix_extent` took `operator[](matrix_axis)` over `count`; and the
host `const_view_t` became `matrix_view_t`, since every view is const. The
header is still "matrix_view.h". Status paragraphs above this one keep the
names of their day.

Status (2026-09-21, KV cache, slice 2: the CPU engine): `gpt2_engine` gained
a nested `kv_cache`, the cached token IDs plus one packed [M, 3C] store per
block that grows by rows, which keeps every cached row in place because the
row width is fixed. `qkv` left `block_activations`: it is no longer scratch,
so `apply_block` takes it as its own parameter, M + N rows with the cached
tokens on top, projects `ln_1_out` into the bottom N rows, and attends over
all of them. `forward(out, new_ids, acts, cache)` runs only the new tokens,
embeds their positions starting at M, and appends them to the cache on
return; an empty cache is the old full pass, so the oracle tests pass a fresh
one. `next_token(out, ids)` keeps its signature and stays `const` over a
`mutable` cache, one caller at a time: it keeps the cached tokens that `ids`
starts with, short of the last one, whose trunk row the logits need, and runs
the rest, so an extended list costs its added tokens and an unrelated list
replaces the cache. `block_activation_buffers` takes the cached count to size
`scores` at M + N. The tests: the bisect prompt as nine tokens then five
matches the single pass bit for bit, in the trunk rows and in every block's
cache; the greedy test now decodes through the cache, then checks that an
unrelated list followed by the prompt, and the prompt asked twice, both get
the manifest's first pick. The twenty-step greedy test case fell from about
2.3 s to 0.6 s, model load included. The device engine is unchanged and its
surface now differs from the CPU one until slice 3. Steven's review:
`embed_positions` on both sides took a defaulted `first_position`, since
slicing the table is the op's job and not its caller's; the cache reserves
the whole context on first use, so no later pass reallocates; the block loop
walks the cache through `std::views::enumerate`; and the packed `matrix_view`
constructor now takes the start of a span at least as large as its extent,
with the old exact-size check behind the `exact_size` tag, which removed the
engine's hand-trimmed span. Still allocated per `next_token` call: the
activation buffers, the trunk, and the logits row. Next: the device twin
(offset mask, rectangular batched attend, device cache), then the device
argmax.

Status (2026-09-22, KV cache, slice 3: the device twin): the device ops take
the CPU's cached form. `causal_mask` takes a `cached_count`, defaulted to
zero, since a stack of N x T blocks cannot tell N from its shape once it is
not square, and row `i` keeps columns 0 through M + `i`. `attend` slices the
new tokens' queries off the bottom of `qkv` and scores them against every
key, so the batched GEMMs run over N x D and T x D pieces and `scores` is
HN x T. The device engine mirrors the CPU one: `qkv` left
`block_activations` and is an `apply_block` parameter, `forward(out,
new_ids, acts, cache)` takes the new IDs as a host span and uploads them
itself, since the cache keeps its IDs on the host for prefix matching, and
`next_token` keeps its signature over a `mutable` cache. The device
`kv_cache` differs in one way. `cuda_matrix` neither grows nor shrinks, so
each block's store is allocated at the full context on the first pass and
sliced to the rows in use, never reallocated or trimmed. The napkin and
oracle attention tests gained the cached forms and match the full pass bit
for bit, as on the CPU. The engine test's cache-versus-full comparison does
not. The five rows run after nine cached differ from the full pass by up to
6.1e-5 in the trunk and 8.6e-6 in the cached rows, since a GEMM over a
different row count may pick a different kernel, so that check uses an
absolute and relative tolerance of 1e-5, which it passes. Next: the device
argmax.

Status (2026-09-24, KV cache, slice 4: the device argmax): the greedy pick
runs on the device, so `next_token` downloads four bytes rather than the 200
KB logits row. `cuda_reduce` gained `element<T>`, a value with its index,
ordered so that a lower index ranks above an equal value, so `block_max`
over elements, through `cuda::maximum`, returns the first largest, which is
the rule of `std::max_element` and so of the CPU `pick_greedy`. The identity
is the variable template `lowest<T>`, specialized for an element, so a max
over a new type adds a specialization rather than a branch. The butterfly
moves an `element<T>` whole through libcu++'s
`cuda::device::warp_shuffle_xor`, which shuffles any trivially copyable
type. The device `pick_greedy(out, logits)` in "llm_ops.cuh" picks one ID
per row of `logits` into a `cuda_buffer<token_id>`, one block per row in the
row-kernel shape of `softmax` and `layer_norm`, so a [1, V] row is one block
of 256 threads each walking about 200 columns. It is a plain function over
`float` logits, as the CPU op is over a float span, so an owning matrix
converts at the call, and the kernel under it is the usual template. Tests:
the reduction on its own across one warp and eight (largest first, last,
mid-block, tied, and falling throughout); five napkin rows wider than a
block; every row of the five prompts' dumped logits against the host pick;
and the twenty-ID greedy gate, now through the device pick. With this the
KV-cache step is complete. Next: bf16, then tokens per second against
llama.cpp.

Status (2026-09-24, bf16, slice 1: the type): `corvid::cuda::bfloat16_t` in
"bfloat16.cuh" wraps the toolkit's `__nv_bfloat16` behind explicit
conversions, since the standard's `std::bfloat16_t` ships only in libstdc++.
Construction from a `float` rounds to nearest even, conversion back is
explicit, the comparisons are those of the float values, and `from_bits` and
`bits` reach the pattern for the constants a `float` would round. It is
storage, not arithmetic. A kernel converts to `float`, computes, and
converts the result back, which keeps the accumulate-in-fp32 rule visible at
the call site. The constraint `DeviceFloating` sits beside it, `Floating` or
`bfloat16_t`, so the device ops can admit the type without CUDA reaching
into "meta". The header specializes libcu++'s `numeric_limits` for it from
bit patterns, so `cuda_reduce::lowest<T>`, the softmax peak, and the mask's
infinity find a bf16 limit where they find a float's, with no branch and no
second special case. Tests: the bit patterns of exact values and the
rounding of ties (1 + 2^-8 goes to the even 0x3F80, 1 + 3 * 2^-8 to 0x3F82),
the comparisons, the limits (max is (2 - 2^-7) * 2^127 and lowest its
negation, both finite), and a device round trip over 64 threads through
`block_max`, which exercises the identity and libcu++'s shuffle of a
two-byte type. Next: the cuBLAS traits for `cublasGemmEx`, then the ops and
the engine over the new element type, then the gate and the measurement.

Status (2026-09-24, bf16, slice 2: the cuBLAS traits): `GemmElement` is now
a traits class, `gemm_traits<T>`, giving the cuBLAS data type an operand is
declared as, the compute type the product accumulates in, and `scalar_t`,
the type of `alpha` and `beta`. `float` and `double` accumulate in their own
type; `bfloat16_t` is declared `CUDA_R_16BF`, accumulates in
`CUBLAS_COMPUTE_32F`, and takes float scalars. The S and D overload pairs
behind `multiply` and `multiply_batched` collapsed into one `cublasGemmEx`
and one `cublasGemmStridedBatchedEx` call each, with `CUBLAS_GEMM_DEFAULT`
and the handle's default math mode, so a float product still runs true fp32
rather than TF32. `gemm_options` and the ops' `beta` take
`gemm_scalar_t<T>`, which is where a bf16 GEMM's scalars stop being bf16.
The fp32 gates are unchanged: the op tests and the twelve-block,
five-prompt, and greedy engine gates pass at their old tolerances through
the new routine. Tests: a bf16 product whose float partial sums are exact,
checked as the float product narrowed once at the store (three of its four
values need more than eight bits, so the rounding is visible), the same
scaled and accumulated, and the batched product over both axes. Next: the
ops and the engine over `bfloat16_t`.

Status (2026-09-24, bf16, slice 3: the ops and the engine): every device op
admits `bfloat16_t` beside `float` and `double`. A kernel computes in
`compute_t<T>` ("bfloat16.cuh"), `float` for `bfloat16_t` and `T` itself
otherwise, widening each element it reads with `widen` and narrowing each
result once, at its store, with `T{}`, so a bf16 op is the float op rounded
at the end. Softmax computes each exponential twice rather than storing the
unnormalized weights, so that its weights are rounded once too. The residual
adds, the position embedding, and the mask follow the same shape, with the
mask's negative infinity narrowed from the compute type. The cuBLAS wrapper
gained `GemmOutput`, the operand and result pair `multiply` accepts, which
is a product's own type or, for `bfloat16_t` operands, `float`, the row of
cuBLAS's table that stores a bf16 product accumulated in fp32 without
narrowing it. The ops' `gemm` and `compute_logits` take their operands as
`DeviceMatrixViewable` (anything that converts to a `cuda_matrix_view`) so
the operand type is deduced apart from the output's, and `convert` is the
elementwise conversion between element types. `gpt2_engine<T>` holds the
model as `T`, uploading each parameter as `float` and converting it on the
device (a `float` engine uploads directly), and computes its logits as
`float` whatever `T` is, through the mixed GEMM, so a greedy pick rests on
the final projection's full precision rather than on eight bits. Measured:
the bf16 logits match the fp32 oracle within a relative error of 3.3e-2 on
the worst prompt (the gate is 4e-2, absolute and relative), and bf16 greedy
decoding follows the manifest for 7 of its 20 IDs before a close pick flips.
The error is the residual stream's, held in eight significant bits through
twelve blocks; keeping the residual in `float` while the projections run in
bf16 is the usual remedy and is left as a decision. Tests: every op's bf16
result checked as its float result narrowed at the store (layer norm, GELU,
softmax, add, subtract, the mask, the embeddings), the attention napkin over
bf16 within 1e-2, the mixed logits stored as exact floats, `convert` both
ways, the concept truth tables, and the two bf16 engine gates. Next: tokens
per second, fp32 against bf16 against llama.cpp.

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
