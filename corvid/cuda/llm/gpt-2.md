# GPT-2 forward pass reference

The steps of one forward pass through GPT-2 (the 124M-parameter "gpt2"
checkpoint), with the shape of every input and output and the name of every
tensor read from the weights file. It is a lookup table for reading and
writing the ops in "gpt2_forward.h", not a tutorial; the roadmap has the
plan and the status, and the doc blocks in the header have the formulas.

## Notation and constants

| symbol | value | meaning |
|---|---|---|
| T | prompt length | tokens in the stream; 14 for the bisect prompt |
| V | 50257 | vocabulary size |
| C | 768 | features per token (`n_embd`) |
| H | 12 | attention heads per block (`n_head`) |
| D | 64 | features per head; C / H = 768 / 12 |
| F | 3072 | hidden features of the MLP; 4 x C |
| L | 12 | blocks (`n_layer`) |
| n_ctx | 1024 | context limit; the most tokens one pass can take |
| eps | 1e-5 | added to the variance inside every layer norm |

Activations are matrices with one row per token and one column per feature,
so a stream is [T, C]. Weights are stored the way the original code's Conv1D
layer keeps them: one row per input feature and one column per output
feature, so `out = in * weight + bias`. The word "residual" below means the
stream itself, the [T, C] matrix that every block reads and adds to.

Acronyms, once: `wte` word token embeddings, `wpe` word position embeddings,
`ln` layer norm (`ln_f` the final one), `attn` attention, `c_attn` and
`c_fc` and `c_proj` the Conv1D attention, fully connected, and projection
weights, MLP multi-layer perceptron, GELU Gaussian Error Linear Unit.

## Weights file

Every tensor in "model.safetensors" is fp32. `N` runs from 0 to L - 1.

| tensor | shape | used by |
|---|---|---|
| `wte.weight` | [V, C] | embedding lookup, and again as the output head |
| `wpe.weight` | [n_ctx, C] | embedding lookup |
| `h.N.ln_1.weight`, `h.N.ln_1.bias` | [C] | layer norm before attention |
| `h.N.attn.c_attn.weight` | [C, 3C] | query, key, value projection |
| `h.N.attn.c_attn.bias` | [3C] | |
| `h.N.attn.c_proj.weight` | [C, C] | attention output projection |
| `h.N.attn.c_proj.bias` | [C] | |
| `h.N.attn.bias` | [1, 1, n_ctx, n_ctx] | the saved causal mask buffer; not a parameter; ignored |
| `h.N.ln_2.weight`, `h.N.ln_2.bias` | [C] | layer norm before the MLP |
| `h.N.mlp.c_fc.weight` | [C, F] | MLP up projection |
| `h.N.mlp.c_fc.bias` | [F] | |
| `h.N.mlp.c_proj.weight` | [F, C] | MLP down projection |
| `h.N.mlp.c_proj.bias` | [C] | |
| `ln_f.weight`, `ln_f.bias` | [C] | final layer norm |

There is no separate output head weight. The head reuses `wte.weight`
transposed, with no bias.

## Embedding

| step | reads | looks up | produces | Corvid |
|---|---|---|---|---|
| tokenize | text | vocab, merges | ids [T] | `gpt2_tokenizer.h` |
| embed | ids [T] | `wte.weight` row per ID, `wpe.weight` row per position | residual [T, C] | `embed` |

The embedding is `wte[id[t]] + wpe[t]` for each position `t`. Positions
past n_ctx have no row, so a longer prompt is refused, not wrapped. After
this step nothing reads the IDs again.

## One block, repeated L times

The block has two sublayers, attention and MLP. Each one reads the residual
through a layer norm, computes a correction of shape [T, C], and adds it to
the residual. The layer norm outputs are consumed by their sublayer and
discarded; only the residual carries forward. `block` runs both sublayers
in place on the residual, taking its parameters as `block_params` and its
working storage as `block_scratch`.

### Attention sublayer

| step | reads | looks up | produces | Corvid |
|---|---|---|---|---|
| ln_1 | residual [T, C] | `h.N.ln_1.weight`, `h.N.ln_1.bias` | ln_1/out [T, C] | `layer_norm` |
| c_attn | ln_1/out [T, C] | `h.N.attn.c_attn.weight` [C, 3C], `.bias` [3C] | qkv [T, 3C] | `linear` |
| split | qkv [T, 3C] | | q, k, v each [T, C]: columns 0..C-1, C..2C-1, 2C..3C-1 | `subview` |
| heads | q, k, v [T, C] | | per head h: q_h, k_h, v_h each [T, D], columns h*D..h*D+D-1 | `subview` |
| scores | q_h [T, D], k_h [T, D] | | s [T, T], s[i][j] = (q_h[i] . k_h[j]) / sqrt(D) = (q_h[i] . k_h[j]) / 8 | `attention_head`, via `dot` |
| mask | s [T, T] | | s with j > i excluded | `attention_head`, as the loop bound |
| softmax | s row i over j <= i | | w [T, T], each row sums to 1, zero where j > i | `softmax_row` |
| weighted sum | w [T, T], v_h [T, D] | | head_out_h [T, D], row i = sum over j <= i of w[i][j] * v_h[j] | `attention_head`, via `add_scaled` |
| concat | H of head_out_h [T, D] | | heads_out [T, C], head h in columns h*D..h*D+D-1 | `attention` writes each head's slice in place |
| c_proj | heads_out [T, C] | `h.N.attn.c_proj.weight` [C, C], `.bias` [C] | attn/out [T, C] | `linear` |
| residual add | residual [T, C], attn/out [T, C] | | residual [T, C] | `add` |

The scores, mask, softmax, and weighted sum run once per head, on that
head's D-wide slices, independently of the other heads. Those slices are
columns of the `c_attn` output, so each of a head's D query features is a
weighted sum over all C input features. Heads partition the projection's
output, never its input, and twelve heads of width D cost the same
parameters and the same arithmetic as one head of width C. The softmax
subtracts the row's maximum before exponentiating so a large score cannot
overflow `exp`. The mask is the causal rule: token i attends to tokens
j <= i only.

### MLP sublayer

| step | reads | looks up | produces | Corvid |
|---|---|---|---|---|
| ln_2 | residual [T, C] | `h.N.ln_2.weight`, `h.N.ln_2.bias` | ln_2/out [T, C] | `layer_norm` |
| c_fc | ln_2/out [T, C] | `h.N.mlp.c_fc.weight` [C, F], `.bias` [F] | hidden [T, F] | `linear` |
| gelu | hidden [T, F] | | hidden [T, F], in place | `gelu_new` |
| c_proj | hidden [T, F] | `h.N.mlp.c_proj.weight` [F, C], `.bias` [C] | mlp/out [T, C] | `linear` |
| residual add | residual [T, C], mlp/out [T, C] | | residual [T, C] | `add` |

The GELU is the tanh form (`gelu_new`), not the erf form; the weights were
trained against the tanh curve.

## Head

| step | reads | looks up | produces | Corvid |
|---|---|---|---|---|
| ln_f | residual [T, C] | `ln_f.weight`, `ln_f.bias` | ln_f/out [T, C] | `layer_norm` |
| logits | ln_f/out [T, C] | `wte.weight` [V, C], transposed, no bias | logits [T, V] | not yet |
| greedy | logits row T - 1 | | the ID with the largest logit | not yet |

Row t of the logits scores every vocabulary entry as the token after
position t, so all T rows are predictions and only the last one is used to
generate. The next ID is appended to the stream and the whole pass runs
again on T + 1 tokens.

## Oracle dumps

"activations.safetensors" holds the bisect prompt's activations, each
[T, C] unless noted, named by where in the pass they were captured:

| dump | is |
|---|---|
| `embed/out` | the residual entering block 0 |
| `block_N/ln_1/in` | the residual entering block N (equals the previous block's exit) |
| `block_N/ln_1/out` | after the first layer norm |
| `block_N/attn/out` | after `c_proj` of attention, before the residual add |
| `block_N/ln_2/in` | the residual after the attention add |
| `block_N/ln_2/out` | after the second layer norm |
| `block_N/mlp/out` | after `c_proj` of the MLP, before the residual add |
| `ln_f/in` | the residual leaving block L - 1 |
| `ln_f/out` | after the final layer norm |
| `logits` | [T, V] |

There is no dump between `c_attn` and `c_proj`, nor between `c_fc` and
`c_proj`, so the projections are gated through their whole sublayer: the
MLP path (`ln_2/out` to `mlp/out`) and, once written, the attention path
(`ln_1/out` to `attn/out`). "logits.safetensors" holds `prompt_N/input_ids`
[T] and `prompt_N/logits` [T, V] for all five prompts.
