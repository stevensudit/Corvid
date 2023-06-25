// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2023 Steven Sudit
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
#include "../enums.h"
#include "../strings/cstring_view.h"

#include <deque>
#include <list>
#include <vector>
#include <unordered_map>

namespace corvid { inline namespace container { inline namespace intern {

using namespace sequence;

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

// Intern requirements:
// - Unique values are stored in a single location and never moved.
// - Lookup by ID, returning the unique value.
// - Looked up by copy of value, returning the ID and unique value.
// - Matching by value can be transparent. This means looking up by a view and
// potentially converting that view into the value type.

// Represents an interned value, which is a singleton that can be looked up by
// ID. Cannot be constructed directly, only by an `intern_table`.
//
// Does not reference the `intern_table` that defines it, so the ID can in
// principle be non-unique.
template<typename T, SequentialEnum ID>
class interned_value {
  using allow = restrict_intern_construction::allow;

public:
  using value_t = T;
  using id_t = ID;

  // Effectively-private constructor.
  constexpr interned_value(allow, const value_t& value, id_t id)
      : value_(&value), id_(id) {}
  constexpr interned_value(allow, const value_t* value, id_t id)
      : value_(value), id_(id) {}

  constexpr interned_value() noexcept = default;
  constexpr interned_value(const interned_value&) noexcept = default;
  constexpr interned_value(interned_value&&) noexcept = default;
  constexpr interned_value& operator=(
      const interned_value&) noexcept = default;
  constexpr interned_value& operator=(interned_value&&) noexcept = default;

  // TODO: Add constructors that take an `intern_table` and a value or ID.
  // These have to be forward-declared here and implemented later in the file.

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
  const value_t* value_ = {};
  id_t id_ = {};
};

//
// Implementation options:
// - An indexed container named `lookup_by_id_` is used to get the value from
// its ID. Something like a `std::vector` would work, but is generally ruled
// out by another consideration. A `std::map<ID,T>` would also work, but would
// not be optimal.
// - The obvious place to put the unique value is in `lookup_by_id_`, but that
// constrains us to a type that guarantees never moving elements around, which
// rules out `std::vector`. A `std::deque` works, but requires two lookups and
// has more overhead, so if we can get away with `std::vector`, we should.
// - If the value is a `std::string_view` then we could store the actual
// characters in a linked list of buffers of concatenated characters. The
// `lookup_by_id_` elements would point into that list.
// - An associative container named `lookup_by_value_` maps from a view of the
// value to the corresponding ID. While `std::unordered_map` works, the choice
// of key is a bit tricky.
// - We generally do not want to use the whole value as the key for
// `lookup_by_value_`, but instead a view into it. So if the value is a
// `std::string`, then `std::string_view` would work. More generally, we can
// use `indirect_hash_key_wrapper` over a reference to the `lookup_by_id_`
// element.
// - In the case of the value itself being something like `std::string_view`,
// we can point directly to the separately-stored characters, avoiding the need
// to touch `lookup_by_id_` when doing a value lookup; which would allow us to
// use a `std::vector` for `lookup_by_id_`.
// - Given all of these choices, we need a traits class.

template<typename T, SequentialEnum ID>
struct intern_traits {
  using value_t = T;
  using id_t = ID;
  using interned_value_t = interned_value<T, ID>;
  using key_t = indirect_hash_key<value_t>;
  using lookup_by_id_t = std::deque<value_t>;
  using lookup_by_value_t = std::unordered_map<key_t, id_t>;
  using additional_storage = void;
};

// TODO: See if specializations can inherit from the primary template and just
// replace the types that changed.

template<SequentialEnum ID>
struct intern_traits<std::string, ID> {
  using value_t = std::string;
  using id_t = ID;
  using interned_value_t = interned_value<std::string, ID>;
  using key_t = std::string_view;
  using lookup_by_id_t = std::deque<value_t>;
  using lookup_by_value_t = std::unordered_map<key_t, id_t>;
  using additional_storage = void;
};

template<SequentialEnum ID>
struct intern_traits<cstring_view, ID> {
  using value_t = cstring_view;
  using id_t = ID;
  using interned_value_t = interned_value<cstring_view, ID>;
  using key_t = value_t;
  using lookup_by_id_t = std::vector<value_t>;
  using lookup_by_value_t = std::unordered_map<key_t, id_t>;
  using additional_storage = std::list<std::vector<char>>;
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
  using id_t = typename TR::id_t;
  using interned_value_t = typename TR::interned_value_t;
  using key_t = typename TR::key_t;
  using lookup_by_id_t = typename TR::lookup_by_id_t;
  using lookup_by_value_t = typename TR::lookup_by_value_t;
  using additional_storage = typename TR::additional_storage;

  explicit intern_table(allow, id_t min_id, id_t max_id,
      const const_pointer& next = {})
      : min_id_(min_id), max_id_(max_id), next_(next) {
    assert(min_id_ < max_id_);
  }

  intern_table(const intern_table&) = delete;
  intern_table& operator=(const intern_table&) = delete;

  [[nodiscard]] static auto
  make(id_t min_id = id_t{}, id_t max_id = id_t{}, const_pointer next = {}) {
    if (next)
      min_id = next->max_id_ + 1;
    else if (min_id == id_t{})
      ++min_id;
    if (max_id == id_t{})
      max_id = static_cast<id_t>(
          std::numeric_limits<as_underlying_t<id_t>>::max() - 1);

    return std::make_shared<intern_table>(allow::ctor, min_id, max_id, next);
  }

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
      found_value = next_->find_value(id, attestation);
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
    if (auto it = lookup_by_value_.find(key_t{value});
        it != lookup_by_value_.end())
    {
      // TODO: Optimize for case where `value_t` is `key_t` so we can just use
      // what's in `first` directly.
      id = it->second;
      found_value = &lookup_by_id_[*id - *min_id_];
    } else if (next_) {
      return next_->get(value, attestation);
    }
    return {allow::ctor, found_value, id};
  }

  // Interns a value. If the value is already interned, returns the existing
  // one. Can only fail if the table is full. The value can be a view that
  // converts to `value_t`, perhaps explicitly. See also: `operator[]`.
  template<typename U>
  requires Viewable<T, U>
  [[nodiscard]] interned_value_t
  intern(U&& value, const lock& attestation = {}) {
    attestation(sync);
    auto iv = get(std::forward<U>(value), attestation);
    // If we found it, or if we have no more room, return what we have.
    if (iv || sync.is_disabled()) return iv;
    auto id = static_cast<id_t>(*min_id_ + lookup_by_id_.size());
    lookup_by_id_.emplace_back(std::forward<U>(value));
    auto& found_value = lookup_by_id_.back();
    lookup_by_value_.emplace(key_t{found_value}, id);
    // After the last entry, we don't need to sync anymore.
    if (id == max_id_) sync.disable();
    return {allow::ctor, found_value, id};
  }

  // Get by ID.
  [[nodiscard]] interned_value_t
  operator()(id_t id, const lock& attestation = {}) const {
    return get(id, attestation);
  }

  // Get by value.
  template<typename U>
  requires Viewable<T, U>
  [[nodiscard]] interned_value_t
  operator()(U&& value, const lock& attestation = {}) const {
    return get(std::forward<U>(value), attestation);
  }

  // Get unique value by value. In other words, pass in a (possibly
  // transparent) value and get the unique value back. Throws if not
  // found.
  template<typename U>
  requires Viewable<T, U>
  [[nodiscard]] const value_t& operator[](U&& value) const {
    auto iv = get(std::forward<U>(value));
    if (!iv) throw std::out_of_range("value not found");
    return iv.value();
  }

  // Get unique value by value, interning if necessary. In other words, pass in
  // a value (which can be a view) and get the unique value back. If
  // it was not found, it is interned now. Throws if table is full.
  template<typename U>
  requires Viewable<T, U>
  [[nodiscard]] const value_t& operator[](U&& value) {
    auto iv = intern(std::forward<U>(value));
    if (!iv) throw std::out_of_range("value not found");
    return iv.value();
  }

  const breakable_synchronizer sync;

private:
  const id_t min_id_;
  const id_t max_id_;
  lookup_by_id_t lookup_by_id_;
  lookup_by_value_t lookup_by_value_;
  const_pointer next_;

  // TODO: Support cstring_view.

  // Find value by ID, return address or `nullptr`.
  [[nodiscard]] const value_t& find_by_id(id_t id) const {
    const size_t index = *id - *min_id_;
    if (index < lookup_by_id_.size()) return &lookup_by_id_[index];
    return nullptr;
  }
};
}}} // namespace corvid::container::intern
