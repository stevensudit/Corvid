// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
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

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "../corvid/ecs.h"
#include "minitest.h"

using namespace std::literals;
using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

using int_stable_ids = stable_ids<int>;

void ArchetypeVector_Basic() {
  enum class entity_id : std::uint32_t {};
  using archetype_t =
      archetype_vector<entity_id, std::tuple<int, float, std::string>>;

  // Default construction and empty state.
  if (true) {
    archetype_t v;
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0U);
  }

  // Construction with allocator.
  if (true) {
    std::allocator<std::byte> alloc;
    archetype_t v{alloc};
    EXPECT_TRUE(v.empty());
  }

  // reserve() and capacity().
  if (true) {
    archetype_t v;
    v.reserve(10);
    EXPECT_GE(v.capacity(), 10U);
    EXPECT_TRUE(v.empty()); // reserve doesn't change size.
  }

  // resize() and size().
  if (true) {
    archetype_t v;
    v.resize(5);
    EXPECT_EQ(v.size(), 5U);
    EXPECT_FALSE(v.empty());
    v.resize(3);
    EXPECT_EQ(v.size(), 3U);
    v.resize(0);
    EXPECT_TRUE(v.empty());
  }

  // clear().
  if (true) {
    archetype_t v;
    v.resize(5);
    v.clear();
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0U);
  }

  // shrink_to_fit().
  if (true) {
    archetype_t v;
    v.reserve(100);
    v.resize(2);
    auto cap_before = v.capacity();
    v.shrink_to_fit();
    // After shrink_to_fit, capacity should be <= previous (implementation
    // dependent, but typically reduces).
    EXPECT_LE(v.capacity(), cap_before);
    EXPECT_EQ(v.size(), 2U);
  }

  // set_index_to_id() and index_to_id().
  if (true) {
    archetype_t v;
    v.set_index_to_id([](archetype_t::size_type index) {
      return entity_id{static_cast<std::uint32_t>(index + 100)};
    });
    EXPECT_EQ(v.index_to_id(0), entity_id{100});
    EXPECT_EQ(v.index_to_id(5), entity_id{105});
    EXPECT_EQ(v.index_to_id(99), entity_id{199});
  }

  // emplace_back() without parameters.
  if (true) {
    archetype_t v;
    v.emplace_back();
    EXPECT_EQ(v.size(), 1U);
    v.emplace_back();
    v.emplace_back();
    EXPECT_EQ(v.size(), 3U);
  }

  // emplace_back(Args...) with parameters.
  if (true) {
    archetype_t v;
    v.emplace_back(42, 3.14f, "hello"s);
    EXPECT_EQ(v.size(), 1U);
    auto ints = v.get_component_span<int>();
    auto floats = v.get_component_span<float>();
    auto strings = v.get_component_span<std::string>();
    EXPECT_EQ(ints[0], 42);
    EXPECT_NEAR(floats[0], 3.14f, 0.001f);
    EXPECT_EQ(strings[0], "hello"s);

    v.emplace_back(99, 2.71f, "world"s);
    EXPECT_EQ(v.size(), 2U);
    // Re-fetch spans after potential reallocation.
    ints = v.get_component_span<int>();
    floats = v.get_component_span<float>();
    strings = v.get_component_span<std::string>();
    EXPECT_EQ(ints[1], 99);
    EXPECT_NEAR(floats[1], 2.71f, 0.001f);
    EXPECT_EQ(strings[1], "world"s);
  }

  // get_component_span<T>() by type.
  if (true) {
    archetype_t v;
    v.emplace_back(1, 1.0f, "one"s);
    v.emplace_back(2, 2.0f, "two"s);

    auto int_span = v.get_component_span<int>();
    auto float_span = v.get_component_span<float>();
    auto string_span = v.get_component_span<std::string>();

    EXPECT_EQ(int_span.size(), 2U);
    EXPECT_EQ(float_span.size(), 2U);
    EXPECT_EQ(string_span.size(), 2U);

    EXPECT_EQ(int_span[0], 1);
    EXPECT_EQ(int_span[1], 2);
    EXPECT_EQ(float_span[0], 1.0f);
    EXPECT_EQ(float_span[1], 2.0f);
    EXPECT_EQ(string_span[0], "one"s);
    EXPECT_EQ(string_span[1], "two"s);
  }

  // get_component_span<Index>() by index.
  if (true) {
    archetype_t v;
    v.emplace_back(10, 10.5f, "ten"s);

    auto span0 = v.get_component_span<0>(); // int
    auto span1 = v.get_component_span<1>(); // float
    auto span2 = v.get_component_span<2>(); // string

    EXPECT_EQ(span0[0], 10);
    EXPECT_EQ(span1[0], 10.5f);
    EXPECT_EQ(span2[0], "ten"s);
  }

  // get_component_vector<T>() by type.
  if (true) {
    archetype_t v;
    v.emplace_back(5, 5.5f, "five"s);

    const auto& int_vec = v.get_component_vector<int>();
    const auto& float_vec = v.get_component_vector<float>();
    const auto& string_vec = v.get_component_vector<std::string>();

    EXPECT_EQ(int_vec.size(), 1U);
    EXPECT_EQ(int_vec[0], 5);
    EXPECT_EQ(float_vec[0], 5.5f);
    EXPECT_EQ(string_vec[0], "five"s);
  }

  // get_component_vector<Index>() by index.
  if (true) {
    archetype_t v;
    v.emplace_back(7, 7.7f, "seven"s);

    const auto& vec0 = v.get_component_vector<0>();
    const auto& vec1 = v.get_component_vector<1>();
    const auto& vec2 = v.get_component_vector<2>();

    EXPECT_EQ(vec0[0], 7);
    EXPECT_EQ(vec1[0], 7.7f);
    EXPECT_EQ(vec2[0], "seven"s);
  }

  // get_component_spans_tuple().
  if (true) {
    archetype_t v;
    v.emplace_back(100, 1.1f, "a"s);
    v.emplace_back(200, 2.2f, "b"s);
    v.emplace_back(300, 3.3f, "c"s);

    auto [ints, floats, strings] = v.get_component_spans_tuple();
    EXPECT_EQ(ints.size(), 3U);
    EXPECT_EQ(ints[0], 100);
    EXPECT_EQ(ints[1], 200);
    EXPECT_EQ(ints[2], 300);
    EXPECT_EQ(floats[0], 1.1f);
    EXPECT_EQ(strings[2], "c"s);
  }

  // Mutable span modification.
  if (true) {
    archetype_t v;
    v.resize(2);
    auto ints = v.get_component_span<int>();
    auto strings = v.get_component_span<std::string>();
    ints[0] = 42;
    ints[1] = 84;
    strings[0] = "first"s;
    strings[1] = "second"s;

    EXPECT_EQ(v.get_component_span<int>()[0], 42);
    EXPECT_EQ(v.get_component_span<int>()[1], 84);
    EXPECT_EQ(v.get_component_span<std::string>()[0], "first"s);
    EXPECT_EQ(v.get_component_span<std::string>()[1], "second"s);
  }

  // Const span access.
  if (true) {
    archetype_t v;
    v.emplace_back(10, 1.0f, "test"s);
    const archetype_t& cv = v;

    auto const_ints = cv.get_component_span<int>();
    auto const_floats = cv.get_component_span<1>();
    EXPECT_EQ(const_ints[0], 10);
    EXPECT_EQ(const_floats[0], 1.0f);
  }

  // operator[] mutable - row_lens.
  if (true) {
    archetype_t v;
    v.set_index_to_id([](archetype_t::size_type index) {
      return entity_id{static_cast<std::uint32_t>(index * 10)};
    });
    v.emplace_back(1, 1.0f, "one"s);
    v.emplace_back(2, 2.0f, "two"s);

    auto row = v[0];
    EXPECT_EQ(row.get_index(), 0U);
    EXPECT_EQ(row.get_id(), entity_id{0});

    auto row1 = v[1];
    EXPECT_EQ(row1.get_index(), 1U);
    EXPECT_EQ(row1.get_id(), entity_id{10});
  }

  // operator[] const - row_view.
  if (true) {
    archetype_t v;
    v.set_index_to_id([](archetype_t::size_type index) {
      return entity_id{static_cast<std::uint32_t>(index + 50)};
    });
    v.emplace_back(99, 9.9f, "ninety-nine"s);
    const archetype_t& cv = v;

    auto row = cv[0];
    EXPECT_EQ(row.get_index(), 0U);
    EXPECT_EQ(row.get_id(), entity_id{50});
  }

  // row_lens::component<T>() mutable.
  if (true) {
    archetype_t v;
    v.emplace_back(0, 0.0f, ""s);

    auto row = v[0];
    row.component<int>() = 123;
    row.component<float>() = 4.56f;
    row.component<std::string>() = "modified"s;

    EXPECT_EQ(v.get_component_span<int>()[0], 123);
    EXPECT_EQ(v.get_component_span<float>()[0], 4.56f);
    EXPECT_EQ(v.get_component_span<std::string>()[0], "modified"s);
  }

  // row_lens::component<T>() const.
  if (true) {
    archetype_t v;
    v.emplace_back(77, 7.7f, "seventy-seven"s);

    const auto row = v[0];
    EXPECT_EQ(row.component<int>(), 77);
    EXPECT_EQ(row.component<float>(), 7.7f);
    EXPECT_EQ(row.component<std::string>(), "seventy-seven"s);
  }

  // row_lens::component<Index>() mutable.
  if (true) {
    archetype_t v;
    v.emplace_back(0, 0.0f, ""s);

    auto row = v[0];
    row.component<0>() = 456;
    row.component<1>() = 7.89f;
    row.component<2>() = "by-index"s;

    EXPECT_EQ(v.get_component_span<0>()[0], 456);
    EXPECT_EQ(v.get_component_span<1>()[0], 7.89f);
    EXPECT_EQ(v.get_component_span<2>()[0], "by-index"s);
  }

  // row_lens::component<Index>() const.
  if (true) {
    archetype_t v;
    v.emplace_back(11, 1.1f, "eleven"s);

    const auto row = v[0];
    EXPECT_EQ(row.component<0>(), 11);
    EXPECT_EQ(row.component<1>(), 1.1f);
    EXPECT_EQ(row.component<2>(), "eleven"s);
  }

  // row_lens::components() mutable.
  if (true) {
    archetype_t v;
    v.emplace_back(0, 0.0f, ""s);

    auto row = v[0];
    auto [i, f, s] = row.components();
    i = 999;
    f = 9.99f;
    s = "components-modified"s;

    EXPECT_EQ(v.get_component_span<int>()[0], 999);
    EXPECT_EQ(v.get_component_span<float>()[0], 9.99f);
    EXPECT_EQ(v.get_component_span<std::string>()[0], "components-modified"s);
  }

  // row_lens::components() const.
  if (true) {
    archetype_t v;
    v.emplace_back(333, 3.33f, "three-three-three"s);

    const auto row = v[0];
    auto [i, f, s] = row.components();
    EXPECT_EQ(i, 333);
    EXPECT_EQ(f, 3.33f);
    EXPECT_EQ(s, "three-three-three"s);
  }

  // row_view (const operator[]).
  if (true) {
    archetype_t v;
    v.emplace_back(555, 5.55f, "five-five-five"s);
    const archetype_t& cv = v;

    auto view = cv[0];
    EXPECT_EQ(view.component<int>(), 555);
    EXPECT_EQ(view.component<float>(), 5.55f);
    EXPECT_EQ(view.component<std::string>(), "five-five-five"s);
    EXPECT_EQ(view.component<0>(), 555);
    EXPECT_EQ(view.component<1>(), 5.55f);
    EXPECT_EQ(view.component<2>(), "five-five-five"s);

    auto [i, f, s] = view.components();
    EXPECT_EQ(i, 555);
    EXPECT_EQ(f, 5.55f);
    EXPECT_EQ(s, "five-five-five"s);
  }

  // row_lens::swap_elements().
  if (true) {
    archetype_t v;
    v.emplace_back(1, 1.0f, "one"s);
    v.emplace_back(2, 2.0f, "two"s);
    v.emplace_back(3, 3.0f, "three"s);

    v.swap_elements(0, 2);

    EXPECT_EQ(v.get_component_span<int>()[0], 3);
    EXPECT_EQ(v.get_component_span<float>()[0], 3.0f);
    EXPECT_EQ(v.get_component_span<std::string>()[0], "three"s);
    EXPECT_EQ(v.get_component_span<int>()[2], 1);
    EXPECT_EQ(v.get_component_span<float>()[2], 1.0f);
    EXPECT_EQ(v.get_component_span<std::string>()[2], "one"s);
    // Middle element unchanged.
    EXPECT_EQ(v.get_component_span<int>()[1], 2);
  }

  // Member swap().
  if (true) {
    archetype_t v1;
    v1.emplace_back(10, 1.0f, "v1"s);
    v1.set_index_to_id([](archetype_t::size_type) { return entity_id{100}; });

    archetype_t v2;
    v2.emplace_back(20, 2.0f, "v2-a"s);
    v2.emplace_back(30, 3.0f, "v2-b"s);
    v2.set_index_to_id([](archetype_t::size_type) { return entity_id{200}; });

    v1.swap(v2);

    EXPECT_EQ(v1.size(), 2U);
    EXPECT_EQ(v2.size(), 1U);
    EXPECT_EQ(v1.get_component_span<int>()[0], 20);
    EXPECT_EQ(v1.get_component_span<int>()[1], 30);
    EXPECT_EQ(v2.get_component_span<int>()[0], 10);
    EXPECT_EQ(v1.index_to_id(0), entity_id{200});
    EXPECT_EQ(v2.index_to_id(0), entity_id{100});
  }

  // Friend swap().
  if (true) {
    archetype_t v1;
    v1.emplace_back(111, 1.11f, "v1"s);

    archetype_t v2;
    v2.emplace_back(222, 2.22f, "v2"s);

    using std::swap;
    swap(v1, v2);

    EXPECT_EQ(v1.get_component_span<int>()[0], 222);
    EXPECT_EQ(v2.get_component_span<int>()[0], 111);
    EXPECT_EQ(v1.get_component_span<std::string>()[0], "v2"s);
    EXPECT_EQ(v2.get_component_span<std::string>()[0], "v1"s);
  }

  // Move constructor.
  if (true) {
    archetype_t v1;
    v1.emplace_back(42, 4.2f, "move-me"s);
    v1.set_index_to_id([](archetype_t::size_type ndx) {
      return entity_id{ndx + 1000};
    });

    archetype_t v2{std::move(v1)};

    EXPECT_EQ(v2.size(), 1U);
    EXPECT_EQ(v2.get_component_span<int>()[0], 42);
    EXPECT_EQ(v2.get_component_span<float>()[0], 4.2f);
    EXPECT_EQ(v2.get_component_span<std::string>()[0], "move-me"s);
    EXPECT_EQ(v2.index_to_id(5), entity_id{1005});
  }

  // Move assignment.
  if (true) {
    archetype_t v1;
    v1.emplace_back(88, 8.8f, "source"s);

    archetype_t v2;
    v2.emplace_back(99, 9.9f, "dest-old"s);
    v2.emplace_back(100, 10.0f, "dest-old-2"s);

    v2 = std::move(v1);

    EXPECT_EQ(v2.size(), 1U);
    EXPECT_EQ(v2.get_component_span<int>()[0], 88);
    EXPECT_EQ(v2.get_component_span<std::string>()[0], "source"s);
  }

  // Multiple operations sequence.
  if (true) {
    archetype_t v;
    v.set_index_to_id([](archetype_t::size_type index) {
      return entity_id{static_cast<std::uint32_t>(index + 100)};
    });

    v.reserve(4);
    EXPECT_GE(v.capacity(), 4U);

    v.resize(3);
    EXPECT_EQ(v.size(), 3U);

    auto ints = v.get_component_span<int>();
    auto floats = v.get_component_span<1>();
    auto strings = v.get_component_span<std::string>();

    ints[0] = 10;
    ints[1] = 20;
    ints[2] = 30;
    floats[0] = 1.5f;
    floats[1] = 2.5f;
    floats[2] = 3.5f;
    strings[0] = "a"s;
    strings[1] = "b"s;
    strings[2] = "c"s;

    auto [ints2, floats2, strings2] = v.get_component_spans_tuple();
    EXPECT_EQ(ints2[1], 20);
    EXPECT_EQ(floats2[2], 3.5f);
    EXPECT_EQ(strings2[0], "a"s);

    // Verify row access.
    auto row1 = v[1];
    EXPECT_EQ(row1.get_id(), entity_id{101});
    EXPECT_EQ(row1.component<int>(), 20);

    v.clear();
    EXPECT_TRUE(v.empty());
  }

  // Type traits verification.
  if (true) {
    using lens_t = archetype_t::row_lens;
    using view_t = archetype_t::row_view;

    static_assert(lens_t::writeable_v == true);
    static_assert(view_t::writeable_v == false);
  }
}

// Test that components() returns references, not copies. Uses unique_ptr which
// is non-copyable, so this test will fail to compile if there's an accidental
// copy.
void ArchetypeVector_NoCopy() {
  enum class entity_id : std::uint32_t {};
  using archetype_t =
      archetype_vector<entity_id, std::tuple<int, std::unique_ptr<int>>>;

  // Mutable components() must return references.
  if (true) {
    archetype_t v;
    v.emplace_back(42, std::make_unique<int>(100));

    auto row = v[0];
    // Note: `auto` (not `auto&`) works here because components() returns a
    // tuple of references. Copying the tuple copies the references, not the
    // referents, so `i` and `ptr` are still references to the original data.
    auto [i, ptr] = row.components();

    // Verify we got references by modifying through them.
    i = 99;
    *ptr = 200;

    EXPECT_EQ(v.get_component_span<int>()[0], 99);
    EXPECT_EQ(*v.get_component_span<std::unique_ptr<int>>()[0], 200);
  }

  // Const components() must return const references.
  if (true) {
    archetype_t v;
    v.emplace_back(42, std::make_unique<int>(100));

    const auto row = v[0];
    auto [i, ptr] = row.components();

    // Verify we can read through the const references.
    EXPECT_EQ(i, 42);
    EXPECT_EQ(*ptr, 100);
  }

  // row_view components() must return const references.
  if (true) {
    archetype_t v;
    v.emplace_back(42, std::make_unique<int>(100));
    const archetype_t& cv = v;

    auto view = cv[0];
    auto [i, ptr] = view.components();

    EXPECT_EQ(i, 42);
    EXPECT_EQ(*ptr, 100);
  }
}

void StableId_Basic() {
  using V = int_stable_ids;
  using id_t = V::id_t;

  // Empty container.
  if (true) {
    V v;
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0U);
    EXPECT_EQ(v.find_max_extant_id(), id_t::invalid);
  }

  // push_back and emplace_back assign sequential IDs starting at 0.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(v.size(), 1U);
    EXPECT_FALSE(v.empty());
    auto id1 = v.push_back(20);
    auto id2 = v.emplace_back(30);
    EXPECT_EQ(*id1, 1U);
    EXPECT_EQ(*id2, 2U);
    EXPECT_EQ(v.size(), 3U);
    EXPECT_EQ(v[id0], 10);
    EXPECT_EQ(v[id1], 20);
    EXPECT_EQ(v[id2], 30);
  }

  // Mutable and const access via operator[] and at().
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    auto id1 = v.push_back(20);
    EXPECT_EQ(v[id0], 10);
    EXPECT_EQ(v.at(id0), 10);
    v[id0] = 42;
    EXPECT_EQ(v[id0], 42);
    v.at(id1) = 99;
    EXPECT_EQ(v.at(id1), 99);
    const V& cv = v;
    EXPECT_EQ(cv[id0], 42);
    EXPECT_EQ(cv.at(id1), 99);
  }

  // at(id) throws std::out_of_range for an invalid ID.
  if (true) {
    V v;
    (void)v.push_back(10);
    EXPECT_THROW(v.at(id_t{99}), std::out_of_range);
  }

  // Handles carry generation; get_handle and is_valid agree.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    auto id1 = v.push_back(20);
    auto h0 = v.get_handle(id0);
    auto h1 = v.get_handle(id1);
    EXPECT_TRUE(v.is_valid(h0));
    EXPECT_TRUE(v.is_valid(h1));
    EXPECT_EQ(h0.get_id(), id0);
    EXPECT_EQ(h1.get_id(), id1);
    EXPECT_EQ(h0.get_gen(), 0U);
    EXPECT_EQ(h1.get_gen(), 0U);
  }

  // push_back_handle and emplace_back_handle return handles.
  if (true) {
    V v;
    auto h0 = v.push_back_handle(10);
    auto h1 = v.emplace_back_handle(20);
    EXPECT_TRUE(v.is_valid(h0));
    EXPECT_TRUE(v.is_valid(h1));
    EXPECT_EQ(v.at(h0), 10);
    EXPECT_EQ(v.at(h1), 20);
  }

  // erase by ID.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    auto id1 = v.push_back(20);
    auto id2 = v.push_back(30);
    EXPECT_EQ(v.size(), 3U);
    EXPECT_TRUE(v.erase(id1));
    EXPECT_EQ(v.size(), 2U);
    EXPECT_FALSE(v.is_valid(id1));
    EXPECT_TRUE(v.is_valid(id0));
    EXPECT_TRUE(v.is_valid(id2));
    EXPECT_EQ(v[id0], 10);
    EXPECT_EQ(v[id2], 30);
  }

  // erase by handle.
  if (true) {
    V v;
    auto h0 = v.push_back_handle(10);
    auto h1 = v.push_back_handle(20);
    EXPECT_TRUE(v.erase(h0));
    EXPECT_FALSE(v.is_valid(h0));
    EXPECT_TRUE(v.is_valid(h1));
    EXPECT_EQ(v.size(), 1U);
  }

  // Erased handle is no longer valid; erase returns false for stale handle.
  if (true) {
    V v;
    auto h = v.push_back_handle(10);
    EXPECT_TRUE(v.is_valid(h));
    EXPECT_TRUE(v.erase(h));
    EXPECT_FALSE(v.is_valid(h));
    EXPECT_FALSE(v.erase(h));
  }

  // ID reuse: erased ID is reused by next insertion (LIFO order).
  if (true) {
    V v;
    (void)v.push_back(10);         // id 0
    auto id1 = v.push_back(20);    // id 1
    (void)v.push_back(30);         // id 2
    v.erase(id1);                  // id 1 is freed
    auto id_new = v.push_back(99); // should reuse id 1
    EXPECT_EQ(id_new, id1);
    EXPECT_EQ(v[id_new], 99);
  }

  // Old handle is invalidated even if ID is reused.
  if (true) {
    V v;
    auto h = v.push_back_handle(10);
    v.erase(h);
    (void)v.push_back(20); // reuse ID
    EXPECT_FALSE(v.is_valid(h));
    auto h_new = v.get_handle(h.get_id());
    EXPECT_TRUE(v.is_valid(h_new));
    EXPECT_NE(h.get_gen(), h_new.get_gen());
  }

  // erase_if removes matching elements.
  if (true) {
    V v;
    (void)v.push_back(5);
    (void)v.push_back(15);
    (void)v.push_back(25);
    (void)v.push_back(10);
    auto cnt = v.erase_if([](int x) { return x > 10; });
    EXPECT_EQ(cnt, 2U);
    EXPECT_EQ(v.size(), 2U);
    int sum = 0;
    for (auto x : v) sum += x;
    EXPECT_EQ(sum, 15); // 5 + 10
  }

  // Linear iteration via begin()/end().
  if (true) {
    V v;
    (void)v.push_back(1);
    (void)v.push_back(2);
    (void)v.push_back(3);
    int sum = 0;
    for (auto x : v) sum += x;
    EXPECT_EQ(sum, 6);
  }

  // span() gives mutable access.
  if (true) {
    V v;
    (void)v.push_back(10);
    (void)v.push_back(20);
    auto s = v.span();
    s[0] = 100;
    EXPECT_EQ(v[V::id_t{0}], 100);
  }

  // vector() gives const access.
  if (true) {
    V v;
    (void)v.push_back(10);
    const auto& vec = v.vector();
    EXPECT_EQ(vec.size(), 1U);
    EXPECT_EQ(vec[0], 10);
  }

  // clear() removes all elements; shrink=true also frees memory.
  if (true) {
    V v;
    (void)v.push_back(1);
    (void)v.push_back(2);
    v.clear();
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0U);
    // IDs may still be reused after clear without shrink.
    auto id = v.push_back(99);
    EXPECT_EQ(*id, 0U); // reuses freed id 0
  }

  // clear(true) fully resets.
  if (true) {
    V v;
    (void)v.push_back(1);
    v.clear(true);
    EXPECT_TRUE(v.empty());
    auto id = v.push_back(100);
    EXPECT_EQ(*id, 0U);
    EXPECT_EQ(v.get_handle(id).get_gen(), 0U);
  }

  // shrink_to_fit compacts the container.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    v.erase(V::id_t{2});
    v.shrink_to_fit();
    EXPECT_EQ(v.size(), 2U);
  }

  // reserve does not change size.
  if (true) {
    V v;
    v.reserve(100);
    EXPECT_TRUE(v.empty());
  }

  // swap exchanges containers.
  if (true) {
    V a, b;
    (void)a.push_back(1);
    (void)a.push_back(2);
    (void)b.push_back(100);
    swap(a, b);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_EQ(b.size(), 2U);
    EXPECT_EQ(a[V::id_t{0}], 100);
    EXPECT_EQ(b[V::id_t{0}], 1);
  }

  // Move constructor and assignment.
  if (true) {
    V v;
    (void)v.push_back(42);
    V w{std::move(v)};
    EXPECT_EQ(w.size(), 1U);
    EXPECT_EQ(w[V::id_t{0}], 42);
    V x;
    x = std::move(w);
    EXPECT_EQ(x.size(), 1U);
    EXPECT_EQ(x[V::id_t{0}], 42);
  }

  // next_id() returns the ID that will be allocated next.
  if (true) {
    V v;
    EXPECT_EQ(v.next_id(), V::id_t{0});
    (void)v.push_back(10);
    EXPECT_EQ(v.next_id(), V::id_t{1});
    (void)v.push_back(20);
    v.erase(V::id_t{0});
    // LIFO: freed ID 0 is at the tail front, so next reuse is ID 0.
    EXPECT_EQ(v.next_id(), V::id_t{0});
  }

  // max_id() is the high-water mark, not the highest extant ID.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    EXPECT_EQ(v.max_id(), V::id_t{2});
    v.erase(V::id_t{2});
    EXPECT_EQ(v.max_id(), V::id_t{2});
    EXPECT_EQ(v.find_max_extant_id(),
        V::id_t{1}); // contrast: live max dropped
    // Reinserting reuses id 2; high-water mark stays the same.
    (void)v.push_back(99);
    EXPECT_EQ(v.max_id(), V::id_t{2});
    // clear() without shrink keeps the index table intact.
    v.clear();
    EXPECT_EQ(v.max_id(), V::id_t{2});
    // clear(true) frees the index table; max_id resets.
    v.clear(true);
    EXPECT_EQ(v.max_id(), V::id_t::invalid);
  }

  // Allocator constructor produces a usable, empty container.
  if (true) {
    V v{std::allocator<int>{}};
    EXPECT_TRUE(v.empty());
    auto id = v.push_back(42);
    EXPECT_EQ(v[id], 42);
    EXPECT_EQ(v.size(), 1U);
  }
}

enum class small_id_t : std::uint8_t { invalid = 255 };

template<>
constexpr auto corvid::enums::registry::enum_spec_v<small_id_t> =
    corvid::enums::sequence::make_sequence_enum_spec<small_id_t, "">();

using int_stable_small_ids = stable_ids<int, small_id_t>;

using int_stable_ids_fifo =
    stable_ids<int, int_stable_ids::id_t, true, true, std::allocator<int>>;
using int_stable_ids_nogen =
    stable_ids<int, int_stable_ids::id_t, false, false, std::allocator<int>>;
using int_stable_ids_fifo_nogen =
    stable_ids<int, int_stable_ids::id_t, false, true, std::allocator<int>>;
using int_stable_small_ids_fifo =
    stable_ids<int, small_id_t, true, true, std::allocator<int>>;

void StableId_SmallId() {
  using V = int_stable_small_ids;
  using id_t = V::id_t; // small_id_t : uint8_t, invalid = 255

  // Fill to capacity: 255 elements occupy every ID from 0 to 254.
  if (true) {
    V v;
    for (int i = 0; i < 255; ++i) (void)v.push_back(i);
    EXPECT_EQ(v.size(), 255U);
    EXPECT_EQ(v[id_t{0}], 0);
    EXPECT_EQ(v[id_t{127}], 127);
    EXPECT_EQ(v[id_t{254}], 254);
  }

  // The 256th insertion exceeds the limit; container size is unchanged.
  if (true) {
    V v;
    for (int i = 0; i < 255; ++i) (void)v.push_back(i);
    EXPECT_EQ(v.size(), 255U);
    EXPECT_THROW(v.push_back(999), std::out_of_range);
    EXPECT_EQ(v.size(), 255U);
  }

  // Erasing one element opens exactly one reuse slot.  After that single
  // reuse the container is full again and the next insertion overflows.
  if (true) {
    V v;
    for (int i = 0; i < 255; ++i) (void)v.push_back(i);
    auto h100 = v.get_handle(id_t{100});

    v.erase(id_t{100});
    EXPECT_EQ(v.size(), 254U);
    EXPECT_FALSE(v.is_valid(id_t{100}));
    EXPECT_FALSE(v.is_valid(h100));

    // The freed ID 100 is the one that gets reused.
    auto id_reused = v.push_back(999);
    EXPECT_EQ(id_reused, id_t{100});
    EXPECT_EQ(v[id_reused], 999);
    EXPECT_EQ(v.size(), 255U);
    // The old handle is still invalid even though the ID is live again.
    EXPECT_FALSE(v.is_valid(h100));
    auto h100_new = v.get_handle(id_reused);
    EXPECT_TRUE(v.is_valid(h100_new));
    EXPECT_GT(h100_new.get_gen(), h100.get_gen());

    // Full again — exceeds limit.
    EXPECT_THROW(v.push_back(0), std::out_of_range);
  }
}

void StableId_NoThrow() {
  using V = int_stable_small_ids;
  using id_t = V::id_t; // small_id_t : uint8_t, invalid = 255

  // Default is to throw.
  if (true) {
    V v;
    EXPECT_TRUE(v.throw_on_insert_failure());
  }

  // Accessor round-trips.
  if (true) {
    V v;
    v.throw_on_insert_failure(false);
    EXPECT_FALSE(v.throw_on_insert_failure());
    v.throw_on_insert_failure(true);
    EXPECT_TRUE(v.throw_on_insert_failure());
  }

  // push_back returns invalid on overflow instead of throwing.
  if (true) {
    V v;
    v.throw_on_insert_failure(false);
    for (int i = 0; i < 255; ++i) (void)v.push_back(i);
    EXPECT_EQ(v.size(), 255U);

    auto id = v.push_back(999);
    EXPECT_EQ(id, id_t::invalid);
    EXPECT_EQ(v.size(), 255U);
  }

  // emplace_back returns invalid on overflow instead of throwing.
  if (true) {
    V v;
    v.throw_on_insert_failure(false);
    for (int i = 0; i < 255; ++i) (void)v.emplace_back(i);
    EXPECT_EQ(v.size(), 255U);

    auto id = v.emplace_back(999);
    EXPECT_EQ(id, id_t::invalid);
    EXPECT_EQ(v.size(), 255U);
  }

  // Re-enabling the flag restores throwing on overflow.
  if (true) {
    V v;
    v.throw_on_insert_failure(false);
    for (int i = 0; i < 255; ++i) (void)v.push_back(i);
    EXPECT_EQ(v.push_back(999), id_t::invalid);

    v.throw_on_insert_failure(true);
    EXPECT_THROW(v.push_back(999), std::out_of_range);
    EXPECT_EQ(v.size(), 255U);
  }

  // Free-list reuse works normally with the flag off; only exceeding the limit
  // returns invalid.
  if (true) {
    V v;
    v.throw_on_insert_failure(false);
    for (int i = 0; i < 255; ++i) (void)v.push_back(i);

    v.erase(id_t{50});
    EXPECT_EQ(v.size(), 254U);

    auto id = v.push_back(888);
    EXPECT_EQ(id, id_t{50});
    EXPECT_EQ(v[id], 888);
    EXPECT_EQ(v.size(), 255U);

    // Now truly full — returns invalid, does not throw.
    EXPECT_EQ(v.push_back(999), id_t::invalid);
    EXPECT_EQ(v.size(), 255U);
  }
}

void StableId_Fifo() {
  using V = int_stable_ids_fifo;
  using id_t = V::id_t;

  // Freed IDs are reused oldest-first (FIFO), not most-recent-first (LIFO).
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    v.erase(id_t{0});      // free list: [0]
    v.erase(id_t{1});      // free list: [0, 1]
    // LIFO would give 1 then 0; FIFO gives 0 then 1.
    EXPECT_EQ(v.push_back(100), id_t{0});
    EXPECT_EQ(v.push_back(200), id_t{1});
  }

  // FIFO reuse order matches erase order, not ID order.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    (void)v.push_back(40); // id 3
    (void)v.push_back(50); // id 4
    v.erase(id_t{2});
    v.erase(id_t{0});
    v.erase(id_t{3});
    // Erase order was 2, 0, 3; reuse must follow that order.
    EXPECT_EQ(v.push_back(100), id_t{2});
    EXPECT_EQ(v.push_back(200), id_t{0});
    EXPECT_EQ(v.push_back(300), id_t{3});
  }

  // Interleaved free and alloc: each alloc pops the oldest free.
  if (true) {
    V v;
    (void)v.push_back(10);      // id 0
    (void)v.push_back(20);      // id 1
    (void)v.push_back(30);      // id 2
    v.erase(id_t{0});           // free: [0]
    v.erase(id_t{1});           // free: [0, 1]
    auto r0 = v.push_back(100); // pops 0; free: [1]
    EXPECT_EQ(r0, id_t{0});
    EXPECT_EQ(v[r0], 100);
    v.erase(id_t{2});           // free: [1, 2]
    auto r1 = v.push_back(200); // pops 1; free: [2]
    EXPECT_EQ(r1, id_t{1});
    EXPECT_EQ(v[r1], 200);
    auto r2 = v.push_back(300); // pops 2; free: []
    EXPECT_EQ(r2, id_t{2});
    EXPECT_EQ(v[r2], 300);
    // All live; next insert gets a fresh ID.
    EXPECT_EQ(v.push_back(400), id_t{3});
  }

  // next_id returns 0 on empty, the FIFO head when IDs are free, or the
  // next sequential value when the free list is empty.
  if (true) {
    V v;
    EXPECT_EQ(v.next_id(), id_t{0});
    (void)v.push_back(10);           // id 0
    (void)v.push_back(20);           // id 1
    (void)v.push_back(30);           // id 2
    EXPECT_EQ(v.next_id(), id_t{3}); // no free IDs
    v.erase(id_t{1});
    EXPECT_EQ(v.next_id(), id_t{1}); // head is 1
    v.erase(id_t{0});
    EXPECT_EQ(v.next_id(), id_t{1}); // head is still 1 (oldest freed)
  }

  // Handles are invalidated on FIFO reuse; gen is bumped on erase.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    (void)v.push_back(20);
    auto h0 = v.get_handle(id0);
    EXPECT_EQ(h0.get_gen(), 0U);
    v.erase(id0);
    EXPECT_FALSE(v.is_valid(h0));
    auto id0_reused = v.push_back(99);
    EXPECT_EQ(id0_reused, id0);
    EXPECT_FALSE(v.is_valid(h0)); // stale handle stays invalid
    auto h0_new = v.get_handle(id0_reused);
    EXPECT_TRUE(v.is_valid(h0_new));
    EXPECT_GT(h0_new.get_gen(), h0.get_gen());
  }

  // push_back_handle returns a correct handle after FIFO reuse.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    v.erase(id_t{0});
    auto h = v.push_back_handle(99);
    EXPECT_EQ(h.get_id(), id_t{0});
    EXPECT_TRUE(v.is_valid(h));
    EXPECT_EQ(v.at(h), 99);
    EXPECT_EQ(h.get_gen(), 1U); // bumped once on erase
  }

  // Single free is a degenerate FIFO list of length one.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    v.erase(id_t{1});
    auto r = v.push_back(99);
    EXPECT_EQ(r, id_t{1});
    EXPECT_EQ(v[r], 99);
    EXPECT_EQ(v[id_t{0}], 10);
    EXPECT_EQ(v[id_t{2}], 30);
  }

  // Free all elements; reuse order matches erase order.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    v.erase(id_t{2});
    v.erase(id_t{1});
    v.erase(id_t{0});
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.push_back(100), id_t{2});
    EXPECT_EQ(v.push_back(200), id_t{1});
    EXPECT_EQ(v.push_back(300), id_t{0});
  }

  // erase_if frees matching elements; subsequent allocs reuse them in
  // the order erase_if processed them (data-index scan, swap-and-pop).
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(25); // id 1
    (void)v.push_back(30); // id 2
    (void)v.push_back(5);  // id 3
    (void)v.push_back(15); // id 4
    auto cnt = v.erase_if([](int x) { return x > 20; });
    EXPECT_EQ(cnt, 2U);
    EXPECT_EQ(v.size(), 3U);
    int sum{};
    for (auto val : v) sum += val;
    EXPECT_EQ(sum, 30); // 10 + 5 + 15
    // erase_if hits id 1 (val 25) first at data-index 1, then id 2
    // (val 30) at data-index 2 after the swap brings it into range.
    // FIFO reuses them in that order.
    EXPECT_EQ(v.push_back(100), id_t{1});
    EXPECT_EQ(v.push_back(200), id_t{2});
    EXPECT_EQ(v.size(), 5U);
  }

  // clear() without shrink rebuilds the FIFO list in position order;
  // with no prior swaps that matches ID order 0, 1, 2.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    auto h0 = v.get_handle(id_t{0});
    v.clear();
    EXPECT_TRUE(v.empty());
    EXPECT_FALSE(v.is_valid(h0));
    EXPECT_EQ(v.push_back(100), id_t{0});
    EXPECT_EQ(v.push_back(200), id_t{1});
    EXPECT_EQ(v.push_back(300), id_t{2});
    EXPECT_EQ(v.get_handle(id_t{0}).get_gen(), 1U); // bumped once by clear
  }

  // clear(true) frees all storage; next insert starts fresh.
  if (true) {
    V v;
    (void)v.push_back(10);
    (void)v.push_back(20);
    v.erase(id_t{0});
    v.clear(true);
    EXPECT_TRUE(v.empty());
    auto id = v.push_back(42);
    EXPECT_EQ(*id, 0U);
    EXPECT_EQ(v.get_handle(id).get_gen(), 0U);
  }

  // shrink_to_fit rebuilds the FIFO list; only free IDs below the new
  // table size survive.  IDs beyond max-live are discarded entirely.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    (void)v.push_back(40); // id 3
    (void)v.push_back(50); // id 4
    v.erase(id_t{3});
    v.erase(id_t{4});
    v.erase(id_t{0});
    // Live: ids 1, 2.  shrink trims to max(1,2)+1 = 3; only id 0 is a
    // free slot that fits.  Ids 3 and 4 are beyond the new table size.
    v.shrink_to_fit();
    EXPECT_EQ(v.size(), 2U);
    EXPECT_EQ(v[id_t{1}], 20);
    EXPECT_EQ(v[id_t{2}], 30);
    auto id_new = v.push_back(99);
    EXPECT_EQ(id_new, id_t{0});
    EXPECT_EQ(v[id_new], 99);
  }

  // swap exchanges the complete FIFO free-list state between containers.
  if (true) {
    V a, b;
    (void)a.push_back(10); // a: id 0
    (void)a.push_back(20); // a: id 1
    a.erase(id_t{0});      // a free list: [0]
    (void)b.push_back(30); // b: id 0
    (void)b.push_back(40); // b: id 1
    (void)b.push_back(50); // b: id 2
    b.erase(id_t{1});      // b free list: [1]
    b.erase(id_t{0});      // b free list: [1, 0]
    swap(a, b);
    // a now has b's old free list [1, 0]; oldest free is 1.
    EXPECT_EQ(a.push_back(100), id_t{1});
    EXPECT_EQ(a.push_back(200), id_t{0});
    // b now has a's old free list [0].
    EXPECT_EQ(b.push_back(300), id_t{0});
  }

  // Move construction transfers the FIFO free-list intact.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    v.erase(id_t{0});      // free: [0]
    v.erase(id_t{2});      // free: [0, 2]
    V w{std::move(v)};
    EXPECT_EQ(w.push_back(100), id_t{0});
    EXPECT_EQ(w.push_back(200), id_t{2});
  }

  // FIFO reuse at small_id_t capacity limit.
  if (true) {
    using SV = int_stable_small_ids_fifo;
    using sid_t = SV::id_t;
    SV v;
    for (int i = 0; i < 255; ++i) (void)v.push_back(i);
    EXPECT_EQ(v.size(), 255U);
    v.erase(sid_t{10});
    v.erase(sid_t{20});
    v.erase(sid_t{30});
    // FIFO order matches erase order: 10, 20, 30.
    EXPECT_EQ(v.push_back(100), sid_t{10});
    EXPECT_EQ(v.push_back(200), sid_t{20});
    EXPECT_EQ(v.push_back(300), sid_t{30});
    EXPECT_EQ(v.size(), 255U);
  }
}

void StableId_NoGen() {
  using V = int_stable_ids_nogen;
  using id_t = V::id_t;

  // Basic push, access, and size without generation tracking.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    auto id1 = v.push_back(20);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(*id1, 1U);
    EXPECT_EQ(v[id0], 10);
    EXPECT_EQ(v[id1], 20);
    EXPECT_EQ(v.size(), 2U);
  }

  // handle_t is exactly sizeof(id_t): the gen field is zero-size via
  // [[no_unique_address]].  Smaller than the default (gen-enabled) handle.
  if (true) {
    static_assert(sizeof(V::handle_t) == sizeof(V::id_t));
    using WithGen = int_stable_ids;
    static_assert(sizeof(V::handle_t) < sizeof(WithGen::handle_t));
  }

  // is_valid detects free IDs; erase makes them invalid.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    (void)v.push_back(20);
    EXPECT_TRUE(v.is_valid(id0));
    v.erase(id0);
    EXPECT_FALSE(v.is_valid(id0));
  }

  // A handle for a free (not-yet-reused) ID is detected as invalid.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    (void)v.push_back(20);
    auto h0 = v.get_handle(id0);
    v.erase(id0);
    EXPECT_FALSE(v.is_valid(h0));
    EXPECT_THROW(v.at(h0), std::invalid_argument);
  }

  // Without gen, a stale handle for a *reused* ID is indistinguishable
  // from a fresh one: is_valid returns true, at() returns the new value.
  // This is the documented trade-off of UseGen=false.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    (void)v.push_back(20);
    auto h0 = v.get_handle(id0); // snapshot while id 0 holds 10
    v.erase(id0);
    (void)v.push_back(99);       // reuses id 0 (LIFO)
    EXPECT_TRUE(v.is_valid(h0)); // indistinguishable: ID is live
    EXPECT_EQ(v.at(h0), 99);     // returns new value, not original 10
  }

  // LIFO reuse: most recently freed ID is reused first.  Contrast with
  // the FIFO variant where the same erase order would yield 0 then 1.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    v.erase(id_t{0});      // freed first
    v.erase(id_t{1});      // freed second (most recent)
    // LIFO: id 1 freed last, so it's reused first.
    EXPECT_EQ(v.push_back(100), id_t{1});
    EXPECT_EQ(v.push_back(200), id_t{0});
  }

  // Erase-reinsert cycle: values and IDs stay consistent.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    v.erase(id_t{1});
    auto r = v.push_back(99);
    EXPECT_EQ(r, id_t{1});
    EXPECT_EQ(v[r], 99);
    EXPECT_EQ(v[id_t{0}], 10);
    EXPECT_EQ(v[id_t{2}], 30);
    EXPECT_EQ(v.size(), 3U);
  }

  // clear() without shrink: all IDs become reusable; no gen to bump.
  if (true) {
    V v;
    (void)v.push_back(10);
    (void)v.push_back(20);
    v.clear();
    EXPECT_TRUE(v.empty());
    auto id0 = v.push_back(100);
    auto id1 = v.push_back(200);
    EXPECT_EQ(id0, id_t{0});
    EXPECT_EQ(id1, id_t{1});
    EXPECT_EQ(v[id0], 100);
    EXPECT_EQ(v[id1], 200);
  }

  // clear(true) resets the container entirely.
  if (true) {
    V v;
    (void)v.push_back(10);
    v.clear(true);
    EXPECT_TRUE(v.empty());
    auto id = v.push_back(42);
    EXPECT_EQ(*id, 0U);
    EXPECT_EQ(v[id], 42);
  }
}

void StableId_FifoNoGen() {
  using V = int_stable_ids_fifo_nogen;
  using id_t = V::id_t;

  // FIFO reuse order is maintained without generation tracking.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    v.erase(id_t{0});      // free: [0]
    v.erase(id_t{2});      // free: [0, 2]
    EXPECT_EQ(v.push_back(100), id_t{0});
    EXPECT_EQ(v.push_back(200), id_t{2});
  }

  // Interleaved free and alloc follow FIFO order without gen.
  if (true) {
    V v;
    (void)v.push_back(10);      // id 0
    (void)v.push_back(20);      // id 1
    (void)v.push_back(30);      // id 2
    (void)v.push_back(40);      // id 3
    v.erase(id_t{1});           // free: [1]
    v.erase(id_t{3});           // free: [1, 3]
    auto r0 = v.push_back(100); // pops 1
    EXPECT_EQ(r0, id_t{1});
    v.erase(id_t{0});           // free: [3, 0]
    auto r1 = v.push_back(200); // pops 3
    EXPECT_EQ(r1, id_t{3});
    auto r2 = v.push_back(300); // pops 0
    EXPECT_EQ(r2, id_t{0});
  }

  // Values are correct after FIFO reuse.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    (void)v.push_back(30); // id 2
    v.erase(id_t{1});
    auto r = v.push_back(99);
    EXPECT_EQ(r, id_t{1});
    EXPECT_EQ(v[r], 99);
    EXPECT_EQ(v[id_t{0}], 10);
    EXPECT_EQ(v[id_t{2}], 30);
  }

  // handle_t is sizeof(id_t): neither gen nor the FIFO next-pointer
  // appears in it.  The next-pointer lives in the internal slot_t only.
  if (true) {
    static_assert(sizeof(V::handle_t) == sizeof(V::id_t));
  }

  // Without gen, a stale handle for a reused ID is indistinguishable.
  // FIFO increases the reuse delay but is not a correctness guard.
  if (true) {
    V v;
    auto id0 = v.push_back(10);
    (void)v.push_back(20);
    (void)v.push_back(30);
    auto h0 = v.get_handle(id0);
    v.erase(id0);
    v.erase(id_t{1}); // id 0 is oldest; next alloc reuses it
    (void)v.push_back(99);
    EXPECT_TRUE(v.is_valid(h0)); // indistinguishable: ID is live again
    EXPECT_EQ(v.at(h0), 99);
  }

  // clear() without shrink rebuilds the FIFO list in position order.
  if (true) {
    V v;
    (void)v.push_back(10);
    (void)v.push_back(20);
    (void)v.push_back(30);
    v.clear();
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.push_back(100), id_t{0});
    EXPECT_EQ(v.push_back(200), id_t{1});
    EXPECT_EQ(v.push_back(300), id_t{2});
  }
}

// Test the max_id() setting to limit ID allocation.
void StableId_MaxId() {
  using id_t = int_stable_ids::id_t;
  using V = stable_ids<int, id_t>;

  // Can allocate up to max.
  if (true) {
    // Limit to 3 IDs (0, 1, 2).
    V v{id_t{3}};
    auto id0 = v.push_back(10);
    auto id1 = v.push_back(20);
    auto id2 = v.push_back(30);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(*id1, 1U);
    EXPECT_EQ(*id2, 2U);
    EXPECT_EQ(v.size(), 3U);
  }

  // The 4th insertion overflows.
  if (true) {
    V v{id_t{3}};
    (void)v.push_back(10);
    (void)v.push_back(20);
    (void)v.push_back(30);
    EXPECT_THROW(v.push_back(40), std::out_of_range);
    EXPECT_EQ(v.size(), 3U);
  }

  // With throw disabled, returns invalid.
  if (true) {
    V v{id_t{3}};
    v.throw_on_insert_failure(false);
    (void)v.push_back(10);
    (void)v.push_back(20);
    (void)v.push_back(30);
    auto id3 = v.push_back(40);
    EXPECT_EQ(id3, id_t::invalid);
    EXPECT_EQ(v.size(), 3U);
  }

  // Erasing frees a slot for reuse.
  if (true) {
    V v{id_t{3}};
    auto id0 = v.push_back(10);
    (void)v.push_back(20);
    (void)v.push_back(30);
    EXPECT_EQ(v.size(), 3U);

    v.erase(id0);
    EXPECT_EQ(v.size(), 2U);

    // Can now insert again, reusing the freed ID.
    auto id_reused = v.push_back(40);
    EXPECT_EQ(id_reused, id0);
    EXPECT_EQ(v.size(), 3U);
    EXPECT_EQ(v[id_reused], 40);

    // Full again — exceeds limit.
    EXPECT_THROW(v.push_back(50), std::out_of_range);
  }

  // set_id_limit on empty container always succeeds.
  if (true) {
    V v;
    EXPECT_EQ(v.id_limit(), id_t::invalid);
    EXPECT_TRUE(v.set_id_limit(id_t{2}));
    EXPECT_EQ(v.id_limit(), id_t{2});

    (void)v.push_back(10);
    (void)v.push_back(20);
    EXPECT_THROW(v.push_back(30), std::out_of_range);
  }

  // set_id_limit fails if it would invalidate live IDs.
  if (true) {
    V v;
    (void)v.push_back(10); // ID 0
    (void)v.push_back(20); // ID 1
    (void)v.push_back(30); // ID 2

    // Can't set limit to 2 because ID 2 is live (limit means IDs 0..limit-1).
    EXPECT_FALSE(v.set_id_limit(id_t{2}));
    EXPECT_EQ(v.id_limit(), id_t::invalid); // Unchanged.

    // Can set limit to 3 (IDs 0,1,2 are valid).
    EXPECT_TRUE(v.set_id_limit(id_t{3}));
    EXPECT_EQ(v.id_limit(), id_t{3});

    // Can raise the limit.
    EXPECT_TRUE(v.set_id_limit(id_t{10}));
    EXPECT_EQ(v.id_limit(), id_t{10});
  }

  // set_id_limit with freed slots beyond the new limit triggers shrink.
  if (true) {
    V v;
    (void)v.push_back(10); // ID 0
    (void)v.push_back(20); // ID 1
    (void)v.push_back(30); // ID 2
    v.erase(id_t{2});      // Free ID 2, max_id() still 2.

    EXPECT_EQ(v.max_id(), id_t{2});
    EXPECT_EQ(v.find_max_extant_id(), id_t{1});

    // Setting limit to 2 should succeed and shrink (ID 2 is freed).
    EXPECT_TRUE(v.set_id_limit(id_t{2}));
    EXPECT_EQ(v.id_limit(), id_t{2});
    // After shrink, max_id() should equal find_max_extant_id().
    EXPECT_EQ(v.max_id(), id_t{1});
  }

  // set_id_limit on empty container with freed slots clears them.
  if (true) {
    V v;
    (void)v.push_back(10); // ID 0
    (void)v.push_back(20); // ID 1
    v.erase(id_t{0});
    v.erase(id_t{1});
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.max_id(), id_t{1}); // High-water mark is still 1.

    // Setting a lower limit should clear the freed slots.
    EXPECT_TRUE(v.set_id_limit(id_t{1}));
    EXPECT_EQ(v.id_limit(), id_t{1});
  }

  // Prefill constructor pre-allocates slots.
  if (true) {
    V v{id_t{5}, true};
    EXPECT_EQ(v.id_limit(), id_t{5});
    // Slots are pre-allocated, so push_back won't allocate indexes_/reverse_.
    auto id0 = v.push_back(10);
    auto id1 = v.push_back(20);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(*id1, 1U);
  }
}

MAKE_TEST_LIST(ArchetypeVector_Basic, ArchetypeVector_NoCopy, StableId_Basic,
    StableId_SmallId, StableId_NoThrow, StableId_Fifo, StableId_NoGen,
    StableId_FifoNoGen, StableId_MaxId);

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
