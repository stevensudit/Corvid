// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2024 Steven Sudit
//
// Licensed under the Apache License, Version 2.0(the "License");
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
#pragma once
#include "containers_shared.h"
#include "arena_allocator.h"
#include "opt_find.h"
#include "../enums.h"
#include "../strings/cstring_view.h"

namespace corvid { inline namespace container { inline namespace intern {

using namespace sequence;
using namespace arena;

// Provide restricted access to `allow` to control construction.
class restrict_intern_construction {
  enum class allow { ctor };

  template<typename T, SequentialEnum ID>
  friend class interned_value;

  template<typename T, SequentialEnum ID, typename TR>
  friend class intern_table;

  template<typename T, SequentialEnum ID>
  friend struct intern_test;
};

// Fwd.
template<typename T, SequentialEnum ID>
class interned_value;

template<typename T, SequentialEnum ID, typename X = void>
struct intern_traits {};

template<typename T, SequentialEnum ID, typename TR>
class intern_table;

// Overview:
// An intern table stores unique values of type `T` and allows them to be
// looked up by ID or by value. The address of the value is its identity, so
// the ID is effectively a pointer. This table is implemented as a chain,
// allowing lower ID ranges to be shared.
//
// For efficiency, the table uses an arena allocator to store the values and
// the containers that index them, which requires some trickery to allow the
// arena-based internal types to appear as the external types.

//

// Lightweight view of interned value.
//
// Contains a nullable pointer to a value of `T` and an ID of type `ID`.
//
// Intern requirements:
// - Unique values are stored in a single location and never moved.
// - Lookup by ID, returning the unique value.
// - Lookup by copy of value, returning the ID and unique value.
// - Matching by value can be transparent. This means looking up by a view and
// potentially converting that view into the value type.

// Represents an interned value, which is a singleton that can be looked up by
// ID. Cannot be constructed directly, only by an `intern_table`.
//
// Does not reference the `intern_table` that defines it, so the ID can in
// principle be non-unique across tables.
template<typename T, SequentialEnum ID>
class interned_value {
  using allow = restrict_intern_construction::allow;

public:
  using value_t = T;
  using id_t = ID;

  // Effectively-private constructors.
  constexpr interned_value(allow, const value_t& value, id_t id)
      : value_{&value}, id_{id} {}
  constexpr interned_value(allow, const value_t* value, id_t id)
      : value_{value}, id_{id} {}

  constexpr interned_value() noexcept = default;
  constexpr interned_value(const interned_value&) noexcept = default;
  constexpr interned_value(interned_value&&) noexcept = default;
  constexpr interned_value& operator=(
      const interned_value&) noexcept = default;
  constexpr interned_value& operator=(interned_value&&) noexcept = default;

  // Look up by ID.
  template<typename TR>
  interned_value(const intern_table<T, ID, TR>& table, ID id,
      const lock& attestation = {});

  // Look up by value (or view into it).
  template<typename U, typename TR>
  requires Viewable<T, U>
  interned_value(const intern_table<T, ID, TR>& table, U&& value,
      const lock& attestation = {});

  // Create by interning.
  template<typename U, typename TR>
  requires Viewable<T, U>
  interned_value(intern_table<T, ID, TR>& table, U&& value,
      const lock& attestation = {});

  // Accessors.
  [[nodiscard]] constexpr id_t id() const noexcept { return id_; }

  [[nodiscard]] constexpr bool has_value() const noexcept { return value_; }
  [[nodiscard]] constexpr const value_t& value() const { return *value_; }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return value_;
  }
  [[nodiscard]] constexpr bool operator!() const noexcept { return !value_; }
  [[nodiscard]] constexpr const value_t& operator*() const { return value(); }
  [[nodiscard]] constexpr const value_t* operator->() const { return value_; }

  // Equality is optimized to compare by address. We do not want to compare
  // ID's because we can't be sure that they're from the same table.
  //
  // TODO: If the value is itself a view, then we should compare the views. One
  // way would be to compare `begin()`, if it's available.
  constexpr bool operator==(const interned_value& other) const noexcept {
    return value_ == other.value_;
  }
  constexpr bool operator!=(const interned_value& other) const noexcept {
    return value_ != other.value_;
  }

  // Inequality has to look at the values. Note that we crash on nullptr.
  constexpr bool operator<(const interned_value& other) const {
    return *value_ < *other.value_;
  }
  constexpr bool operator<=(const interned_value& other) const {
    return *value_ <= *other.value_;
  }
  constexpr bool operator>(const interned_value& other) const {
    return *value_ > *other.value_;
  }
  constexpr bool operator>=(const interned_value& other) const {
    return *value_ >= *other.value_;
  }

  // Heterogenous comparisons with types that are viewable as `value_t`.
  template<typename U>
  requires Viewable<value_t, U>
  friend constexpr auto operator<=>(const interned_value& lhs, const U& rhs) {
    return *lhs.value_ <=> rhs;
  }
  template<typename U>
  requires Viewable<value_t, U>
  friend constexpr auto operator<=>(const U& lhs, const interned_value& rhs) {
    return lhs <=> *rhs.value_;
  }

  friend std::ostream&
  operator<<(std::ostream& out, const interned_value& iv) {
    out << iv.id() << ": ";
    if (iv)
      out << *iv;
    else
      out << "{}";
    return out;
  }

private:
  const value_t* value_{};
  id_t id_{};
};

// Intern traits define the data structures used for the specified type.
//
// The general scheme is that the values are all in `lookup_by_id_`, while
// `lookup_by_value_` indexes them by key. These containers and the internal
// version of the value all use `arena_allocator` so that they can be stored in
// an arena chain. The clever trick is that the internal version can be cast to
// the external version by reinterpretation.
//
// - `value_t` is the type of the value that is interned, as seen from outside.
// - `arena_value_t` is the internal, arena-based version of `value_t`, which
// uses `arena_allocator` so that it can be reinterpreted as a `value_t`.
// - `id_t` is the type of the ID used to look up the value. It should be a
// scoped enum, and its underlying type should be no bigger than needed.
// - `interned_value_t` is the value/ID pair.
// - `key_t` is the type used to look up the value by value. Since the actual
// interned value is stored in `lookup_by_id_` but is looked up by
// `lookup_by_value_`, the key type has to work with the latter container but
// can reference the value in the former.
// - `lookup_by_id_t` is the container that stores the interned values, indexed
// by ID. Elements must never be moved, since address is identity.
// - `lookup_by_value_t` is the container that indexes the interned values. In
// principle, the key could be a value_t, but we use a key_t to avoid
// duplicating the value in the lookup_by_id_ container.
//
// TODO: Add arena size as a parameter.
template<typename T, SequentialEnum ID>
struct intern_traits<T, ID> {
  using value_t = T;
  using arena_value_t = T;
  using id_t = ID;
  using interned_value_t = interned_value<T, ID>;
  using key_t = indirect_hash_key<value_t>;
  using lookup_by_id_t = std::deque<value_t>;
  using lookup_by_value_t = std::unordered_map<key_t, id_t>;
};

// TODO: A deque is only being used because we want to be able to enlarge
// without moving, so we need something that amounts to a linked list. As an
// entirely optional optimization, write a class with a signature similar to
// vector or deque that is streamlined to only support appending, and to use
// one level of indirection for indexes.

// TODO: An entirely different scheme would be to store the values in a stable
// associative container, such as a map, and then use a deque to store pointers
// into the map, thus reversing the pattern. It would add an extra level of
// indirection to looking up by ID but possibly speed up the by-value lookups.
// It's unclear whether it's worth experimenting with this, and whether
// template magic could be used to allow both schemes in the same class.

// TODO: See if specializations can inherit from the primary template and
// just replace the types that changed.

// For strings, the default traits use an arena to hold the strings and the
// containers that index them. Strings are stored as `arena_string` but are
// returned as `std::string`, and are transparently looked up by
// `std::string_view`.
template<SequentialEnum ID>
struct intern_traits<std::string, ID> {
  using value_t = std::string;
  using arena_value_t = arena_string;
  using id_t = ID;
  using interned_value_t = interned_value<std::string, id_t>;
  using key_t = std::string_view;
  using lookup_by_id_t = arena_deque<arena_value_t>;
  using lookup_by_value_t = arena_map<key_t, id_t>;
};

// Intern table of `T` values, indexed by `ID`, using the traits `TR`.
//
// Instances can only be constructed through the `make` factory functions,
// which return a `std::shared_ptr`. Tables can chain to previous tables for
// lower ID ranges, allowing a subset to be shared.
template<typename T, SequentialEnum ID, typename TR = intern_traits<T, ID>>
class intern_table
    : public std::enable_shared_from_this<intern_table<T, ID, TR>> {
  using allow = restrict_intern_construction::allow;

public:
  using pointer = std::shared_ptr<intern_table>;
  using const_pointer = std::shared_ptr<const intern_table>;
  using value_t = typename TR::value_t;
  using arena_value_t = typename TR::arena_value_t;
  using id_t = typename TR::id_t;
  using interned_value_t = typename TR::interned_value_t;
  using key_t = typename TR::key_t;
  using lookup_by_id_t = typename TR::lookup_by_id_t;
  using lookup_by_value_t = typename TR::lookup_by_value_t;
  static_assert(sizeof(arena_value_t) == sizeof(value_t));

  // Effectively-private constructor.
  intern_table(allow, id_t min_id, id_t max_id, const const_pointer& next = {})
      : min_id_{min_id}, max_id_{max_id},
        lookup_by_id_{arena_construct<lookup_by_id_t>(arena_)},
        lookup_by_value_{arena_construct<lookup_by_value_t>(arena_)},
        next_{next} {
    assert(min_id);
    assert(min_id_ < max_id_);
    // TODO: Consider whether we should disable `next` if it's specified.
  }

  intern_table(const intern_table&) = delete;
  intern_table& operator=(const intern_table&) = delete;

  // Make intern table for a range of IDs. If `next` is specified, the new
  // table will chain to that one and will default its `min_id` to 1 past that
  // table's `max_id`. Otherwise, if unspecified, then it defaults to 1. If
  // `max_id` is unspecified, it defaults to the max of the underlying type.
  [[nodiscard]] static auto
  make(id_t min_id = id_t{}, id_t max_id = id_t{}, const_pointer next = {}) {
    if (next)
      min_id = next->max_id_ + 1;
    else if (!min_id)
      ++min_id;

    if (!max_id)
      max_id = static_cast<id_t>(
          std::numeric_limits<as_underlying_t<id_t>>::max() - 1);

    return std::make_shared<intern_table>(allow::ctor, min_id, max_id, next);
  }

  // Make next block of IDs, chaining to this table. The `min_id` is one past
  // this table's `max_id`. The `max_id`, if unspecified, defaults to the max
  // of the underlying type.
  [[nodiscard]] auto make_next(id_t max_id = id_t{}) const {
    return make(max_id_ + 1, max_id, this->shared_from_this());
  }

  // When full, `intern` fails.
  bool is_full() const { return sync.is_disabled(); }

  // Get interned value by ID. If not found, returns empty. Chains to next
  // table if necessary. See also: `operator()`.
  [[nodiscard]] interned_value_t
  get(id_t id, const lock& attestation = {}) const {
    attestation(sync);
    const value_t* found_value{};
    if (id >= min_id_ && id <= max_id_)
      found_value = find_by_id(id);
    else if (next_)
      return next_->get(id);
    return {allow::ctor, found_value, id};
  }

  // Get interned value by (transparent) value. If not found, returns empty.
  // Chains to next table if necessary. See also: `operator()` and `operator[]
  // const`.
  template<typename U>
  requires Viewable<T, U>
  [[nodiscard]] interned_value_t
  get(const U& value, const lock& attestation = {}) const {
    attestation(sync);
    id_t id{};
    const value_t* found_value{};
    if (auto id_ptr = find_opt(lookup_by_value_, key_t{value})) {
      id = *id_ptr;
      const auto index = *id - *min_id_;
      found_value = reinterpret_cast<const value_t*>(&lookup_by_id_[index]);
    } else if (next_)
      return next_->get(value, attestation);

    return {allow::ctor, found_value, id};
  }

  // Interns a value. If the value is already interned, returns the existing
  // one. Can only fail if the table is full, in which case it returns an empty
  // instance. The value can be a view that converts to `value_t`, perhaps
  // explicitly. See also: `operator[]`.
  template<typename U>
  requires Viewable<T, U>
  [[nodiscard]] interned_value_t
  intern(U&& value, const lock& attestation = {}) {
    attestation(sync);

    // If we found it, or if we have no more room, return what we have.
    auto iv = get(std::forward<U>(value), attestation);
    if (iv || sync.is_disabled()) return iv;

    extensible_arena::scope s{arena_};
    const auto id = static_cast<id_t>(*min_id_ + lookup_by_id_.size());
    auto& found_value = lookup_by_id_.emplace_back(std::forward<U>(value));
    lookup_by_value_.emplace(key_t{found_value}, id);

    // After the last entry, we don't need to sync anymore.
    if (id == max_id_) sync.disable();

    return {allow::ctor, reinterpret_cast<const value_t*>(&found_value), id};
  }

  // Get by ID. If not found, returns empty ID and value.
  [[nodiscard]] interned_value_t
  operator()(id_t id, const lock& attestation = {}) const {
    return get(id, attestation);
  }

  // Get by value. Does not intern. If not found, returns empty ID and value.
  template<typename U>
  requires Viewable<T, U>
  [[nodiscard]] interned_value_t
  operator()(U&& value, const lock& attestation = {}) const {
    return get(std::forward<U>(value), attestation);
  }

  // Get unique value by value, interning if necessary. In other words, pass in
  // a (possibly transparent) value and get the unique value back. Throws if
  // not found.
  template<typename U>
  requires Viewable<T, U>
  [[nodiscard]] const value_t& operator[](U&& value) const {
    auto iv = get(std::forward<U>(value));
    if (!iv) throw std::out_of_range("value not found");
    return iv.value();
  }

  // Get unique value by value, interning if necessary. In other words, pass in
  // a (possibly transparent) value and get the unique value back. If it was
  // not found, it is interned now. Throws if table is full.
  template<typename U>
  requires Viewable<T, U>
  [[nodiscard]] const value_t& operator[](U&& value) {
    auto iv = intern(std::forward<U>(value));
    if (!iv) throw std::out_of_range("value not found");
    return iv.value();
  }

  const breakable_synchronizer sync;

private:
  extensible_arena arena_{4096};
  const id_t min_id_;
  const id_t max_id_;
  lookup_by_id_t& lookup_by_id_;
  lookup_by_value_t& lookup_by_value_;
  const_pointer next_;

  // TODO: Add real or fake arena allocator, depending on traits. Then create
  // real or fake scopes in the methods that can allocate.

  // Find value by ID, returning address or `nullptr`.
  [[nodiscard]] const value_t* find_by_id(id_t id) const {
    const size_t index = *id - *min_id_;
    if (index >= lookup_by_id_.size()) return nullptr;
    return reinterpret_cast<const value_t*>(&lookup_by_id_[index]);
  }
};

// Inline constructors.
template<typename T, SequentialEnum ID>
template<typename TR>
interned_value<T, ID>::interned_value(const intern_table<T, ID, TR>& table,
    ID id, const lock& attestation) {
  *this = table.get(id, attestation);
}

template<typename T, SequentialEnum ID>
template<typename U, typename TR>
requires Viewable<T, U>
interned_value<T, ID>::interned_value(const intern_table<T, ID, TR>& table,
    U&& value, const lock& attestation) {
  *this = table.get(std::forward<U>(value), attestation);
}

template<typename T, SequentialEnum ID>
template<typename U, typename TR>
requires Viewable<T, U>
interned_value<T, ID>::interned_value(intern_table<T, ID, TR>& table,
    U&& value, const lock& attestation) {
  *this = table.intern(std::forward<U>(value), attestation);
}

}}} // namespace corvid::container::intern

// TODO: Replace find with opt_find.
