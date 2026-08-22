// Tests for cuda_ptr: allocation, the host transfer overloads, and the
// count clamp that keeps a short or empty host buffer from being overrun.

#include <array>
#include <span>

#include <cuda_runtime.h>

#include "corvid/cuda/cuda_ptr.cuh"
#include "corvid/cuda/cuda_status.cuh"
#include "catch2_main.h"

using namespace corvid::cuda;

namespace {

// Fill every element of `host` with `value`.
void fill(std::span<int> host, int value) {
  for (auto& x : host) x = value;
}

} // namespace

#pragma region Allocation

TEST_CASE("cuda_ptr allocates and moves", "[cuda]") {
  cuda_ptr<int> a{4};
  REQUIRE(a.ok());
  const auto* raw = a.get();

  cuda_ptr<int> b = std::move(a);
  CHECK(b.get() == raw);
  CHECK_FALSE(a.ok()); // NOLINT(bugprone-use-after-move): moved-from is null

  a = std::move(b);
  CHECK(a.get() == raw);
  CHECK_FALSE(b.ok()); // NOLINT(bugprone-use-after-move): moved-from is null
}

#pragma endregion
#pragma region Transfer

TEST_CASE("cuda_ptr transfer overloads round-trip", "[cuda]") {
  cuda_ptr<int> d{4};
  REQUIRE(d.ok());

  SECTION("pointer and count") {
    const int in[]{1, 2, 3, 4};
    REQUIRE(d.load(in, 4));
    int out[4]{};
    REQUIRE(d.store(out, 4));
    CHECK(std::equal(std::begin(in), std::end(in), std::begin(out)));
  }

  SECTION("defaulted count is the whole allocation") {
    const int in[]{5, 6, 7, 8};
    REQUIRE(d.load(in, 4));
    int out[4]{};
    REQUIRE(d.store(out));
    CHECK(std::equal(std::begin(in), std::end(in), std::begin(out)));
  }

  SECTION("span") {
    std::array<int, 4> in{9, 10, 11, 12};
    REQUIRE(d.load(std::span<const int>{in}));
    std::array<int, 4> out{};
    REQUIRE(d.store(std::span<int>{out}));
    CHECK(out == in);
  }

  SECTION("array reference") {
    const int in[]{13, 14, 15, 16};
    REQUIRE(d.load(in));
    int out[4]{};
    REQUIRE(d.store(out));
    CHECK(std::equal(std::begin(in), std::end(in), std::begin(out)));
  }

  SECTION("single object") {
    REQUIRE(d.load(17));
    int out{};
    REQUIRE(d.store(out));
    CHECK(out == 17);
  }
}

TEST_CASE("cuda_ptr clamps the count to the host buffer", "[cuda]") {
  cuda_ptr<int> d{4};
  REQUIRE(d.ok());
  const int in[]{1, 2, 3, 4};
  REQUIRE(d.load(in));

  SECTION("empty span stores nothing") {
    // An empty span once meant "the whole allocation", so it overran its
    // (null) buffer. A guard element past the span's end shows nothing was
    // written.
    std::array<int, 2> out{};
    fill(out, -1);
    REQUIRE(d.store(std::span<int>{out}.first(0)));
    CHECK(out[0] == -1);
    CHECK(out[1] == -1);
  }

  SECTION("empty span loads nothing") {
    std::array<int, 0> none{};
    REQUIRE(d.load(std::span<const int>{none}));
    int out[4]{};
    REQUIRE(d.store(out));
    CHECK(std::equal(std::begin(in), std::end(in), std::begin(out)));
  }

  SECTION("a short span copies only its length") {
    std::array<int, 4> out{};
    fill(out, -1);
    REQUIRE(d.store(std::span<int>{out}.first(2)));
    CHECK(out[0] == 1);
    CHECK(out[1] == 2);
    CHECK(out[2] == -1);
    CHECK(out[3] == -1);
  }

  SECTION("a count past the allocation is clamped to it") {
    std::array<int, 8> out{};
    fill(out, -1);
    REQUIRE(d.store(out.data(), 8));
    CHECK(out[3] == 4);
    CHECK(out[4] == -1);
  }
}

#pragma endregion
