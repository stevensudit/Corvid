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
  using id_enums::entity_id_t;
  using archetype_t =
      archetype_vector<entity_id_t, std::tuple<int, float, std::string>>;

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
      return entity_id_t{static_cast<std::uint32_t>(index + 100)};
    });
    EXPECT_EQ(v.index_to_id(0), entity_id_t{100});
    EXPECT_EQ(v.index_to_id(5), entity_id_t{105});
    EXPECT_EQ(v.index_to_id(99), entity_id_t{199});
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
      return entity_id_t{static_cast<std::uint32_t>(index * 10)};
    });
    v.emplace_back(1, 1.0f, "one"s);
    v.emplace_back(2, 2.0f, "two"s);

    auto row = v[0];
    EXPECT_EQ(row.index(), 0U);
    EXPECT_EQ(row.id(), entity_id_t{0});

    auto row1 = v[1];
    EXPECT_EQ(row1.index(), 1U);
    EXPECT_EQ(row1.id(), entity_id_t{10});
  }

  // operator[] const - row_view.
  if (true) {
    archetype_t v;
    v.set_index_to_id([](archetype_t::size_type index) {
      return entity_id_t{static_cast<std::uint32_t>(index + 50)};
    });
    v.emplace_back(99, 9.9f, "ninety-nine"s);
    const archetype_t& cv = v;

    auto row = cv[0];
    EXPECT_EQ(row.index(), 0U);
    EXPECT_EQ(row.id(), entity_id_t{50});
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
    v1.set_index_to_id([](archetype_t::size_type) {
      return entity_id_t{100};
    });

    archetype_t v2;
    v2.emplace_back(20, 2.0f, "v2-a"s);
    v2.emplace_back(30, 3.0f, "v2-b"s);
    v2.set_index_to_id([](archetype_t::size_type) {
      return entity_id_t{200};
    });

    v1.swap(v2);

    EXPECT_EQ(v1.size(), 2U);
    EXPECT_EQ(v2.size(), 1U);
    EXPECT_EQ(v1.get_component_span<int>()[0], 20);
    EXPECT_EQ(v1.get_component_span<int>()[1], 30);
    EXPECT_EQ(v2.get_component_span<int>()[0], 10);
    EXPECT_EQ(v1.index_to_id(0), entity_id_t{200});
    EXPECT_EQ(v2.index_to_id(0), entity_id_t{100});
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
      return entity_id_t{ndx + 1000};
    });

    archetype_t v2{std::move(v1)};

    EXPECT_EQ(v2.size(), 1U);
    EXPECT_EQ(v2.get_component_span<int>()[0], 42);
    EXPECT_EQ(v2.get_component_span<float>()[0], 4.2f);
    EXPECT_EQ(v2.get_component_span<std::string>()[0], "move-me"s);
    EXPECT_EQ(v2.index_to_id(5), entity_id_t{1005});
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
  using id_enums::entity_id_t;
  using archetype_t =
      archetype_vector<entity_id_t, std::tuple<int, std::unique_ptr<int>>>;

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
    EXPECT_EQ(h0.id(), id0);
    EXPECT_EQ(h1.id(), id1);
    EXPECT_EQ(h0.gen(), 0U);
    EXPECT_EQ(h1.gen(), 0U);
  }

  // push_back_handle and emplace_back_handle return handles.
  if (true) {
    V v;
    auto h0 = v.push_back_handle(10);
    auto h1 = v.emplace_back_handle(20);
    EXPECT_TRUE(v.is_valid(h0));
    EXPECT_TRUE(v.is_valid(h1));
    EXPECT_EQ(v.at(h0), 10);
    const V& cv = v;
    EXPECT_EQ(cv.at(h1), 20);
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
    EXPECT_THROW(v.at(h), std::invalid_argument);
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
    auto h_new = v.get_handle(h.id());
    EXPECT_TRUE(v.is_valid(h_new));
    EXPECT_NE(h.gen(), h_new.gen());
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
    EXPECT_EQ(v.get_handle(id).gen(), 0U);
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
    EXPECT_GT(h100_new.gen(), h100.gen());

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
    EXPECT_EQ(h0.gen(), 0U);
    v.erase(id0);
    EXPECT_FALSE(v.is_valid(h0));
    auto id0_reused = v.push_back(99);
    EXPECT_EQ(id0_reused, id0);
    EXPECT_FALSE(v.is_valid(h0)); // stale handle stays invalid
    auto h0_new = v.get_handle(id0_reused);
    EXPECT_TRUE(v.is_valid(h0_new));
    EXPECT_GT(h0_new.gen(), h0.gen());
  }

  // push_back_handle returns a correct handle after FIFO reuse.
  if (true) {
    V v;
    (void)v.push_back(10); // id 0
    (void)v.push_back(20); // id 1
    v.erase(id_t{0});
    auto h = v.push_back_handle(99);
    EXPECT_EQ(h.id(), id_t{0});
    EXPECT_TRUE(v.is_valid(h));
    EXPECT_EQ(h.gen(), 1U); // bumped once on erase
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
    EXPECT_EQ(v.get_handle(id_t{0}).gen(), 1U); // bumped once by clear
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
    EXPECT_EQ(v.get_handle(id).gen(), 0U);
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

void EntityRegistry_Basic() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};
  const loc_t loc1{store_id_t{0}, 1};
  const loc_t loc2{store_id_t{0}, 2};
  const loc_t loc_s1{store_id_t{1}, 0};

  // Default template parameters.
  if (true) {
    static_assert(std::is_same_v<reg_t::id_t, entity_id_t>);
    static_assert(std::is_same_v<reg_t::store_id_t, store_id_t>);
    static_assert(std::is_same_v<reg_t::size_type, size_t>);
    static_assert(std::is_same_v<reg_t::allocator_type, std::allocator<int>>);
  }

  // handle_t default construction.
  if (true) {
    using handle_t = reg_t::handle_t;
    handle_t h;
    EXPECT_EQ(h.id(), entity_id_t::invalid);
    EXPECT_EQ(h.gen(), *entity_id_t::invalid);
  }

  // handle_t copy construction and assignment.
  if (true) {
    using handle_t = reg_t::handle_t;
    handle_t h1;
    handle_t h2{h1};
    EXPECT_TRUE(h1 == h2);
    handle_t h3;
    h3 = h1;
    EXPECT_TRUE(h1 == h3);
  }

  // handle_t comparison operators.
  if (true) {
    using handle_t = reg_t::handle_t;
    handle_t h1;
    handle_t h2;
    EXPECT_TRUE(h1 == h2);
    EXPECT_FALSE(h1 != h2);
    EXPECT_FALSE(h1 < h2);
    EXPECT_TRUE(h1 <= h2);
    EXPECT_FALSE(h1 > h2);
    EXPECT_TRUE(h1 >= h2);
  }

  // Default construction: empty registry.
  if (true) {
    reg_t r;
    EXPECT_EQ(r.id_limit(), id_t::invalid);
    EXPECT_FALSE(r.is_valid(id_t{0}));
  }

  // Allocator constructor.
  if (true) {
    reg_t r{std::allocator<int>{}};
    EXPECT_FALSE(r.is_valid(id_t{0}));
  }

  // create returns sequential IDs.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    auto id1 = r.create_id(loc1, 20);
    auto id2 = r.create_id(loc2, 30);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(*id1, 1U);
    EXPECT_EQ(*id2, 2U);
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_TRUE(r.is_valid(id2));
  }

  // create with invalid store_id fails.
  if (true) {
    reg_t r;
    auto id = r.create_id(loc_t{});
    EXPECT_EQ(id, id_t::invalid);
  }

  // create stores metadata.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 42);
    auto id1 = r.create_id(loc1, 99);
    EXPECT_EQ(r[id0], 42);
    EXPECT_EQ(r[id1], 99);
  }

  // Mutable metadata access via operator[].
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    r[id0] = 777;
    EXPECT_EQ(r[id0], 777);
  }

  // Const metadata access via operator[].
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 42);
    const auto& cr = r;
    EXPECT_EQ(cr[id0], 42);
  }

  // create stores location.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc_s1, 10);
    const auto& loc = r.get_location(id0);
    EXPECT_EQ(*loc.store_id, 1U);
    EXPECT_EQ(loc.ndx, 0U);
  }

  // set_location by ID.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    r.set_location(id0, loc_s1);
    const auto& loc = r.get_location(id0);
    EXPECT_EQ(*loc.store_id, 1U);
    EXPECT_EQ(loc.ndx, 0U);
  }

  // set_location to invalid erases the entity.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    EXPECT_TRUE(r.is_valid(id0));
    r.set_location(id0, loc_t{});
    EXPECT_FALSE(r.is_valid(id0));
  }

  // erase by ID.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    auto id1 = r.create_id(loc1, 20);
    EXPECT_TRUE(r.erase(id0));
    EXPECT_FALSE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    // Double erase fails.
    EXPECT_FALSE(r.erase(id0));
  }

  // erase invalid ID returns false.
  if (true) {
    reg_t r;
    EXPECT_FALSE(r.erase(id_t{0}));
    EXPECT_FALSE(r.erase(id_t::invalid));
  }

  // erase_if removes matching entities.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 5);
    (void)r.create_id(loc1, 15);
    (void)r.create_id(loc2, 25);
    auto cnt = r.erase_if([](auto& rec) { return rec.metadata > 10; });
    EXPECT_EQ(cnt, 2U);
    EXPECT_TRUE(r.is_valid(id_t{0}));
    EXPECT_FALSE(r.is_valid(id_t{1}));
    EXPECT_FALSE(r.is_valid(id_t{2}));
  }

  // at(id_t) success.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 42);
    EXPECT_EQ(r.at(id0), 42);
    r.at(id0) = 99;
    EXPECT_EQ(r.at(id0), 99);
  }

  // at(id_t) const.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 42);
    const auto& cr = r;
    EXPECT_EQ(cr.at(id0), 42);
  }

  // at(id_t) throws for invalid ID.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10);
    EXPECT_THROW(r.at(id_t{99}), std::out_of_range);
  }

  // at(id_t) throws for erased ID.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    r.erase(id0);
    EXPECT_THROW(r.at(id0), std::out_of_range);
  }

  // size() tracks living entities.
  if (true) {
    reg_t r;
    EXPECT_EQ(r.size(), 0U);
    auto id0 = r.create_id(loc0, 10);
    EXPECT_EQ(r.size(), 1U);
    (void)r.create_id(loc1, 20);
    EXPECT_EQ(r.size(), 2U);
    r.erase(id0);
    EXPECT_EQ(r.size(), 1U);
  }

  // max_id() is the high-water mark.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc1, 20); // id 1
    (void)r.create_id(loc2, 30); // id 2
    EXPECT_EQ(r.max_id(), id_t{2});
    r.erase(id_t{2});
    EXPECT_EQ(r.max_id(), id_t{2}); // still 2, high-water mark
  }
}

void EntityRegistry_Handle() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};
  const loc_t loc1{store_id_t{0}, 1};

  // create_with_handle returns a valid handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 42);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(h.id(), id_t{0});
    EXPECT_EQ(h.gen(), 0U);
  }

  // create_with_handle with invalid store_id returns invalid handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc_t{}, 42);
    EXPECT_FALSE(r.is_valid(h));
    EXPECT_EQ(h.id(), id_t::invalid);
  }

  // get_handle for valid ID.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    auto h = r.get_handle(id0);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(h.id(), id0);
    EXPECT_EQ(h.gen(), 0U);
  }

  // get_handle for invalid ID returns invalid handle.
  if (true) {
    reg_t r;
    auto h = r.get_handle(id_t{99});
    EXPECT_FALSE(r.is_valid(h));
    EXPECT_EQ(h.id(), id_t::invalid);
  }

  // Handle is invalidated after erase.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    auto h = r.get_handle(id0);
    r.erase(id0);
    EXPECT_FALSE(r.is_valid(h));
  }

  // Stale handle is invalid even after ID reuse (gen mismatch).
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    auto h_old = r.get_handle(id0);
    EXPECT_EQ(h_old.gen(), 0U);
    r.erase(id0);
    auto id0_reused = r.create_id(loc0, 99);
    EXPECT_EQ(id0_reused, id0);
    EXPECT_FALSE(r.is_valid(h_old));
    auto h_new = r.get_handle(id0_reused);
    EXPECT_TRUE(r.is_valid(h_new));
    EXPECT_GT(h_new.gen(), h_old.gen());
  }

  // erase by handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    EXPECT_TRUE(r.erase(h));
    EXPECT_FALSE(r.is_valid(h));
    // Double erase by handle fails.
    EXPECT_FALSE(r.erase(h));
  }

  // Metadata access by handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 42);
    EXPECT_EQ(r.at(h), 42);
    r.at(h) = 100;
    EXPECT_EQ(r.at(h), 100);
    const auto& cr = r;
    EXPECT_EQ(cr.at(h), 100);
  }

  // Metadata access by invalid handle throws.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 42);
    r.erase(h);
    EXPECT_THROW(r.at(h), std::invalid_argument);
  }

  // get_location by handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    const auto& loc = r.get_location(h);
    EXPECT_EQ(*loc.store_id, 0U);
    EXPECT_EQ(loc.ndx, 0U);
  }

  // set_location by handle.
  if (true) {
    reg_t r;
    loc_t loc_new{store_id_t{2}, 5};
    auto h = r.create_handle(loc0, 10);
    r.set_location(h, loc_new);
    const auto& loc = r.get_location(h);
    EXPECT_EQ(*loc.store_id, 2U);
    EXPECT_EQ(loc.ndx, 5U);
  }

  // set_location by invalid handle is a no-op.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    r.erase(h);
    r.set_location(h, loc1); // Should not crash.
  }

  // Handle comparison: handles from different IDs compare by ID.
  if (true) {
    reg_t r;
    auto h0 = r.create_handle(loc0, 10);
    auto h1 = r.create_handle(loc1, 20);
    EXPECT_TRUE(h0 != h1);
    EXPECT_TRUE(h0 < h1);
  }

  // Handle comparison: same ID, different gen compares unequal.
  if (true) {
    reg_t r;
    auto h_old = r.create_handle(loc0, 10);
    r.erase(h_old);
    auto h_new = r.create_handle(loc0, 20);
    EXPECT_EQ(h_old.id(), h_new.id());
    EXPECT_TRUE(h_old != h_new);
  }

  // get_location by invalid handle returns invalid location.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    r.erase(h);
    const auto& loc = r.get_location(h);
    EXPECT_EQ(loc.store_id, store_id_t::invalid);
  }

  // set_location by handle to invalid erases the entity.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    EXPECT_TRUE(r.is_valid(h));
    r.set_location(h, loc_t{});
    EXPECT_FALSE(r.is_valid(h));
    EXPECT_EQ(r.size(), 0U);
  }

  // at(handle_t) const.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 42);
    const auto& cr = r;
    EXPECT_EQ(cr.at(h), 42);
  }

  // at(handle_t) const throws for invalid handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 42);
    r.erase(h);
    const auto& cr = r;
    EXPECT_THROW(cr.at(h), std::invalid_argument);
  }
}

void EntityRegistry_Fifo() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // Freed IDs are reused in FIFO order (oldest first).
  // Detailed FIFO behavior is tested in StableId_Fifo.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    auto id1 = r.create_id(loc0, 20);
    (void)r.create_id(loc0, 30); // id 2
    r.erase(id0);                // free: [0]
    r.erase(id1);                // free: [0, 1]
    // FIFO: 0 was freed first, so it's reused first.
    EXPECT_EQ(r.create_id(loc0, 100), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 200), id_t{1});
  }
}

void EntityRegistry_Clear() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // clear() without shrink: IDs reusable, gens bumped.
  // Detailed clear behavior is tested in StableId_Basic and StableId_Fifo.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    auto h0 = r.get_handle(id_t{0});
    r.clear();
    EXPECT_FALSE(r.is_valid(id_t{0}));
    EXPECT_FALSE(r.is_valid(h0));
    EXPECT_EQ(r.create_id(loc0, 100), id_t{0});
    EXPECT_EQ(r.get_handle(id_t{0}).gen(), 1U);
  }

  // clear(true) with shrink: fully resets storage and gens.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    r.clear(true);
    EXPECT_EQ(r.size(), 0U);
    EXPECT_FALSE(r.is_valid(id_t{0}));
    auto id0 = r.create_id(loc0, 100);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(r.get_handle(id0).gen(), 0U); // gen reset
  }
}

void EntityRegistry_Reserve() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // reserve without prefill: just reserves capacity, no IDs allocated.
  if (true) {
    reg_t r;
    r.reserve(10);
    EXPECT_FALSE(r.is_valid(id_t{0}));
    auto id0 = r.create_id(loc0, 42);
    EXPECT_EQ(*id0, 0U);
  }

  // reserve with prefill: pre-creates free slots.
  if (true) {
    reg_t r;
    r.reserve(5, true);
    // Slots exist but are not valid (free).
    EXPECT_FALSE(r.is_valid(id_t{0}));
    EXPECT_FALSE(r.is_valid(id_t{4}));
    // create reuses prefilled slots in order.
    EXPECT_EQ(r.create_id(loc0, 10), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 20), id_t{1});
    EXPECT_EQ(r.create_id(loc0, 30), id_t{2});
    EXPECT_EQ(r.create_id(loc0, 40), id_t{3});
    EXPECT_EQ(r.create_id(loc0, 50), id_t{4});
  }

  // Prefill constructor.
  if (true) {
    reg_t r{id_t{5}, true};
    EXPECT_EQ(r.id_limit(), id_t{5});
    EXPECT_EQ(r.create_id(loc0, 10), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 20), id_t{1});
  }

  // shrink_to_fit trims trailing dead records.
  // Detailed shrink behavior is tested in StableId_Basic and StableId_Fifo.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    r.erase(id_t{2});
    r.shrink_to_fit();
    EXPECT_TRUE(r.is_valid(id_t{0}));
    EXPECT_TRUE(r.is_valid(id_t{1}));
  }
}

void EntityRegistry_IdLimit() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // Constructor with limit and overflow.
  // Detailed id_limit behavior is tested in StableId_MaxId.
  if (true) {
    reg_t r{id_t{3}};
    EXPECT_EQ(r.id_limit(), id_t{3});
    EXPECT_EQ(r.create_id(loc0, 10), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 20), id_t{1});
    EXPECT_EQ(r.create_id(loc0, 30), id_t{2});
    // 4th creation fails.
    EXPECT_EQ(r.create_id(loc0, 40), id_t::invalid);
  }

  // set_id_limit on empty registry.
  if (true) {
    reg_t r;
    EXPECT_EQ(r.id_limit(), id_t::invalid);
    EXPECT_TRUE(r.set_id_limit(id_t{2}));
    EXPECT_EQ(r.id_limit(), id_t{2});
    (void)r.create_id(loc0, 10);
    (void)r.create_id(loc0, 20);
    EXPECT_EQ(r.create_id(loc0, 30), id_t::invalid);
  }
}

void EntityRegistry_NoGen() {
  using namespace id_enums;
  using reg_t = entity_registry<int, entity_id_t, store_id_t, false>;
  using id_t = reg_t::id_t;
  using handle_t = reg_t::handle_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};
  const loc_t loc1{store_id_t{0}, 1};

  // handle_t has no get_gen() when UseGen=false.
  // Detailed no-gen behavior is tested in StableId_NoGen and
  // StableId_FifoNoGen.
  if (true) {
    static_assert(sizeof(reg_t::handle_t) == sizeof(id_t));
  }

  // Basic create and access.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    auto id1 = r.create_id(loc0, 20);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(*id1, 1U);
    EXPECT_EQ(r[id0], 10);
    EXPECT_EQ(r[id1], 20);
  }

  // create_with_handle and handle validity.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 42);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(h.id(), id_t{0});
    EXPECT_EQ(r.at(h), 42);
  }

  // create_with_handle with invalid store_id returns invalid handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc_t{}, 42);
    EXPECT_FALSE(r.is_valid(h));
    EXPECT_EQ(h.id(), id_t::invalid);
  }

  // erase by handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    EXPECT_TRUE(r.erase(h));
    EXPECT_FALSE(r.is_valid(h));
    EXPECT_FALSE(r.erase(h)); // double erase
  }

  // get_location and set_location by handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    const auto& loc = r.get_location(h);
    EXPECT_EQ(*loc.store_id, 0U);
    EXPECT_EQ(loc.ndx, 0U);
    r.set_location(h, loc1);
    EXPECT_EQ(r.get_location(h).ndx, 1U);
  }

  // get_location by invalid handle returns invalid location.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    r.erase(h);
    const auto& loc = r.get_location(h);
    EXPECT_EQ(loc.store_id, store_id_t::invalid);
  }

  // set_location by invalid handle is a no-op.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    r.erase(h);
    r.set_location(h, loc1); // should not crash
  }

  // set_location by handle to invalid erases the entity.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 10);
    r.set_location(h, loc_t{});
    EXPECT_FALSE(r.is_valid(h));
    EXPECT_EQ(r.size(), 0U);
  }

  // Handle comparison.
  if (true) {
    reg_t r;
    auto h0 = r.create_handle(loc0, 10);
    auto h1 = r.create_handle(loc1, 20);
    EXPECT_TRUE(h0 != h1);
    EXPECT_TRUE(h0 < h1);
    EXPECT_TRUE(h0 == h0);
  }

  // Without gen, stale handles are falsely valid after ID reuse.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    auto h_old = r.get_handle(id0);
    r.erase(id0);
    EXPECT_FALSE(r.is_valid(h_old));
    auto id0_reused = r.create_id(loc0, 99);
    EXPECT_EQ(id0_reused, id0);
    // Without gen protection, old handle becomes valid again.
    EXPECT_TRUE(r.is_valid(h_old));
  }

  // Handle default construction.
  if (true) {
    handle_t h;
    EXPECT_EQ(h.id(), id_t::invalid);
  }

  // Handle copy construction and assignment.
  if (true) {
    handle_t h1;
    handle_t h2{h1};
    EXPECT_TRUE(h1 == h2);
    handle_t h3;
    h3 = h1;
    EXPECT_TRUE(h1 == h3);
  }

  // at(handle_t) throws for invalid handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 42);
    r.erase(h);
    EXPECT_THROW(r.at(h), std::invalid_argument);
  }

  // at(handle_t) const.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0, 42);
    const auto& cr = r;
    EXPECT_EQ(cr.at(h), 42);
  }
}

void EntityRegistry_VoidMeta() {
  using namespace id_enums;
  using reg_t = entity_registry<void>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};
  const loc_t loc1{store_id_t{0}, 1};

  // Create and validate without metadata.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0);
    auto id1 = r.create_id(loc1);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(*id1, 1U);
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
  }

  // Erase and FIFO reuse.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0); // id 0
    (void)r.create_id(loc0); // id 1
    (void)r.create_id(loc0); // id 2
    r.erase(id_t{0});
    r.erase(id_t{1});
    EXPECT_EQ(r.create_id(loc0), id_t{0});
    EXPECT_EQ(r.create_id(loc0), id_t{1});
  }

  // Handles work with void metadata.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_EQ(h.gen(), 0U);
    r.erase(h);
    EXPECT_FALSE(r.is_valid(h));
  }

  // Location operations.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0);
    const auto& loc = r.get_location(id0);
    EXPECT_EQ(*loc.store_id, 0U);
    EXPECT_EQ(loc.ndx, 0U);
    r.set_location(id0, loc1);
    const auto& loc_after = r.get_location(id0);
    EXPECT_EQ(loc_after.ndx, 1U);
  }

  // clear and reserve with prefill.
  if (true) {
    reg_t r;
    r.reserve(5, true);
    EXPECT_EQ(r.create_id(loc0), id_t{0});
    EXPECT_EQ(r.create_id(loc0), id_t{1});
    r.clear();
    EXPECT_EQ(r.create_id(loc0), id_t{0});
  }

  // erase_if with void metadata.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0); // id 0
    (void)r.create_id(loc0); // id 1
    (void)r.create_id(loc0); // id 2
    auto cnt = r.erase_if([](auto& rec) { return rec.location.ndx == 0; });
    // All have ndx 0 from loc0, so all erased.
    EXPECT_EQ(cnt, 3U);
    EXPECT_EQ(r.size(), 0U);
  }

  // shrink_to_fit with void metadata.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0); // id 0
    (void)r.create_id(loc0); // id 1
    (void)r.create_id(loc0); // id 2
    r.erase(id_t{2});
    r.shrink_to_fit();
    EXPECT_EQ(r.max_id(), id_t{1});
    EXPECT_TRUE(r.is_valid(id_t{0}));
    EXPECT_TRUE(r.is_valid(id_t{1}));
  }

  // set_id_limit with void metadata.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0); // id 0
    (void)r.create_id(loc0); // id 1
    (void)r.create_id(loc0); // id 2
    r.erase(id_t{2});
    EXPECT_TRUE(r.set_id_limit(id_t{2}));
    // id 2 was trimmed by set_id_limit, and limit prevents new id >= 2.
    EXPECT_EQ(r.id_limit(), id_t{2});
    EXPECT_EQ(r.size(), 2U);
  }

  // Handle with generation tracking (void metadata).
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0);
    auto h_old = r.get_handle(id0);
    EXPECT_EQ(h_old.gen(), 0U);
    r.erase(id0);
    auto id0_reused = r.create_id(loc0);
    EXPECT_EQ(id0_reused, id0);
    auto h_new = r.get_handle(id0_reused);
    EXPECT_EQ(h_new.gen(), 1U);
    EXPECT_FALSE(r.is_valid(h_old));
    EXPECT_TRUE(r.is_valid(h_new));
  }
}

void EntityRegistry_VoidNoGen() {
  using namespace id_enums;
  using reg_t = entity_registry<void, entity_id_t, store_id_t, false>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // Minimal footprint: no metadata, no gen.
  if (true) {
    static_assert(sizeof(reg_t::handle_t) == sizeof(id_t));
  }

  // Create and validate.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0);
    auto id1 = r.create_id(loc0);
    EXPECT_EQ(*id0, 0U);
    EXPECT_EQ(*id1, 1U);
    EXPECT_TRUE(r.is_valid(id0));
  }

  // Handle operations.
  if (true) {
    reg_t r;
    auto h = r.create_handle(loc0);
    EXPECT_TRUE(r.is_valid(h));
    EXPECT_TRUE(r.erase(h));
    EXPECT_FALSE(r.is_valid(h));
  }

  // Erase and FIFO reuse.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0); // id 0
    (void)r.create_id(loc0); // id 1
    r.erase(id_t{0});
    r.erase(id_t{1});
    EXPECT_EQ(r.create_id(loc0), id_t{0});
    EXPECT_EQ(r.create_id(loc0), id_t{1});
  }
}

void EntityRegistry_IdLimitAdvanced() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // set_id_limit fails when live IDs exist at or past the new limit.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    EXPECT_FALSE(r.set_id_limit(id_t{2}));
    EXPECT_EQ(r.id_limit(), id_t::invalid); // unchanged
  }

  // set_id_limit succeeds when only dead IDs exist past the limit; triggers
  // shrink.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    r.erase(id_t{2});
    EXPECT_EQ(r.max_id(), id_t{2});
    EXPECT_TRUE(r.set_id_limit(id_t{2}));
    EXPECT_EQ(r.id_limit(), id_t{2});
    EXPECT_EQ(r.max_id(), id_t{1}); // trimmed
  }

  // Raising the limit always succeeds.
  if (true) {
    reg_t r{id_t{3}};
    (void)r.create_id(loc0, 10);
    (void)r.create_id(loc0, 20);
    (void)r.create_id(loc0, 30);
    EXPECT_EQ(r.create_id(loc0, 40), id_t::invalid); // at limit
    EXPECT_TRUE(r.set_id_limit(id_t{5}));
    EXPECT_EQ(r.id_limit(), id_t{5});
    EXPECT_EQ(r.create_id(loc0, 40), id_t{3}); // now succeeds
  }

  // set_id_limit on empty registry with freed slots beyond limit.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    r.erase(id_t{0});
    r.erase(id_t{1});
    EXPECT_EQ(r.size(), 0U);
    EXPECT_TRUE(r.set_id_limit(id_t{1}));
    EXPECT_EQ(r.id_limit(), id_t{1});
    // Only id 0 should be available for reuse.
    EXPECT_EQ(r.create_id(loc0, 100), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 200), id_t::invalid); // at limit
  }
}

void EntityRegistry_FifoAdvanced() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // Interleaved erase/create: each create pops the oldest free.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10);      // id 0
    (void)r.create_id(loc0, 20);      // id 1
    (void)r.create_id(loc0, 30);      // id 2
    r.erase(id_t{0});                 // free: [0]
    r.erase(id_t{1});                 // free: [0, 1]
    auto r0 = r.create_id(loc0, 100); // pops 0; free: [1]
    EXPECT_EQ(r0, id_t{0});
    r.erase(id_t{2});                 // free: [1, 2]
    auto r1 = r.create_id(loc0, 200); // pops 1; free: [2]
    EXPECT_EQ(r1, id_t{1});
    auto r2 = r.create_id(loc0, 300); // pops 2; free: []
    EXPECT_EQ(r2, id_t{2});
    // All live; next gets fresh ID.
    EXPECT_EQ(r.create_id(loc0, 400), id_t{3});
  }

  // FIFO reuse order matches erase order, not ID order.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    (void)r.create_id(loc0, 40); // id 3
    (void)r.create_id(loc0, 50); // id 4
    r.erase(id_t{2});
    r.erase(id_t{0});
    r.erase(id_t{3});
    EXPECT_EQ(r.create_id(loc0, 100), id_t{2});
    EXPECT_EQ(r.create_id(loc0, 200), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 300), id_t{3});
  }

  // Free all then re-create: FIFO order matches erase order.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    r.erase(id_t{2});
    r.erase(id_t{1});
    r.erase(id_t{0});
    EXPECT_TRUE(r.size() == 0U);
    EXPECT_EQ(r.create_id(loc0, 100), id_t{2});
    EXPECT_EQ(r.create_id(loc0, 200), id_t{1});
    EXPECT_EQ(r.create_id(loc0, 300), id_t{0});
  }

  // FIFO order after clear() without shrink: rebuild scans in order.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    r.clear();
    EXPECT_EQ(r.create_id(loc0, 100), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 200), id_t{1});
    EXPECT_EQ(r.create_id(loc0, 300), id_t{2});
  }
}

void EntityRegistry_EdgeCases() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // max_id() on empty registry returns invalid.
  if (true) {
    reg_t r;
    // records_.size() is 0, so size()-1 underflows to max value = invalid.
    EXPECT_EQ(r.max_id(), id_t::invalid);
  }

  // create with default metadata parameter.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0); // uses default metadata_t{}
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_EQ(r[id0], 0); // default-initialized int
  }

  // erase_if with no matches returns 0.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 5);
    (void)r.create_id(loc0, 10);
    auto cnt = r.erase_if([](auto& rec) { return rec.metadata > 100; });
    EXPECT_EQ(cnt, 0U);
    EXPECT_EQ(r.size(), 2U);
  }

  // erase_if on empty registry.
  if (true) {
    reg_t r;
    auto cnt = r.erase_if([](auto&) { return true; });
    EXPECT_EQ(cnt, 0U);
  }

  // shrink_to_fit with interior dead: only trims trailing dead.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    r.erase(id_t{1});            // interior dead
    r.shrink_to_fit();
    // id 2 is still live so no trimming happens.
    EXPECT_EQ(r.max_id(), id_t{2});
    EXPECT_TRUE(r.is_valid(id_t{0}));
    EXPECT_FALSE(r.is_valid(id_t{1}));
    EXPECT_TRUE(r.is_valid(id_t{2}));
  }

  // shrink_to_fit trims multiple trailing dead records.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    (void)r.create_id(loc0, 40); // id 3
    r.erase(id_t{2});
    r.erase(id_t{3});
    r.shrink_to_fit();
    EXPECT_EQ(r.max_id(), id_t{1}); // trimmed to 2 records
    EXPECT_TRUE(r.is_valid(id_t{0}));
    EXPECT_TRUE(r.is_valid(id_t{1}));
  }

  // shrink_to_fit on empty registry.
  if (true) {
    reg_t r;
    r.shrink_to_fit(); // should not crash
  }

  // reserve without prefill does not affect create order.
  if (true) {
    reg_t r;
    r.reserve(100);
    EXPECT_EQ(r.size(), 0U);
    EXPECT_EQ(r.create_id(loc0, 10), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 20), id_t{1});
  }

  // Prefill constructor with prefill=false: just sets limit, no slots.
  if (true) {
    reg_t r{id_t{5}, false};
    EXPECT_EQ(r.id_limit(), id_t{5});
    EXPECT_EQ(r.create_id(loc0, 10), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 20), id_t{1});
  }

  // clear() bumps generation counters.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    auto h = r.get_handle(id0);
    EXPECT_EQ(h.gen(), 0U);
    r.clear();
    EXPECT_FALSE(r.is_valid(h));
    auto id0_new = r.create_id(loc0, 20);
    EXPECT_EQ(id0_new, id0);
    auto h_new = r.get_handle(id0_new);
    EXPECT_EQ(h_new.gen(), 1U);
    EXPECT_TRUE(h != h_new);
  }

  // Multiple erase/reuse cycles bump gen each time.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10);
    auto h0 = r.get_handle(id0);
    EXPECT_EQ(h0.gen(), 0U);
    r.erase(id0);
    auto id0_r1 = r.create_id(loc0, 20);
    EXPECT_EQ(id0_r1, id0);
    auto h1 = r.get_handle(id0_r1);
    EXPECT_EQ(h1.gen(), 1U);
    r.erase(id0_r1);
    auto id0_r2 = r.create_id(loc0, 30);
    EXPECT_EQ(id0_r2, id0);
    auto h2 = r.get_handle(id0_r2);
    EXPECT_EQ(h2.gen(), 2U);
    EXPECT_FALSE(r.is_valid(h0));
    EXPECT_FALSE(r.is_valid(h1));
    EXPECT_TRUE(r.is_valid(h2));
  }

  // FIFO free-list rebuilt correctly after shrink_to_fit.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    (void)r.create_id(loc0, 40); // id 3
    (void)r.create_id(loc0, 50); // id 4
    r.erase(id_t{3});
    r.erase(id_t{4});
    r.erase(id_t{0});
    // Live: 1, 2. Trailing dead: 3, 4. Interior dead: 0.
    r.shrink_to_fit();
    // After shrink: records 0..2; 3 and 4 trimmed.
    // Free list rebuilt: only id 0 is free.
    EXPECT_EQ(r.size(), 2U);
    EXPECT_EQ(r.create_id(loc0, 100), id_t{0});
    // No more free; next gets fresh id 3.
    EXPECT_EQ(r.create_id(loc0, 200), id_t{3});
  }

  // size() and is_valid consistency after mixed operations.
  if (true) {
    reg_t r;
    EXPECT_EQ(r.size(), 0U);
    auto id0 = r.create_id(loc0, 10);
    auto id1 = r.create_id(loc0, 20);
    auto id2 = r.create_id(loc0, 30);
    EXPECT_EQ(r.size(), 3U);
    r.erase(id1);
    EXPECT_EQ(r.size(), 2U);
    auto id3 = r.create_id(loc0, 40); // reuses id 1
    EXPECT_EQ(r.size(), 3U);
    EXPECT_EQ(id3, id1);
    r.erase(id0);
    r.erase(id2);
    r.erase(id3);
    EXPECT_EQ(r.size(), 0U);
  }

  // Constructor with id_limit and custom allocator.
  if (true) {
    std::allocator<int> alloc;
    reg_t r{id_t{5}, true, alloc};
    EXPECT_EQ(r.id_limit(), id_t{5});
    EXPECT_EQ(r.create_id(loc0, 10), id_t{0});
  }

  // Prefill constructor with prefill=false: just sets limit.
  if (true) {
    reg_t r{id_t::invalid, false};
    EXPECT_EQ(r.id_limit(), id_t::invalid);
  }

  // Prefill constructor with id_limit{0}: early return, can't create.
  if (true) {
    reg_t r{id_t{0}};
    EXPECT_EQ(r.id_limit(), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 10), id_t::invalid);
  }

  // set_id_limit to 0: no IDs allowed.
  if (true) {
    reg_t r;
    EXPECT_TRUE(r.set_id_limit(id_t{0}));
    EXPECT_EQ(r.id_limit(), id_t{0});
    EXPECT_EQ(r.create_id(loc0, 10), id_t::invalid);
  }

  // set_id_limit to same value: no-op.
  if (true) {
    reg_t r{id_t{3}};
    (void)r.create_id(loc0, 10);
    EXPECT_TRUE(r.set_id_limit(id_t{3}));
    EXPECT_EQ(r.id_limit(), id_t{3});
    EXPECT_TRUE(r.is_valid(id_t{0}));
  }

  // erase_if erases all when all match.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 5);
    (void)r.create_id(loc0, 10);
    (void)r.create_id(loc0, 15);
    auto cnt = r.erase_if([](auto&) { return true; });
    EXPECT_EQ(cnt, 3U);
    EXPECT_EQ(r.size(), 0U);
  }

  // shrink_to_fit on all-dead registry trims to empty.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10);
    (void)r.create_id(loc0, 20);
    r.erase(id_t{0});
    r.erase(id_t{1});
    r.shrink_to_fit();
    EXPECT_EQ(r.size(), 0U);
    // After full trim, fresh IDs start from 0.
    EXPECT_EQ(r.create_id(loc0, 100), id_t{0});
  }
}

void EntityRegistry_MetadataCleanup() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // Metadata is cleared on erase; re-creation with default gets 0, not stale.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 42);
    EXPECT_EQ(r[id0], 42);
    r.erase(id0);
    auto id0_reused = r.create_id(loc0);
    EXPECT_EQ(id0_reused, id0);
    EXPECT_EQ(r[id0_reused], 0);
  }

  // Metadata is cleared on erase even when re-created with explicit value.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 999);
    r.erase(id0);
    auto id0_reused = r.create_id(loc0, 7);
    EXPECT_EQ(r[id0_reused], 7);
  }

  // Metadata is cleared on clear(false); re-creation with default gets 0.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 100);
    (void)r.create_id(loc0, 200);
    (void)r.create_id(loc0, 300);
    r.clear();
    EXPECT_EQ(r.create_id(loc0), id_t{0});
    EXPECT_EQ(r[id_t{0}], 0);
    EXPECT_EQ(r.create_id(loc0), id_t{1});
    EXPECT_EQ(r[id_t{1}], 0);
    EXPECT_EQ(r.create_id(loc0), id_t{2});
    EXPECT_EQ(r[id_t{2}], 0);
  }

  // Metadata is cleared on clear(true); re-creation with default gets 0.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 100);
    (void)r.create_id(loc0, 200);
    r.clear(true);
    auto id0 = r.create_id(loc0);
    EXPECT_EQ(r[id0], 0);
  }
}

void EntityRegistry_EraseIfPredicate() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // Predicate is only called for valid (live) records, not dead ones.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    (void)r.create_id(loc0, 40); // id 3
    r.erase(id_t{1});
    r.erase(id_t{3});
    // 2 live, 2 dead. Predicate should be called exactly twice.
    size_t call_count = 0;
    auto cnt = r.erase_if([&](auto& rec) {
      ++call_count;
      return rec.metadata > 20;
    });
    EXPECT_EQ(call_count, 2U); // only live records
    EXPECT_EQ(cnt, 1U);        // only id 2 (metadata 30) matches
    EXPECT_TRUE(r.is_valid(id_t{0}));
    EXPECT_FALSE(r.is_valid(id_t{2}));
  }
}

void EntityRegistry_IdLimitFreeList() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // Reducing limit trims free-list entries while interior live records remain.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    (void)r.create_id(loc0, 40); // id 3
    (void)r.create_id(loc0, 50); // id 4
    r.erase(id_t{1});            // interior dead
    r.erase(id_t{3});            // will be trimmed
    r.erase(id_t{4});            // will be trimmed
    // Live: 0, 2. Dead: 1, 3, 4. Trim to limit 3 removes 3 and 4.
    EXPECT_TRUE(r.set_id_limit(id_t{3}));
    EXPECT_EQ(r.size(), 2U);
    EXPECT_TRUE(r.is_valid(id_t{0}));
    EXPECT_FALSE(r.is_valid(id_t{1}));
    EXPECT_TRUE(r.is_valid(id_t{2}));
    // Free list should only contain id 1.
    EXPECT_EQ(r.create_id(loc0, 60), id_t{1});
    // At limit now: 3 live, limit is 3.
    EXPECT_EQ(r.create_id(loc0, 70), id_t::invalid);
  }
}

void EntityRegistry_ReservePrefillExisting() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  const loc_t loc0{store_id_t{0}, 0};

  // reserve with prefill on a registry that already has live entities.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(loc0, 10); // id 0
    auto id1 = r.create_id(loc0, 20); // id 1
    EXPECT_EQ(r.size(), 2U);
    r.reserve(5, true); // adds free slots 2, 3, 4
    // Existing entities are undisturbed.
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_EQ(r[id0], 10);
    EXPECT_EQ(r[id1], 20);
    // New creates should use the prefilled free slots.
    EXPECT_EQ(r.create_id(loc0, 30), id_t{2});
    EXPECT_EQ(r.create_id(loc0, 40), id_t{3});
    EXPECT_EQ(r.create_id(loc0, 50), id_t{4});
    // Next create expands.
    EXPECT_EQ(r.create_id(loc0, 60), id_t{5});
  }

  // reserve with prefill when some slots are already free.
  if (true) {
    reg_t r;
    (void)r.create_id(loc0, 10); // id 0
    (void)r.create_id(loc0, 20); // id 1
    (void)r.create_id(loc0, 30); // id 2
    r.erase(id_t{1});            // free: [1]
    r.reserve(5, true);          // adds free slots 3, 4
    // Free list should be: 1 (existing), then 3, 4 (new).
    EXPECT_EQ(r.create_id(loc0, 40), id_t{1});
    EXPECT_EQ(r.create_id(loc0, 50), id_t{3});
    EXPECT_EQ(r.create_id(loc0, 60), id_t{4});
  }
}

void EntityRegistry_HandleOwner() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  using owner_t = reg_t::handle_owner;
  const loc_t staging{store_id_t{}};

  // Default-constructed owner holds no entity.
  if (true) {
    owner_t o;
    EXPECT_FALSE(bool(o));
    EXPECT_EQ(o.id(), id_t::invalid);
  }

  // Constructor from existing valid handle takes ownership.
  if (true) {
    reg_t r;
    auto h = r.create_handle(staging, 42);
    owner_t o{r, h};
    EXPECT_TRUE(bool(o));
    EXPECT_EQ(o.id(), h.id());
    EXPECT_TRUE(o.handle() == h);
  }

  // Constructor from existing invalid handle: owner is empty.
  if (true) {
    reg_t r;
    auto h = r.create_handle(staging, 42);
    r.erase(h);
    owner_t o{r, h};
    EXPECT_FALSE(bool(o));
    EXPECT_EQ(o.id(), id_t::invalid);
  }

  // Creating constructor: creates entity and takes ownership.
  if (true) {
    reg_t r;
    owner_t o{r, staging, 99};
    EXPECT_TRUE(bool(o));
    EXPECT_NE(o.id(), id_t::invalid);
    EXPECT_EQ(r[o.id()], 99);
    EXPECT_TRUE(r.is_valid(o.id()));
  }

  // make_owner factory method.
  if (true) {
    reg_t r;
    auto o = r.make_owner(staging, 77);
    EXPECT_TRUE(bool(o));
    EXPECT_NE(o.id(), id_t::invalid);
    EXPECT_EQ(r[o.id()], 77);
  }

  // Creating constructor: failure (registry at limit) → owner is empty.
  if (true) {
    reg_t r{id_t{0}};
    owner_t o{r, staging, 10};
    EXPECT_FALSE(bool(o));
    EXPECT_EQ(o.id(), id_t::invalid);
  }

  // Destructor erases entity when owner goes out of scope.
  if (true) {
    reg_t r;
    id_t saved_id;
    {
      auto o = r.make_owner(staging, 10);
      saved_id = o.id();
      EXPECT_TRUE(r.is_valid(saved_id));
    }
    EXPECT_FALSE(r.is_valid(saved_id));
  }

  // Destructor on empty owner is a no-op.
  if (true) {
    owner_t o; // destructs without touching any registry
  }

  // release() returns handle, leaves entity alive, and empties owner.
  if (true) {
    reg_t r;
    auto o = r.make_owner(staging, 10);
    auto id = o.id();
    auto h = o.release();
    EXPECT_FALSE(bool(o));
    EXPECT_EQ(o.id(), id_t::invalid);
    EXPECT_EQ(h.id(), id);
    EXPECT_TRUE(r.is_valid(h));
    r.erase(h); // manual cleanup
  }

  // reset() erases entity and empties owner.
  if (true) {
    reg_t r;
    auto o = r.make_owner(staging, 10);
    auto id = o.id();
    o.reset();
    EXPECT_FALSE(bool(o));
    EXPECT_FALSE(r.is_valid(id));
  }

  // reset() on empty owner is a no-op.
  if (true) {
    owner_t o;
    o.reset();
    EXPECT_FALSE(bool(o));
  }

  // Move constructor transfers ownership; source becomes empty.
  if (true) {
    reg_t r;
    auto o1 = r.make_owner(staging, 10);
    auto id = o1.id();
    owner_t o2{std::move(o1)};
    EXPECT_FALSE(bool(o1));
    EXPECT_EQ(o1.id(), id_t::invalid);
    EXPECT_TRUE(bool(o2));
    EXPECT_EQ(o2.id(), id);
    EXPECT_TRUE(r.is_valid(id));
  }

  // Move assignment: erases current entity, takes ownership of source.
  if (true) {
    reg_t r;
    auto o1 = r.make_owner(staging, 10);
    auto o2 = r.make_owner(staging, 20);
    auto id1 = o1.id();
    auto id2 = o2.id();
    o2 = std::move(o1);
    EXPECT_FALSE(bool(o1));
    EXPECT_TRUE(bool(o2));
    EXPECT_EQ(o2.id(), id1);
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_FALSE(r.is_valid(id2)); // erased by move assignment
  }

  // Move assignment into empty owner.
  if (true) {
    reg_t r;
    auto o1 = r.make_owner(staging, 10);
    auto id = o1.id();
    owner_t o2;
    o2 = std::move(o1);
    EXPECT_FALSE(bool(o1));
    EXPECT_TRUE(bool(o2));
    EXPECT_EQ(o2.id(), id);
  }

  // Move self-assignment is a no-op (entity stays intact).
  if (true) {
    reg_t r;
    auto o = r.make_owner(staging, 10);
    auto id = o.id();
    // Use pointer indirection to avoid -Wself-move.
    owner_t* p = &o;
    o = std::move(*p);
    EXPECT_TRUE(bool(o));
    EXPECT_EQ(o.id(), id);
    EXPECT_TRUE(r.is_valid(id));
  }

  // handle() accessor returns the underlying handle.
  if (true) {
    reg_t r;
    auto h = r.create_handle(staging, 10);
    owner_t o{r, h};
    EXPECT_TRUE(o.handle() == h);
    EXPECT_EQ(o.handle().id(), h.id());
    EXPECT_EQ(o.handle().gen(), h.gen());
  }

  // registry() returns a reference to the associated registry.
  if (true) {
    reg_t r;
    auto o = r.make_owner(staging, 10);
    EXPECT_TRUE(&o.registry() == &r);
  }

  // const registry() returns a const reference.
  if (true) {
    reg_t r;
    const auto o = r.make_owner(staging, 10);
    const reg_t& cr = o.registry();
    EXPECT_TRUE(&cr == &r);
  }

  // RAII pattern: early return erases entity; success path releases it.
  if (true) {
    reg_t r;
    id_t saved_id = id_t::invalid;

    auto do_work = [&](bool succeed) -> bool {
      auto o = r.make_owner(staging, 10);
      if (!o) return false;
      saved_id = o.id();
      if (!succeed) return false; // destructor fires, entity erased
      (void)o.release();
      return true;
    };

    EXPECT_FALSE(do_work(false));
    EXPECT_FALSE(r.is_valid(saved_id)); // erased on early return

    EXPECT_TRUE(do_work(true));
    EXPECT_TRUE(r.is_valid(saved_id)); // survived via release()
    r.erase(saved_id);
  }

  // handle_owner with void metadata.
  if (true) {
    using vreg_t = entity_registry<void>;
    const vreg_t::location_t vstaging{store_id_t{}};
    vreg_t r;
    auto o = r.make_owner(vstaging);
    EXPECT_TRUE(bool(o));
    auto id = o.id();
    EXPECT_TRUE(r.is_valid(id));
    o.reset();
    EXPECT_FALSE(r.is_valid(id));
  }
}

void ComponentStorage_Basic() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<float, reg_t>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // Construction.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0U);
    EXPECT_EQ(*s.store_id(), *sid);
  }

  // Construction with invalid store_id throws.
  if (true) {
    reg_t r;
    EXPECT_THROW(storage_t(r, store_id_t::invalid), std::invalid_argument);
    EXPECT_THROW(storage_t(r, store_id_t{}), std::invalid_argument);
  }

  // Default construction.
  if (true) {
    storage_t s;
    EXPECT_TRUE(s.empty());
  }

  // Add and lookup.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_EQ(s.size(), 2U);
    EXPECT_EQ(s[id0], 1.0f);
    EXPECT_EQ(s[id1], 2.0f);
    EXPECT_EQ(s.at(id0), 1.0f);
    EXPECT_EQ(s.at(id1), 2.0f);
  }

  // Const access.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 3.14f));
    const auto& cs = s;
    EXPECT_EQ(cs[id0], 3.14f);
    EXPECT_EQ(cs.at(id0), 3.14f);
  }

  // Mutable access via operator[].
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    s[id0] = 99.0f;
    EXPECT_EQ(s[id0], 99.0f);
  }

  // contains by ID.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_FALSE(s.contains(id0));
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.contains(id0));
    EXPECT_FALSE(s.contains(id_t{99}));
  }

  // at() throws for entity not in this storage.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_THROW(s.at(id_t{0}), std::out_of_range);
  }

  // Registry location updated on add.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    const auto loc = r.get_location(id0);
    EXPECT_EQ(*loc.store_id, *sid);
    EXPECT_EQ(loc.ndx, 0U);
  }

  // add returns false for entity not at store 0.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_FALSE(s.add(id0, 2.0f));
  }

  // add_new creates entity and adds component; returns its handle.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h0 = s.add_new(1.0f, 42);
    EXPECT_TRUE(bool(h0));
    EXPECT_EQ(s.size(), 1U);
    EXPECT_EQ(s[h0.id()], 1.0f);
    EXPECT_EQ(r[h0.id()], 42);
    EXPECT_TRUE(s.contains(h0));
    const auto loc = r.get_location(h0);
    EXPECT_EQ(*loc.store_id, *sid);
  }

  // add_new with default metadata.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h0 = s.add_new(5.5f);
    EXPECT_TRUE(bool(h0));
    EXPECT_EQ(s[h0.id()], 5.5f);
    EXPECT_EQ(r[h0.id()], 0);
  }

  // reserve.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    s.reserve(100);
    EXPECT_TRUE(s.empty());
  }
}

void ComponentStorage_Handle() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<float, reg_t>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // add by handle.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(s.add(h, 1.0f));
    EXPECT_EQ(s.size(), 1U);
    EXPECT_EQ(s[h.id()], 1.0f);
  }

  // add by invalid handle returns false.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    r.erase(h);
    EXPECT_FALSE(s.add(h, 1.0f));
  }

  // contains by handle.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_FALSE(s.contains(h));
    EXPECT_TRUE(s.add(h, 1.0f));
    EXPECT_TRUE(s.contains(h));
  }

  // contains by stale handle returns false.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(s.add(h, 1.0f));
    s.erase(h.id());
    EXPECT_FALSE(s.contains(h));
  }

  // at by handle.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(s.add(h, 3.14f));
    EXPECT_EQ(s.at(h), 3.14f);
    s.at(h) = 99.0f;
    EXPECT_EQ(s.at(h), 99.0f);
  }

  // at by handle const.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(s.add(h, 3.14f));
    const auto& cs = s;
    EXPECT_EQ(cs.at(h), 3.14f);
  }

  // at by invalid handle throws.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_THROW(s.at(h), std::invalid_argument);
  }

  // remove by handle.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(s.add(h, 1.0f));
    EXPECT_TRUE(s.remove(h));
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(r.is_valid(h));
    const auto loc = r.get_location(h);
    EXPECT_EQ(*loc.store_id, 0U);
  }

  // remove by invalid handle returns false.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_FALSE(s.remove(h));
  }

  // erase by handle.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_TRUE(s.add(h, 1.0f));
    EXPECT_TRUE(s.erase(h));
    EXPECT_TRUE(s.empty());
    EXPECT_FALSE(r.is_valid(h));
  }

  // erase by invalid handle returns false.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto h = r.create_handle(staging, 10);
    EXPECT_FALSE(s.erase(h));
  }
}

void ComponentStorage_Remove() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<float, reg_t>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // Remove moves entity to store 0, entity remains valid.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.remove(id0));
    EXPECT_FALSE(s.contains(id0));
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(r.is_valid(id0));
    const auto loc = r.get_location(id0);
    EXPECT_EQ(*loc.store_id, 0U);
  }

  // Remove returns false for entity not in this storage.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_FALSE(s.remove(id_t{0}));
  }

  // Swap-and-pop: remove non-last element.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_TRUE(s.add(id2, 3.0f));
    EXPECT_TRUE(s.remove(id0));
    EXPECT_EQ(s.size(), 2U);
    EXPECT_EQ(s[id2], 3.0f);
    EXPECT_EQ(r.get_location(id2).ndx, 0U);
    EXPECT_EQ(s[id1], 2.0f);
    EXPECT_EQ(r.get_location(id1).ndx, 1U);
  }

  // Remove last element (no swap needed).
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_TRUE(s.remove(id1));
    EXPECT_EQ(s.size(), 1U);
    EXPECT_EQ(s[id0], 1.0f);
  }

  // Remove allows re-add.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.remove(id0));
    EXPECT_TRUE(s.add(id0, 99.0f));
    EXPECT_EQ(s[id0], 99.0f);
  }
}

void ComponentStorage_RemoveAll() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<float, reg_t>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // remove_all moves all entities to store 0.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_TRUE(s.add(id2, 3.0f));
    s.remove_all();
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_TRUE(r.is_valid(id2));
    EXPECT_EQ(*r.get_location(id0).store_id, 0U);
  }

  // remove_all on empty is a no-op.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    s.remove_all();
    EXPECT_TRUE(s.empty());
  }

  // Can re-add after remove_all.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    s.remove_all();
    EXPECT_TRUE(s.add(id0, 99.0f));
    EXPECT_EQ(s[id0], 99.0f);
  }
}

void ComponentStorage_Erase() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<float, reg_t>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // Erase removes component and destroys entity in registry.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.erase(id0));
    EXPECT_TRUE(s.empty());
    EXPECT_FALSE(r.is_valid(id0));
  }

  // Erase with swap-and-pop.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_TRUE(s.erase(id0));
    EXPECT_EQ(s.size(), 1U);
    EXPECT_FALSE(r.is_valid(id0));
    EXPECT_TRUE(r.is_valid(id1));
    EXPECT_EQ(s[id1], 2.0f);
    EXPECT_EQ(r.get_location(id1).ndx, 0U);
  }

  // Erase returns false for invalid entity.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_FALSE(s.erase(id_t{0}));
  }
}

void ComponentStorage_EraseIf() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<float, reg_t>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // erase_if removes matching entities.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 5.0f));
    EXPECT_TRUE(s.add(id2, 10.0f));
    auto cnt = s.erase_if([](float val, auto) { return val > 3.0f; });
    EXPECT_EQ(cnt, 2U);
    EXPECT_EQ(s.size(), 1U);
    EXPECT_TRUE(s.contains(id0));
    EXPECT_FALSE(r.is_valid(id1));
    EXPECT_FALSE(r.is_valid(id2));
  }

  // erase_if with no matches.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    auto cnt = s.erase_if([](float, auto) { return false; });
    EXPECT_EQ(cnt, 0U);
    EXPECT_EQ(s.size(), 1U);
  }

  // erase_if on empty storage.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto cnt = s.erase_if([](float, auto) { return true; });
    EXPECT_EQ(cnt, 0U);
  }
}

void ComponentStorage_Clear() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<float, reg_t>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // Clear removes all components and erases entities from registry.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_TRUE(s.add(id2, 3.0f));
    s.clear();
    EXPECT_TRUE(s.empty());
    EXPECT_FALSE(r.is_valid(id0));
    EXPECT_FALSE(r.is_valid(id1));
    EXPECT_FALSE(r.is_valid(id2));
  }

  // Clear on empty is a no-op.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    s.clear();
    EXPECT_TRUE(s.empty());
  }
}

void ComponentStorage_SwapAndMove() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<float, reg_t>;

  const auto sid1 = store_id_t{1};
  const auto sid2 = store_id_t{2};
  const loc_t staging{store_id_t{}};

  // Member swap.
  if (true) {
    reg_t r;
    storage_t s1{r, sid1};
    storage_t s2{r, sid2};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s1.add(id0, 1.0f));
    EXPECT_TRUE(s2.add(id1, 2.0f));
    s1.swap(s2);
    EXPECT_EQ(*s1.store_id(), *sid2);
    EXPECT_EQ(*s2.store_id(), *sid1);
    EXPECT_EQ(s1.size(), 1U);
    EXPECT_EQ(s2.size(), 1U);
  }

  // Friend swap.
  if (true) {
    reg_t r;
    storage_t s1{r, sid1};
    storage_t s2{r, sid2};
    using std::swap;
    swap(s1, s2);
    EXPECT_EQ(*s1.store_id(), *sid2);
    EXPECT_EQ(*s2.store_id(), *sid1);
  }

  // Move constructor.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(staging, 10);
    {
      storage_t s1{r, sid1};
      EXPECT_TRUE(s1.add(id0, 1.0f));
      storage_t s2{std::move(s1)};
      EXPECT_EQ(s2.size(), 1U);
      EXPECT_EQ(s2[id0], 1.0f);
      EXPECT_EQ(*s2.store_id(), *sid1);
    }
    EXPECT_FALSE(r.is_valid(id0));
  }

  // Move assignment.
  if (true) {
    reg_t r;
    auto id0 = r.create_id(staging, 10);
    {
      storage_t s1{r, sid1};
      EXPECT_TRUE(s1.add(id0, 1.0f));
      storage_t s2;
      s2 = std::move(s1);
      EXPECT_EQ(s2.size(), 1U);
      EXPECT_EQ(s2[id0], 1.0f);
    }
    EXPECT_FALSE(r.is_valid(id0));
  }
}

void ComponentStorage_LimitAndReserve() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<float, reg_t>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // Default limit is effectively unlimited.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_EQ(s.limit(), *id_t::invalid);
  }

  // Constructor with limit.
  if (true) {
    reg_t r;
    storage_t s{r, sid, 3};
    EXPECT_EQ(s.limit(), 3U);
    EXPECT_TRUE(s.empty());
  }

  // Constructor with limit and reserve.
  if (true) {
    reg_t r;
    storage_t s{r, sid, 5, true};
    EXPECT_EQ(s.limit(), 5U);
    EXPECT_TRUE(s.empty());
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_EQ(s[id0], 1.0f);
  }

  // Constructor with limit enforces on add.
  if (true) {
    reg_t r;
    storage_t s{r, sid, 1};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_FALSE(s.add(id1, 2.0f));
  }

  // Constructor with default limit and reserve=true is a no-op reserve.
  if (true) {
    reg_t r;
    storage_t s{r, sid, *id_t::invalid, true};
    EXPECT_EQ(s.limit(), *id_t::invalid);
    EXPECT_TRUE(s.empty());
  }

  // set_limit on empty storage.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.set_limit(3));
    EXPECT_EQ(s.limit(), 3U);
  }

  // set_limit enforced on add.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.set_limit(2));
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_FALSE(s.add(id2, 3.0f));
    EXPECT_EQ(s.size(), 2U);
    // id2 should still be in staging.
    EXPECT_TRUE(r.is_valid(id2));
    EXPECT_EQ(*r.get_location(id2).store_id, 0U);
  }

  // set_limit enforced on add by handle.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.set_limit(1));
    auto h0 = r.create_handle(staging, 10);
    auto h1 = r.create_handle(staging, 20);
    EXPECT_TRUE(s.add(h0, 1.0f));
    EXPECT_FALSE(s.add(h1, 2.0f));
    EXPECT_EQ(s.size(), 1U);
  }

  // set_limit enforced on add_new.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.set_limit(1));
    auto h0 = s.add_new(1.0f, 10);
    EXPECT_TRUE(bool(h0));
    auto h1 = s.add_new(2.0f, 20);
    EXPECT_FALSE(bool(h1));
    EXPECT_EQ(s.size(), 1U);
    // The failed add_new should have cleaned up the entity it created.
    EXPECT_EQ(r.size(), 1U);
  }

  // set_limit fails if current size exceeds new limit.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_FALSE(s.set_limit(1));
    EXPECT_EQ(s.limit(), *id_t::invalid); // unchanged
  }

  // set_limit succeeds when equal to current size.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.set_limit(1));
    EXPECT_EQ(s.limit(), 1U);
  }

  // Remove frees a slot under the limit.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.set_limit(2));
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_FALSE(s.add(id2, 3.0f)); // at limit
    EXPECT_TRUE(s.remove(id0));
    EXPECT_TRUE(s.add(id2, 3.0f)); // now succeeds
    EXPECT_EQ(s.size(), 2U);
  }

  // set_limit to 0: no adds allowed.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.set_limit(0));
    auto id0 = r.create_id(staging, 10);
    EXPECT_FALSE(s.add(id0, 1.0f));
  }

  // Raising the limit allows more adds.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.set_limit(1));
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_FALSE(s.add(id1, 2.0f));
    EXPECT_TRUE(s.set_limit(2));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_EQ(s.size(), 2U);
  }

  // shrink_to_fit reduces capacity.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    s.reserve(100);
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    s.shrink_to_fit();
    EXPECT_EQ(s.size(), 1U);
    EXPECT_EQ(s[id0], 1.0f);
  }

  // shrink_to_fit on empty storage.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    s.shrink_to_fit(); // should not crash
    EXPECT_TRUE(s.empty());
  }

  // Limit is preserved across swap.
  if (true) {
    reg_t r;
    storage_t s1{r, store_id_t{1}};
    storage_t s2{r, store_id_t{2}};
    EXPECT_TRUE(s1.set_limit(5));
    EXPECT_TRUE(s2.set_limit(10));
    s1.swap(s2);
    EXPECT_EQ(s1.limit(), 10U);
    EXPECT_EQ(s2.limit(), 5U);
  }
}

void ComponentStorage_Iterator() {
  using namespace id_enums;
  using reg_t = entity_registry<int>;
  using id_t = reg_t::id_t;
  using loc_t = reg_t::location_t;
  using storage_t = component_storage<float, reg_t>;

  const auto sid = store_id_t{1};
  const loc_t staging{store_id_t{}};

  // begin == end on empty storage.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    EXPECT_TRUE(s.begin() == s.end());
    EXPECT_TRUE(s.cbegin() == s.cend());
  }

  // Dereference yields component reference; id() yields entity ID.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    auto it = s.begin();
    EXPECT_EQ(*it, 1.0f);
    EXPECT_EQ(it.id(), id0);
    ++it;
    EXPECT_EQ(*it, 2.0f);
    EXPECT_EQ(it.id(), id1);
    ++it;
    EXPECT_TRUE(it == s.end());
  }

  // Mutable iteration: modify components through iterator.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 1.0f));
    *s.begin() = 99.0f;
    EXPECT_EQ(s[id0], 99.0f);
  }

  // Const iteration.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    EXPECT_TRUE(s.add(id0, 3.14f));
    const auto& cs = s;
    auto it = cs.begin();
    EXPECT_EQ(*it, 3.14f);
    EXPECT_EQ(it.id(), id0);
  }

  // Range-for loop to sum components.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_TRUE(s.add(id2, 3.0f));
    float sum = 0.0f;
    for (auto val : s) sum += val;
    EXPECT_EQ(sum, 6.0f);
  }

  // Range-for with id() access via explicit iterator.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    std::vector<id_t> ids;
    for (auto it = s.begin(); it != s.end(); ++it) ids.push_back(it.id());
    EXPECT_EQ(ids.size(), 2U);
    EXPECT_EQ(ids[0], id0);
    EXPECT_EQ(ids[1], id1);
  }

  // Random access: operator[], +, -, +=, -=.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    auto id2 = r.create_id(staging, 30);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    EXPECT_TRUE(s.add(id2, 3.0f));
    auto it = s.begin();
    EXPECT_EQ(it[0], 1.0f);
    EXPECT_EQ(it[2], 3.0f);
    auto it2 = it + 2;
    EXPECT_EQ(*it2, 3.0f);
    EXPECT_EQ(it2.id(), id2);
    EXPECT_EQ(it2 - it, 2);
    it2 -= 1;
    EXPECT_EQ(*it2, 2.0f);
  }

  // Comparison operators.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    auto a = s.begin();
    auto b = s.begin() + 1;
    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b > a);
    EXPECT_TRUE(a <= a);
    EXPECT_TRUE(a != b);
  }

  // Post-increment and post-decrement.
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    auto it = s.begin();
    auto prev = it++;
    EXPECT_EQ(*prev, 1.0f);
    EXPECT_EQ(*it, 2.0f);
    auto next = it--;
    EXPECT_EQ(*next, 2.0f);
    EXPECT_EQ(*it, 1.0f);
  }

  // n + iterator (commutative).
  if (true) {
    reg_t r;
    storage_t s{r, sid};
    auto id0 = r.create_id(staging, 10);
    auto id1 = r.create_id(staging, 20);
    EXPECT_TRUE(s.add(id0, 1.0f));
    EXPECT_TRUE(s.add(id1, 2.0f));
    auto it = 1 + s.begin();
    EXPECT_EQ(*it, 2.0f);
  }
}

MAKE_TEST_LIST(ArchetypeVector_Basic, ArchetypeVector_NoCopy, StableId_Basic,
    StableId_SmallId, StableId_NoThrow, StableId_Fifo, StableId_NoGen,
    StableId_FifoNoGen, StableId_MaxId, EntityRegistry_Basic,
    EntityRegistry_Handle, EntityRegistry_Fifo, EntityRegistry_Clear,
    EntityRegistry_Reserve, EntityRegistry_IdLimit, EntityRegistry_NoGen,
    EntityRegistry_VoidMeta, EntityRegistry_VoidNoGen,
    EntityRegistry_IdLimitAdvanced, EntityRegistry_FifoAdvanced,
    EntityRegistry_EdgeCases, EntityRegistry_MetadataCleanup,
    EntityRegistry_EraseIfPredicate, EntityRegistry_IdLimitFreeList,
    EntityRegistry_ReservePrefillExisting, EntityRegistry_HandleOwner,
    ComponentStorage_Basic,
    ComponentStorage_Handle, ComponentStorage_Remove,
    ComponentStorage_RemoveAll, ComponentStorage_Erase,
    ComponentStorage_EraseIf, ComponentStorage_Clear,
    ComponentStorage_SwapAndMove, ComponentStorage_LimitAndReserve,
    ComponentStorage_Iterator);

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
