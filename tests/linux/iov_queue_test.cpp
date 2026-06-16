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

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <sys/uio.h>
#include <vector>

#include "corvid/proto/iov_queue.h"
#include "catch2_main.h"

using corvid::proto::iov_queue;

namespace {

std::vector<uint8_t> bytes(std::initializer_list<uint8_t> il) {
  return std::vector<uint8_t>{il};
}

// Flatten an iovec view back into a byte vector, for content comparison.
std::vector<uint8_t> flatten(std::span<const iovec> iov) {
  std::vector<uint8_t> out;
  for (const auto& e : iov) {
    const auto* p = static_cast<const uint8_t*>(e.iov_base);
    out.insert(out.end(), p, p + e.iov_len);
  }
  return out;
}

// A harvested span back to a byte vector, for content comparison.
std::vector<uint8_t> as_vec(std::span<const uint8_t> s) {
  return {s.begin(), s.end()};
}

// Simulate a scatter read of `src` into the queue's free room, returning the
// count written (capped at the available room across the unused iovecs).
size_t fill(iov_queue<>& q, std::span<const uint8_t> src) {
  auto iov = q.unused();
  size_t written = 0;
  for (const auto& e : iov) {
    if (written >= src.size()) break;
    const size_t take = std::min(e.iov_len, src.size() - written);
    std::memcpy(e.iov_base, src.data() + written, take);
    written += take;
  }
  return written;
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("empty queue is empty", "[iov_queue]") {
  iov_queue<> q;
  CHECK(q.size() == 0);
  CHECK(q.unacknowledged() == 0);
  CHECK(q.unused().empty());
  CHECK(q.appended() == 0);
  CHECK(q.used() == 0);
  CHECK(q.reclaimed() == 0);
  CHECK(q.retained_chunks() == 0);
  CHECK(q.slack() == 0);
}

#pragma region send-style use

TEST_CASE("append takes ownership and drops empties", "[iov_queue]") {
  iov_queue<> q;
  CHECK_FALSE(q.append({})); // empty chunk is dropped
  CHECK(q.append(bytes({1, 2, 3})));
  CHECK_FALSE(q.append({})); // still dropped
  CHECK(q.appended() == 3);
  CHECK(q.size() == 3);
  CHECK(q.retained_chunks() == 1);
}

TEST_CASE("unused gathers across chunks", "[iov_queue]") {
  iov_queue<> q;
  CHECK(q.append(bytes({1, 2, 3})));
  CHECK(q.append(bytes({4, 5})));
  CHECK(q.append(bytes({6, 7, 8, 9})));

  auto iov = q.unused();
  CHECK(iov.size() == 3);
  CHECK(iov_queue<>::iov_byte_count(iov) == 9);
  CHECK(flatten(iov) == bytes({1, 2, 3, 4, 5, 6, 7, 8, 9}));
}

TEST_CASE("consume shrinks the unused window mid-chunk", "[iov_queue]") {
  iov_queue<> q;
  CHECK(q.append(bytes({1, 2, 3})));
  CHECK(q.append(bytes({4, 5})));

  CHECK(q.consume(1));
  CHECK(q.size() == 4);
  CHECK(q.unacknowledged() == 1);

  // The front chunk's first byte is now used; the view starts mid-chunk.
  auto iov = q.unused();
  CHECK(iov.size() == 2);
  CHECK(iov[0].iov_len == 2); // {2, 3}
  CHECK(flatten(iov) == bytes({2, 3, 4, 5}));
}

TEST_CASE("consume past a chunk boundary", "[iov_queue]") {
  iov_queue<> q;
  CHECK(q.append(bytes({1, 2, 3})));
  CHECK(q.append(bytes({4, 5})));
  CHECK(q.append(bytes({6, 7, 8, 9})));

  CHECK(q.consume(4)); // past chunk 0, one into chunk 1
  CHECK(q.size() == 5);
  CHECK(flatten(q.unused()) == bytes({5, 6, 7, 8, 9}));
  // Nothing reclaimed yet, so all three chunks are still held.
  CHECK(q.retained_chunks() == 3);
}

TEST_CASE("consume landing on a boundary points one past", "[iov_queue]") {
  iov_queue<> q;
  CHECK(q.append(bytes({1, 2, 3})));
  CHECK(q.consume(3)); // exactly the whole chunk
  CHECK(q.size() == 0);
  CHECK(q.unused().empty()); // cursor is one past the back, not stuck at end

  // A later append lands right where the cursor points and is offered.
  CHECK(q.append(bytes({4, 5})));
  CHECK(flatten(q.unused()) == bytes({4, 5}));
}

TEST_CASE("retire frees only fully covered front chunks", "[iov_queue]") {
  iov_queue<> q;
  CHECK(q.append(bytes({1, 2, 3})));
  CHECK(q.append(bytes({4, 5})));
  CHECK(q.append(bytes({6, 7, 8, 9})));
  CHECK(q.consume(9));

  SECTION("partial retire of the front chunk keeps it") {
    CHECK(q.retire(2)); // < 3, chunk 0 still straddled
    CHECK(q.retained_chunks() == 3);
    CHECK(q.reclaimed() == 2);
    CHECK(q.unacknowledged() == 7);
    CHECK(q.slack() == 0);
  }

  SECTION("retiring a whole chunk frees it") {
    CHECK(q.retire(3));              // exactly chunk 0
    CHECK(q.retained_chunks() == 2); // chunk 0 dropped, two still held
    CHECK(q.reclaimed() == 3);
    CHECK(q.unacknowledged() == 6); // 9 used, 3 reclaimed
    CHECK(q.unused().empty());      // all 9 already used
    CHECK(q.slack() == 1);
  }

  SECTION("retiring everything drains the queue") {
    CHECK(q.retire(9));
    CHECK(q.retained_chunks() == 0);
    CHECK(q.size() == 0);
    CHECK(q.unacknowledged() == 0);
    CHECK(q.unused().empty());
    CHECK(q.slack() == 3);
  }
}

TEST_CASE("reliable caller may retire without consume", "[iov_queue]") {
  iov_queue<> q;
  CHECK(q.append(bytes({1, 2, 3, 4})));

  CHECK(q.retire(2));   // no consume first
  CHECK(q.used() == 2); // pulled along by retire
  CHECK(q.size() == 2);
  CHECK(q.unacknowledged() == 0);
  CHECK(flatten(q.unused()) == bytes({3, 4}));
}

// The pattern a gather-write sink uses when it can only take a bounded number
// of segments per pull (e.g. nghttp3's veccnt): cap `unused` with `first`, sum
// the capped prefix with `iov_byte_count`, and `consume` exactly that, so the
// next pull resumes past the handed-off bytes.
TEST_CASE("budget-capped unused with exact consume", "[iov_queue]") {
  iov_queue<> q;
  for (uint8_t i = 1; i <= 20; ++i) CHECK(q.append(bytes({i})));
  CHECK(q.appended() == 20);

  constexpr size_t budget = 16;
  const auto all = q.unused();
  CHECK(all.size() == 20);
  const auto capped = all.first(std::min(all.size(), budget));
  CHECK(capped.size() == 16);
  const uint64_t n = iov_queue<>::iov_byte_count(capped);
  CHECK(n == 16);
  q.consume(n);

  // The next pull resumes exactly past the consumed bytes.
  CHECK(q.size() == 4);
  const auto rest = q.unused();
  CHECK(rest.size() == 4);
  CHECK(flatten(rest) == bytes({17, 18, 19, 20}));
}

#pragma endregion
#pragma region receive-style use

TEST_CASE("receive round-trip: fill, consume, harvest_bytes", "[iov_queue]") {
  iov_queue<> q;
  // Append empty, pre-sized capacity.
  CHECK(q.append(std::vector<uint8_t>(3)));
  CHECK(q.append(std::vector<uint8_t>(3)));
  CHECK(q.size() == 6); // free capacity, nothing filled yet

  // The kernel scatters 5 bytes into the free room.
  CHECK(fill(q, std::vector<uint8_t>{10, 11, 12, 13, 14}) == 5);
  CHECK(q.consume(5));
  CHECK(q.unacknowledged() == 5); // filled, not yet harvested
  CHECK(q.size() == 1);           // one byte of free room left

  // Harvest the filled payload into a caller buffer.
  std::vector<uint8_t> out(8);
  auto good = q.harvest_bytes(out);
  CHECK(as_vec(good) == bytes({10, 11, 12, 13, 14}));
  CHECK(q.unacknowledged() == 0); // all harvested
  CHECK(q.reclaimed() == 5);
}

TEST_CASE("harvest_bytes is bounded by the out buffer and leaves slack",
    "[iov_queue]") {
  iov_queue<> q;
  CHECK(q.append(std::vector<uint8_t>(2)));
  CHECK(q.append(std::vector<uint8_t>(2)));
  CHECK(q.append(std::vector<uint8_t>(2)));
  CHECK(fill(q, std::vector<uint8_t>{1, 2, 3, 4, 5, 6}) == 6);
  CHECK(q.consume(6));

  // out only has room for 5; the sixth byte stays.
  std::vector<uint8_t> out(5);
  CHECK(as_vec(q.harvest_bytes(out)) == bytes({1, 2, 3, 4, 5}));
  CHECK(q.unacknowledged() == 1);
  // Two front buffers fully drained but not freed: they are slack.
  CHECK(q.slack() == 2);
  CHECK(q.retained_chunks() == 1);

  // The remaining byte harvests next.
  std::vector<uint8_t> rest(4);
  CHECK(as_vec(q.harvest_bytes(rest)) == bytes({6}));
  CHECK(q.unacknowledged() == 0);

  // tidy frees the slack buffers harvest_bytes left behind.
  CHECK(q.tidy());
  CHECK(q.slack() == 0);
}

TEST_CASE("harvest_bytes writes at an offset within out", "[iov_queue]") {
  iov_queue<> q;
  CHECK(q.append(std::vector<uint8_t>(4)));
  CHECK(fill(q, std::vector<uint8_t>{1, 2, 3, 4}) == 4);
  CHECK(q.consume(4));

  SECTION("accumulates after existing content") {
    std::vector<uint8_t> out{9, 9, 0, 0, 0, 0}; // 2-byte header, room for 4
    auto good = q.harvest_bytes(out, 2);
    CHECK(
        as_vec(good) == bytes({1, 2, 3, 4})); // span covers only what we wrote
    CHECK(out == bytes({9, 9, 1, 2, 3, 4}));  // header left intact
    CHECK(q.unacknowledged() == 0);
  }

  SECTION("the offset reduces the room") {
    std::vector<uint8_t> out(3); // `at` of 2 leaves room for 1
    CHECK(as_vec(q.harvest_bytes(out, 2)) == bytes({1}));
    CHECK(q.unacknowledged() == 3); // the other three stay
  }
}

TEST_CASE("harvest_chunk moves out filled buffers, refuses the tail",
    "[iov_queue]") {
  iov_queue<> q;
  CHECK(q.append(std::vector<uint8_t>(4)));
  CHECK(q.append(std::vector<uint8_t>(4)));
  // Fill 6 of 8: buffer 0 full, buffer 1 half.
  CHECK(fill(q, std::vector<uint8_t>{1, 2, 3, 4, 5, 6}) == 6);
  CHECK(q.consume(6));

  // The fully-filled front buffer hands back whole.
  std::vector<uint8_t> out;
  CHECK(as_vec(q.harvest_chunk(out)) == bytes({1, 2, 3, 4}));
  CHECK(out.size() == 4); // ownership transferred to the caller
  CHECK(q.reclaimed() == 4);

  // The half-filled tail buffer is not moved out; the kernel may still fill
  // it.
  std::vector<uint8_t> none{9, 9}; // non-empty going in
  CHECK(q.harvest_chunk(none).empty());
  CHECK(none.empty()); // cleared on refusal
  CHECK(q.reclaimed() == 4);
}

TEST_CASE("mixing harvest_bytes then harvest_chunk yields a mid-chunk span",
    "[iov_queue]") {
  iov_queue<> q;
  CHECK(q.append(std::vector<uint8_t>(4)));
  CHECK(fill(q, std::vector<uint8_t>{1, 2}) == 2); // partial fill
  CHECK(q.consume(2));

  // Take the filled front by copy, advancing into the middle of the buffer.
  std::vector<uint8_t> head(2);
  CHECK(as_vec(q.harvest_bytes(head)) == bytes({1, 2}));

  // Fill the rest of the same buffer and consume it.
  CHECK(fill(q, std::vector<uint8_t>{3, 4}) == 2);
  CHECK(q.consume(2)); // buffer now full

  // harvest_chunk hands back the buffer, but the good span starts past the
  // already-harvested prefix.
  std::vector<uint8_t> tail;
  CHECK(as_vec(q.harvest_chunk(tail)) == bytes({3, 4}));
  CHECK(tail.size() == 4); // the whole moved buffer, span covered only {3,4}
  CHECK(q.reclaimed() == 4);
  CHECK(q.unacknowledged() == 0);
}

TEST_CASE("recycle returns drained buffers as fresh capacity", "[iov_queue]") {
  iov_queue<> q;
  CHECK(q.append(std::vector<uint8_t>(2)));
  CHECK(q.append(std::vector<uint8_t>(2)));
  CHECK(q.append(std::vector<uint8_t>(2)));

  // Fill and drain the first two buffers by copy; the third stays free room.
  CHECK(fill(q, std::vector<uint8_t>{1, 2, 3, 4}) == 4);
  CHECK(q.consume(4));
  std::vector<uint8_t> out(4);
  CHECK(as_vec(q.harvest_bytes(out)) == bytes({1, 2, 3, 4}));
  CHECK(q.slack() == 2); // two drained buffers, still allocated
  CHECK(q.retained_chunks() == 1);
  CHECK(q.size() == 2); // only the third buffer is free

  const auto appended_before = q.appended();

  // Recycle the drained buffers to the back as fresh capacity.
  CHECK(q.recycle());
  CHECK(q.slack() == 0);
  CHECK(q.retained_chunks() == 3);            // all three owned again
  CHECK(q.appended() == appended_before + 4); // their 4 bytes re-added
  CHECK(q.size() == 6);                       // full capacity available again
  CHECK(q.unacknowledged() == 0);

  // The recycled room accepts a fresh read across all of it, in order.
  CHECK(fill(q, std::vector<uint8_t>{5, 6, 7, 8, 9, 10}) == 6);
  CHECK(q.consume(6));
  std::vector<uint8_t> out2(6);
  CHECK(as_vec(q.harvest_bytes(out2)) == bytes({5, 6, 7, 8, 9, 10}));
}

TEST_CASE("recycle with no slack is a no-op", "[iov_queue]") {
  iov_queue<> q;
  CHECK(q.append(std::vector<uint8_t>(4)));
  CHECK(fill(q, std::vector<uint8_t>{1, 2}) == 2);
  CHECK(q.consume(2));

  CHECK(q.recycle()); // nothing fully reclaimed yet
  CHECK(q.appended() == 4);
  CHECK(q.size() == 2);
  CHECK(q.unacknowledged() == 2);
  CHECK(q.retained_chunks() == 1);
}

#pragma endregion
#pragma region tidy

TEST_CASE("tidy compacts freed slots, live bytes survive", "[iov_queue]") {
  iov_queue<> q;
  CHECK(q.append(bytes({1, 2})));
  CHECK(q.append(bytes({3, 4})));
  CHECK(q.append(bytes({5, 6})));
  CHECK(q.consume(4));
  CHECK(q.retire(4)); // frees the first two chunks

  CHECK(q.retained_chunks() == 1);
  CHECK(q.slack() == 2); // the two freed slots are still present
  CHECK(flatten(q.unused()) == bytes({5, 6}));

  CHECK(q.tidy());
  CHECK(q.slack() == 0); // tidy dropped the freed slots
  CHECK(q.retained_chunks() == 1);
  CHECK(flatten(q.unused()) == bytes({5, 6}));
  CHECK(q.size() == 2);
  CHECK(q.appended() == 6); // preserve leaves the watermarks alone

  // The queue keeps working after a tidy.
  CHECK(q.append(bytes({7, 8})));
  CHECK(flatten(q.unused()) == bytes({5, 6, 7, 8}));
}

TEST_CASE("tidy(release) rebases watermarks on a chunk boundary",
    "[iov_queue]") {
  using corvid::proto::deallocation_policy;
  iov_queue<> q;
  CHECK(q.append(bytes({1, 2})));
  CHECK(q.append(bytes({3, 4})));
  CHECK(q.append(bytes({5, 6})));
  CHECK(q.consume(5));   // 5 of 6 used
  CHECK(q.retire(4));    // frees chunks 0 and 1; lands on chunk 2's start
  CHECK(q.slack() == 2); // two freed slots awaiting tidy

  CHECK(q.tidy(deallocation_policy::release));
  CHECK(q.slack() == 0);
  // Watermarks rebased down by the 4 freed bytes; deltas preserved.
  CHECK(q.appended() == 2);
  CHECK(q.used() == 1);
  CHECK(q.reclaimed() == 0);
  CHECK(q.retained_chunks() == 1);
  CHECK(q.size() == 1);
  CHECK(q.unacknowledged() == 1); // used - reclaimed survives the rebase
  CHECK(flatten(q.unused()) == bytes({6}));
}

TEST_CASE("tidy(release) rebases watermarks mid-chunk", "[iov_queue]") {
  using corvid::proto::deallocation_policy;
  iov_queue<> q;
  CHECK(q.append(bytes({1, 2})));
  CHECK(q.append(bytes({3, 4})));
  CHECK(q.append(bytes({5, 6})));
  CHECK(q.consume(5));
  CHECK(q.retire(3)); // frees chunk 0; reclaimed lands mid chunk 1 (byte {3})
  CHECK(q.retained_chunks() == 2);

  CHECK(q.tidy(deallocation_policy::release));
  // Only the one dropped chunk's 2 bytes leave; chunk 1's reclaimed byte
  // stays.
  CHECK(q.appended() == 4);  // {3,4,5,6} remain physically
  CHECK(q.used() == 3);      // {3,4,5} used from the new origin
  CHECK(q.reclaimed() == 1); // byte {3} still reclaimed within the front chunk
  CHECK(q.unacknowledged() == 2);
  CHECK(q.size() == 1);
  CHECK(flatten(q.unused()) == bytes({6}));
}

#pragma endregion
#pragma region chunk and state types

TEST_CASE("string chunk type with defeated SSO", "[iov_queue]") {
  // Clang-tidy is drunk.
  // NOLINTBEGIN(bugprone-use-after-move)
  iov_queue<std::string> q;
  std::string s;
  s.reserve(64); // force heap storage so the buffer survives moves
  s = "hello";
  CHECK(q.append(std::move(s)));
  CHECK(q.size() == 5);

  auto iov = q.unused();
  REQUIRE(iov.size() == 1);
  CHECK(iov[0].iov_len == 5);
  const auto* p = static_cast<const char*>(iov[0].iov_base);
  CHECK(std::string(p, 5) == "hello");
  // NOLINTEND(bugprone-use-after-move)
}

namespace {
enum class tflags : uint8_t { none = 0, fin = 1, more = 2 };
} // namespace

TEST_CASE("State is carried; void adds no storage", "[iov_queue]") {
  // A void State must not enlarge the queue the way a real one does.
  static_assert(
      sizeof(iov_queue<std::vector<uint8_t>, void>) <
      sizeof(iov_queue<std::vector<uint8_t>, std::array<std::byte, 64>>));

  iov_queue<std::vector<uint8_t>, tflags> q;
  CHECK(q.state() == tflags::none); // value-initialized
  q.state() = tflags::fin;
  CHECK(q.state() == tflags::fin);

  // Readable through a const reference (deducing-this accessor).
  const auto& cq = q;
  CHECK(cq.state() == tflags::fin);
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
