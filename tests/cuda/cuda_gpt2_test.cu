// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2026 Steven Sudit
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <format>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_template_test_macros.hpp>

#include "corvid/containers/utils/interval.h"
#include "corvid/cuda/bfloat16.cuh"
#include "corvid/cuda/llm/gpt2_engine.cuh"
#include "corvid/infra/stopwatch.h"
#include "catch2_main.h"
#include "gpt2_oracle.h"

// The whole test sits in a named namespace. A `using namespace corvid;` at
// global scope would make `cuda` (libcu++'s namespace against corvid::cuda)
// and `log` (corvid::infra::log against the C math function) ambiguous in the
// host code nvcc appends after the translation unit, and clang sees the same
// ambiguity wherever the test spells `cuda::`, which is why it is spelled
// `corvid::cuda::` throughout.
namespace corvid_tests {

using namespace corvid;
using namespace corvid::tests::gpt2;
using corvid::cuda::bfloat16_t;
using corvid::cuda::cublas_handle;
using corvid::cuda::cuda_buffer;
using corvid::cuda::cuda_matrix;
using corvid::cuda::cuda_matrix_view;
using corvid::cuda::device_value_t;
using corvid::cuda::DeviceMatrixViewable;
using corvid::cuda::llm::gpt2_engine;
using corvid::llm::gpt2_end_of_text;
using corvid::llm::gpt2_model;
using corvid::llm::token_id;
using matrix_types::col_ndx;
using matrix_types::matrix_extent;
using matrix_types::row_ndx;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

#pragma region Helpers

// Download `in` into a host vector of `float`, in row-major order, converted
// from its element type.
template<DeviceMatrixViewable In>
std::vector<float> download(const In& in) {
  using T = device_value_t<In>;
  const cuda_matrix_view<T> device = in;
  std::vector<T> storage(device.row_extent() * device.col_extent());
  REQUIRE(device.store(matrix_lens<T>(storage, device.extent())));
  return {storage.begin(), storage.end()};
}

// The rate of `count` tokens in `took`, in tokens per second.
double tokens_per_second(size_t count, fp_milliseconds took) {
  return static_cast<double>(count) /
         std::chrono::duration<double>(took).count();
}

// Download `device` and check that it is close to `expected`.
void check_device_close(cuda_matrix_view<float> device,
    float_matrix_view expected, float atol, float rtol) {
  const auto storage = download(device);
  check_close(float_matrix_view(storage, device.extent()), expected, atol,
      rtol);
}

// The tolerance of the bf16 logits against the oracle's fp32 ones, both
// absolute and relative, set above the largest relative error measured over
// the five prompts, 2.7e-2 on prompt 3 (2026-09-24), with the residual
// stream, queries, keys, and scores in `float` and the products' operands
// against the parameters in eight significant bits. The reference is GPT-2
// run wholly in bf16 by transformers, which loses 3.7e-2 on the same prompt
// ("gpt2_bf16_reference.py").
constexpr auto bf16_logits_tolerance = 3e-2F;

// The ratios of a bf16 block's largest error to its largest expected value,
// set above the largest measured over the twelve blocks (2026-09-24): the
// exit residual 1.3e-2 (block 11), the attention output 7.9e-2 (block 3,
// whose output is small against the bf16 rounding of the layer norm output
// that its projections read), the residual after it 1.2e-2 (block 11), and
// the MLP output 9.4e-3 (block 5).
constexpr auto bf16_exit_ratio = 2e-2F;
constexpr auto bf16_attn_out_ratio = 1e-1F;
constexpr auto bf16_ln_2_in_ratio = 2e-2F;
constexpr auto bf16_mlp_out_ratio = 2e-2F;

// The tolerance of an engine's logits against the fp32 oracle's, by element
// type: the fp32 gate, except for bf16's own.
template<typename T>
constexpr auto logits_tolerance = 1e-4F;
template<>
constexpr auto logits_tolerance<bfloat16_t> = bf16_logits_tolerance;

#pragma endregion

} // namespace

#pragma region Tests

TEST_CASE("Device block matches the oracle", "[Gpt2Test][oracle][cuda]") {
  auto oracle = oracle_dumps::load();
  const auto model = gpt2_model<float>::load(std::move(oracle.weights));
  const gpt2_engine<float> engine(model);

  // Every block, fed its own dumped residual, as the CPU test does. The
  // residual that leaves it is checked against the next block's `ln_1/in`
  // or, for the last block, `ln_f/in`, and the five activations the oracle
  // also dumped are checked on the way, at the CPU gate's tolerances. A
  // plain loop rather than a section per block, since a section re-enters
  // the test case and uploads the model again.
  for (const auto n : iota(n_layer)) {
    {
      INFO("block_" << n);
      const auto dump = std::format("block_{}", n);
      const auto in_view =
          matrix_of(oracle.activations, dump + "/ln_1/in", n_embd);
      const auto exit =
          (n + 1 < n_layer)
              ? std::format("block_{}/ln_1/in", n + 1)
              : std::string{"ln_f/in"};
      const auto expected = matrix_of(oracle.activations, exit, n_embd);
      const auto token_count = in_view.row_extent();
      REQUIRE(token_count == 14);

      const cuda_matrix<float> in(in_view);
      cuda_matrix<float> out(in_view.extent());
      gpt2_engine<float>::block_activation_buffers owned(token_count, n_embd,
          n_hidden, n_head);
      const auto acts = owned.lenses();
      cuda_matrix<float> keys({.row_count = token_count, .col_count = n_embd});
      cuda_matrix<float> values(keys.extent());
      REQUIRE(engine.apply_block(out, in, n, acts, keys, values));

      check_device_close(out, expected, 1e-4F, 1e-4F);
      struct dumped {
        const char* name;
        cuda_matrix_view<float> actual;
        float tolerance;
      };
      for (const auto& [name, actual, tolerance] :
          {dumped{"ln_1/out", acts.ln_1_out, 1e-5F},
              dumped{"attn/out", acts.attn_out, 1e-4F},
              dumped{"ln_2/in", acts.ln_2_in, 1e-4F},
              dumped{"ln_2/out", acts.ln_2_out, 1e-4F},
              dumped{"mlp/out", acts.mlp_out, 1e-4F}})
      {
        INFO(name);
        check_device_close(actual,
            matrix_of(oracle.activations, dump + "/" + name, n_embd),
            tolerance, tolerance);
      }

      // In place, with `ln_2_in` aliased to the residual too, is the same
      // arithmetic in the same order, so it matches bit for bit.
      cuda_matrix<float> residual(in_view);
      auto in_place = acts;
      in_place.ln_2_in = residual;
      REQUIRE(
          engine.apply_block(residual, residual, n, in_place, keys, values));
      CHECK(download(residual) == download(out));
    }
  }
}

TEST_CASE("Device forward pass matches the oracle",
    "[Gpt2Test][oracle][cuda]") {
  auto oracle = oracle_dumps::load();

  // The bisect prompt from its IDs through every block to `ln_f/out`, with
  // the whole model uploaded once, at the block's gate.
  const auto id_storage =
      ids_of(oracle.logits, std::format("prompt_{}/input_ids", bisect_prompt));
  const auto token_count = id_storage.size();
  REQUIRE(token_count == 14);
  const auto model = gpt2_model<float>::load(std::move(oracle.weights));
  const gpt2_engine<float> engine(model);
  const auto expected = matrix_of(oracle.activations, "ln_f/out", n_embd);

  cuda_matrix<float> out(expected.extent());
  cuda_matrix<float> residual(expected.extent());
  gpt2_engine<float>::block_activation_buffers owned(token_count, n_embd,
      n_hidden, n_head);
  gpt2_engine<float>::kv_cache cache;
  REQUIRE(engine.forward(out, residual, id_storage, owned.lenses(), cache));

  check_device_close(out, expected, 1e-4F, 1e-4F);
  CHECK(cache.ids == id_storage);
}

TEST_CASE("Device double engine matches the oracle",
    "[Gpt2Test][oracle][cuda]") {
  auto oracle = oracle_dumps::load();
  const cublas_handle blas;

  // The bisect prompt through the model held as `double`, which converts
  // every parameter on upload and computes in double throughout, against
  // the fp32 oracle's final layer norm output and logits at the fp32 gate's
  // tolerance, and its greedy picks against the host's over those logits.
  const auto id_storage =
      ids_of(oracle.logits, std::format("prompt_{}/input_ids", bisect_prompt));
  const auto token_count = id_storage.size();
  const auto model = gpt2_model<float>::load(std::move(oracle.weights));
  const gpt2_engine<double> engine(model);
  const auto expected = matrix_of(oracle.activations, "ln_f/out", n_embd);
  const auto expected_logits = matrix_of(oracle.logits,
      std::format("prompt_{}/logits", bisect_prompt), n_vocab);

  cuda_matrix<double> out(expected.extent());
  cuda_matrix<double> residual(expected.extent());
  gpt2_engine<double>::block_activation_buffers owned(token_count, n_embd,
      n_hidden, n_head);
  gpt2_engine<double>::kv_cache cache;
  REQUIRE(engine.forward(out, residual, id_storage, owned.lenses(), cache));

  const auto out_values = download(out);
  check_close(float_matrix_view(out_values, out.extent()), expected, 1e-4F,
      1e-4F);

  const cuda_matrix<float> wte_f32(model.wte);
  cuda_matrix<double> wte(wte_f32.extent());
  REQUIRE(corvid::cuda::linalg::convert(wte, wte_f32));
  cuda_matrix<double> logits(expected_logits.extent());
  REQUIRE(corvid::cuda::llm::compute_logits(blas, logits, out, wte));
  const auto logit_values = download(logits);
  check_close(float_matrix_view(logit_values, logits.extent()),
      expected_logits, 1e-4F, 1e-4F);

  cuda_buffer<token_id> picks(token_count);
  REQUIRE(corvid::cuda::llm::pick_greedy(picks, logits));
  std::vector<token_id> picked(token_count);
  REQUIRE(picks.store(picked));
  for (const auto [pick, logits_row] :
      std::views::zip(picked, expected_logits.rows()))
    CHECK(pick == corvid::llm::pick_greedy(logits_row));
}

TEST_CASE("Device forward pass over a cache matches a full pass",
    "[Gpt2Test][oracle][cuda]") {
  auto oracle = oracle_dumps::load();

  // The bisect prompt in one pass, then again as nine tokens followed by the
  // other five, as the CPU test does. There the five rows match bit for bit;
  // here a GEMM over a different row count may pick a different kernel, so
  // the rows and the cached ones match within a tolerance.
  const auto ids =
      ids_of(oracle.logits, std::format("prompt_{}/input_ids", bisect_prompt));
  const auto total_count = ids.size();
  REQUIRE(total_count == 14);
  constexpr auto cached_count = 9UZ;
  const auto new_count = total_count - cached_count;
  const auto model = gpt2_model<float>::load(std::move(oracle.weights));
  const gpt2_engine<float> engine(model);

  const auto run =
      [&](cuda_matrix<float>& out, std::span<const token_id> new_ids,
          gpt2_engine<float>::kv_cache& cache) {
        gpt2_engine<float>::block_activation_buffers owned(new_ids.size(),
            n_embd, n_hidden, n_head, cache.ids.size());
        cuda_matrix<float> residual(out.extent());
        REQUIRE(engine.forward(out, residual, new_ids, owned.lenses(), cache));
      };

  cuda_matrix<float> full({.row_count = total_count, .col_count = n_embd});
  gpt2_engine<float>::kv_cache full_cache;
  run(full, ids, full_cache);

  cuda_matrix<float> first({.row_count = cached_count, .col_count = n_embd});
  cuda_matrix<float> rest({.row_count = new_count, .col_count = n_embd});
  gpt2_engine<float>::kv_cache cache;
  run(first, std::span{ids}.first(cached_count), cache);
  CHECK(cache.ids.size() == cached_count);
  run(rest, std::span{ids}.subspan(cached_count), cache);
  CHECK(cache.ids == ids);

  const auto rest_of_full =
      download(full[{row_ndx{cached_count}, col_ndx{0}}, rest.extent()]);
  check_device_close(rest, float_matrix_view(rest_of_full, rest.extent()),
      1e-5F, 1e-5F);
  REQUIRE(cache.blocks.size() == full_cache.blocks.size());
  const matrix_extent in_use{.row_count = total_count, .col_count = n_embd};
  for (const auto [block, full_block] :
      std::views::zip(cache.blocks, full_cache.blocks))
  {
    for (const auto [store, full_store] :
        {std::pair{&block.keys, &full_block.keys},
            std::pair{&block.values, &full_block.values}})
    {
      const auto full_rows =
          download((*full_store)[{row_ndx{0}, col_ndx{0}}, in_use]);
      check_device_close((*store)[{row_ndx{0}, col_ndx{0}}, in_use],
          float_matrix_view(full_rows, in_use), 1e-5F, 1e-5F);
    }
  }
}

TEST_CASE("Device model matches the oracle on every prompt",
    "[Gpt2Test][oracle][cuda]") {
  auto oracle = oracle_dumps::load();
  const cublas_handle blas;

  // The whole model, IDs to logits, on each of the manifest's prompts, with
  // the head a GEMM against the transposed embedding, which the test uploads
  // for itself since the engine scores only the last token.
  const auto model = gpt2_model<float>::load(std::move(oracle.weights));
  const gpt2_engine<float> engine(model);
  const cuda_matrix<float> wte(model.wte);
  for (auto n = 0UZ; n < 5; ++n) {
    DYNAMIC_SECTION("prompt_" << n) {
      const auto prefix = std::format("prompt_{}", n);
      const auto id_storage = ids_of(oracle.logits, prefix + "/input_ids");
      const auto expected =
          matrix_of(oracle.logits, prefix + "/logits", n_vocab);
      const auto token_count = id_storage.size();
      REQUIRE(expected.row_extent() == token_count);

      cuda_matrix<float> trunk(
          {.row_count = token_count, .col_count = n_embd});
      cuda_matrix<float> residual(trunk.extent());
      gpt2_engine<float>::block_activation_buffers owned(token_count, n_embd,
          n_hidden, n_head);
      gpt2_engine<float>::kv_cache cache;
      REQUIRE(
          engine.forward(trunk, residual, id_storage, owned.lenses(), cache));

      cuda_matrix<float> logits(expected.extent());
      REQUIRE(corvid::cuda::llm::compute_logits(blas, logits, trunk, wte));

      check_device_close(logits, expected, 1e-4F, 1e-4F);
    }
  }
}

TEST_CASE("Device greedy decoding reproduces the manifest",
    "[Gpt2Test][oracle][cuda]") {
  auto oracle = oracle_dumps::load();

  // The CPU test's gate, through the device engine: the twenty IDs the
  // manifest records and their text.
  const auto expected = read_greedy_continuation();
  REQUIRE(expected.ids.size() == 20);

  const auto model = gpt2_model<float>::load(std::move(oracle.weights));
  const gpt2_engine<float> engine(model);
  auto ids = ids_of(oracle.logits,
      std::format("prompt_{}/input_ids", expected.prompt));
  const auto prompt_count = ids.size();
  REQUIRE(engine.generate(ids, expected.ids.size()));
  const auto appended = std::span{ids}.subspan(prompt_count);
  const std::vector<token_id> generated(appended.begin(), appended.end());
  CHECK(generated == expected.ids);

  // And as text, through the tokenizer.
  CHECK(decode_ids(generated) == expected.text);

  // Nothing follows no tokens.
  token_id next{};
  CHECK(!engine.next_token(next, {}));

  // An unrelated list replaces the cached one, and the prompt, run again from
  // nothing, still gets the first pick. So does the same prompt asked twice,
  // which has every token but the last cached.
  const auto prompt = std::span{ids}.first(prompt_count);
  const std::vector<token_id> unrelated{gpt2_end_of_text};
  REQUIRE(engine.next_token(next, unrelated));
  REQUIRE(engine.next_token(next, prompt));
  CHECK(next == expected.ids.front());
  REQUIRE(engine.next_token(next, prompt));
  CHECK(next == expected.ids.front());
}

TEST_CASE("Device bf16 model matches the oracle within its tolerance",
    "[Gpt2Test][oracle][cuda]") {
  auto oracle = oracle_dumps::load();
  const cublas_handle blas;

  // The five-prompt gate again, with the model held as `bfloat16_t`. Every
  // product against the parameters reads operands rounded to eight
  // significant bits, so the logits land within a looser tolerance than
  // fp32's, measured and stated here. The trunk leaves the engine as
  // `bfloat16_t`, ready for the product against the embedding.
  const auto model = gpt2_model<float>::load(std::move(oracle.weights));
  const gpt2_engine<bfloat16_t> engine(model);
  const cuda_matrix<float> wte_f32(model.wte);
  cuda_matrix<bfloat16_t> wte(wte_f32.extent());
  REQUIRE(corvid::cuda::linalg::convert(wte, wte_f32));
  for (const auto n : iota(5)) {
    DYNAMIC_SECTION("prompt_" << n) {
      const auto prefix = std::format("prompt_{}", n);
      const auto id_storage = ids_of(oracle.logits, prefix + "/input_ids");
      const auto expected =
          matrix_of(oracle.logits, prefix + "/logits", n_vocab);
      const auto token_count = id_storage.size();
      REQUIRE(expected.row_extent() == token_count);

      cuda_matrix<bfloat16_t> trunk(
          {.row_count = token_count, .col_count = n_embd});
      cuda_matrix<float> residual(trunk.extent());
      gpt2_engine<bfloat16_t>::block_activation_buffers owned(token_count,
          n_embd, n_hidden, n_head);
      gpt2_engine<bfloat16_t>::kv_cache cache;
      REQUIRE(
          engine.forward(trunk, residual, id_storage, owned.lenses(), cache));

      cuda_matrix<float> logits(expected.extent());
      REQUIRE(corvid::cuda::llm::compute_logits(blas, logits, trunk, wte));

      check_device_close(logits, expected, bf16_logits_tolerance,
          bf16_logits_tolerance);
    }
  }
}

TEST_CASE("Device bf16 block matches the oracle within its tolerance",
    "[Gpt2Test][oracle][cuda]") {
  auto oracle = oracle_dumps::load();
  const auto model = gpt2_model<float>::load(std::move(oracle.weights));
  const gpt2_engine<bfloat16_t> engine(model);

  // Every block fed its own dumped residual, as the fp32 test does, with the
  // block's products over bf16 operands and its residual in float. The exit
  // residual is checked against the next block's entry, and the three float
  // activations on the way against their dumps, each as a ratio of its
  // largest error to its largest value, since a block's scale is set by a
  // few massive activations and a ratio per element would mean nothing.
  // A plain loop rather than a section per block, since a section re-enters
  // the test case and uploads the model again.
  for (const auto n : iota(n_layer)) {
    {
      INFO("block_" << n);
      const auto dump = std::format("block_{}", n);
      const auto in_view =
          matrix_of(oracle.activations, dump + "/ln_1/in", n_embd);
      const auto exit =
          (n + 1 < n_layer)
              ? std::format("block_{}/ln_1/in", n + 1)
              : std::string{"ln_f/in"};
      const auto expected = matrix_of(oracle.activations, exit, n_embd);
      const auto token_count = in_view.row_extent();

      const cuda_matrix<float> in(in_view);
      cuda_matrix<float> out(in_view.extent());
      gpt2_engine<bfloat16_t>::block_activation_buffers owned(token_count,
          n_embd, n_hidden, n_head);
      const auto acts = owned.lenses();
      cuda_matrix<float> keys({.row_count = token_count, .col_count = n_embd});
      cuda_matrix<bfloat16_t> values(keys.extent());
      REQUIRE(engine.apply_block(out, in, n, acts, keys, values));

      check_close_to_scale(float_matrix_view(download(out), out.extent()),
          expected, bf16_exit_ratio);
      struct dumped {
        const char* name;
        cuda_matrix_view<float> actual;
        float ratio;
      };
      for (const auto& [name, actual, ratio] :
          {dumped{"attn/out", acts.attn_out, bf16_attn_out_ratio},
              dumped{"ln_2/in", acts.ln_2_in, bf16_ln_2_in_ratio},
              dumped{"mlp/out", acts.mlp_out, bf16_mlp_out_ratio}})
      {
        INFO(name);
        const auto storage = download(actual);
        check_close_to_scale(float_matrix_view(storage, actual.extent()),
            matrix_of(oracle.activations, dump + "/" + name, n_embd), ratio);
      }
    }
  }
}

TEST_CASE("Device bf16 greedy decoding follows the manifest",
    "[Gpt2Test][oracle][cuda]") {
  auto oracle = oracle_dumps::load();

  // The greedy gate over the bf16 engine. A pick between two close logits
  // can flip under bf16, and every pick after a flip follows a different
  // prefix, so only the first pick is required, and how far the run follows
  // the manifest is reported.
  const auto expected = read_greedy_continuation();
  const auto model = gpt2_model<float>::load(std::move(oracle.weights));
  const gpt2_engine<bfloat16_t> engine(model);
  auto ids = ids_of(oracle.logits,
      std::format("prompt_{}/input_ids", expected.prompt));
  const auto prompt_count = ids.size();
  REQUIRE(engine.generate(ids, expected.ids.size()));
  const auto generated = std::span{ids}.subspan(prompt_count);
  REQUIRE(!generated.empty());
  CHECK(generated.front() == expected.ids.front());

  const auto followed = static_cast<size_t>(
      std::ranges::mismatch(generated, expected.ids).in1 - generated.begin());
  if (followed < expected.ids.size())
    WARN("bf16 greedy decoding follows the manifest for "
         << followed << " of " << expected.ids.size() << " IDs");
}

TEST_CASE(
    "Device bf16 engine over a bf16 file matches the oracle within its "
    "tolerance",
    "[Gpt2Test][oracle][cuda]") {
  auto oracle = oracle_dumps::load();
  const cublas_handle blas;

  // The five-prompt gate over the engine built from the bf16 file, whose
  // matrices go up as they are and whose vectors widen to `float` on the
  // device. Against the engine built from the fp32 file, the only change is
  // that the layer norms' weights and biases and the residual stream's biases
  // arrive rounded to eight significant bits, so the gate is the same. The
  // worst relative error measured this way is 1.5e-2 on prompt 3, against
  // the fp32 file's 2.6e-2 (2026-09-25).
  const auto model =
      gpt2_model<bfloat16_t>::load(std::move(oracle.weights_bf16));
  const gpt2_engine<bfloat16_t> engine(model);
  const cuda_matrix<bfloat16_t> wte(model.wte);
  for (const auto n : iota(5)) {
    INFO("prompt_" << n);
    const auto prefix = std::format("prompt_{}", n);
    const auto id_storage = ids_of(oracle.logits, prefix + "/input_ids");
    const auto expected =
        matrix_of(oracle.logits, prefix + "/logits", n_vocab);
    const auto token_count = id_storage.size();
    REQUIRE(expected.row_extent() == token_count);

    cuda_matrix<bfloat16_t> trunk(
        {.row_count = token_count, .col_count = n_embd});
    cuda_matrix<float> residual(trunk.extent());
    gpt2_engine<bfloat16_t>::block_activation_buffers owned(token_count,
        n_embd, n_hidden, n_head);
    gpt2_engine<bfloat16_t>::kv_cache cache;
    REQUIRE(
        engine.forward(trunk, residual, id_storage, owned.lenses(), cache));

    cuda_matrix<float> logits(expected.extent());
    REQUIRE(corvid::cuda::llm::compute_logits(blas, logits, trunk, wte));

    check_device_close(logits, expected, bf16_logits_tolerance,
        bf16_logits_tolerance);
  }
}

TEMPLATE_TEST_CASE("Device model matches the oracle at full context",
    "[Gpt2Test][oracle][cuda]", float, bfloat16_t, double) {
  auto oracle = oracle_dumps::load();
  const cublas_handle blas;

  // The stress prompt, one token short of the context, through the model in
  // each element type: the last row of the logits against the row the oracle
  // dumped, at the type's tolerance, and the pick that follows it. Every
  // position is in use, so the attention scores are as wide as they get.
  using engine_t = gpt2_engine<TestType>;
  using wide_t = engine_t::wide_t;
  const auto ids = ids_of(oracle.logits, "stress/input_ids");
  const auto token_count = ids.size();
  REQUIRE(token_count == n_ctx - 1);
  const auto expected =
      matrix_of(oracle.logits, "stress/last_logits", n_vocab);
  REQUIRE(expected.row_extent() == 1);
  const auto model = gpt2_model<float>::load(std::move(oracle.weights));
  const engine_t engine(model);

  cuda_matrix<TestType> trunk({.row_count = token_count, .col_count = n_embd});
  cuda_matrix<wide_t> residual(trunk.extent());
  typename engine_t::block_activation_buffers owned(token_count, n_embd,
      n_hidden, n_head);
  typename engine_t::kv_cache cache;
  REQUIRE(engine.forward(trunk, residual, ids, owned.lenses(), cache));

  const cuda_matrix<float> wte_f32(model.wte);
  cuda_matrix<TestType> wte(wte_f32.extent());
  REQUIRE(corvid::cuda::linalg::convert(wte, wte_f32));
  cuda_matrix<wide_t> logits(expected.extent());
  const auto last_row =
      trunk[{row_ndx{token_count - 1}, col_ndx{0}}, matrix_extent::npos];
  REQUIRE(corvid::cuda::llm::compute_logits(blas, logits, last_row, wte));
  const auto values = download(logits);
  check_close(float_matrix_view(values, logits.extent()), expected,
      logits_tolerance<TestType>, logits_tolerance<TestType>);

  token_id next{};
  REQUIRE(engine.next_token(next, ids));
  CHECK(next == corvid::llm::pick_greedy(expected[row_ndx{0}]));
}

TEMPLATE_TEST_CASE("Device tokens per second", "[Gpt2Test][oracle][cuda]",
    float, bfloat16_t, double) {
  auto oracle = oracle_dumps::load();

  // The measurement stage 4 records, per element type: the stress prompt as
  // a prefill, one cached step at the full context after it, and the bisect
  // prompt's greedy run as cached steps at a short context, each through
  // `next_token`, which ends in a download and so returns when the device is
  // done. A one-token list goes first, so that the cache's allocation and
  // cuBLAS's warm-up fall outside the measured passes. The numbers are
  // reported, not checked. The roadmap records them.
  const auto model = gpt2_model<float>::load(std::move(oracle.weights));
  const gpt2_engine<TestType> engine(model);
  token_id next{};
  const std::vector<token_id> unrelated{gpt2_end_of_text};
  REQUIRE(engine.next_token(next, unrelated));

  auto ids = ids_of(oracle.logits, "stress/input_ids");
  const auto prefill_count = ids.size();
  fp_milliseconds prefill_ms;
  if (const auto timer = scope_timer(prefill_ms)) {
    REQUIRE(engine.next_token(next, ids));
  }
  ids.push_back(next);
  fp_milliseconds full_step_ms;
  if (const auto timer = scope_timer(full_step_ms)) {
    REQUIRE(engine.next_token(next, ids));
  }

  // The prompt runs first, so that every step of the run is cached.
  auto short_ids =
      ids_of(oracle.logits, std::format("prompt_{}/input_ids", bisect_prompt));
  const auto prompt_count = short_ids.size();
  REQUIRE(engine.next_token(next, short_ids));
  constexpr auto step_count = 20UZ;
  fp_milliseconds steps_ms;
  if (const auto timer = scope_timer(steps_ms)) {
    REQUIRE(engine.generate(short_ids, step_count));
  }
  const auto steps = short_ids.size() - prompt_count;
  REQUIRE(steps);
  const auto step_ms = steps_ms / static_cast<double>(steps);

  WARN(std::format(
      "prefill {} tokens: {:.1f} ms, {:.0f} tokens/s; decode at "
      "{} tokens: {:.2f} ms, {:.0f} tokens/s; decode from {} "
      "tokens over {} steps: {:.2f} ms/token, {:.0f} tokens/s",
      prefill_count, prefill_ms.count(),
      tokens_per_second(prefill_count, prefill_ms), ids.size(),
      full_step_ms.count(), tokens_per_second(1, full_step_ms), prompt_count,
      steps, step_ms.count(), tokens_per_second(1, step_ms)));
}

#pragma endregion

} // namespace corvid_tests

// NOLINTEND(readability-function-cognitive-complexity)
