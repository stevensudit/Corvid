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
#pragma once
#include <cassert>
#include <concepts>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <string_view>
#include <utility>

#include "../meta/concepts.h"

namespace corvid { inline namespace stringviewwrapper {

#pragma region string_view_wrapper

// String view wrapper
//
// CRTP base for value types that hold a `std::basic_string_view` and add the
// things `std::string_view` lacks: a `null` vs `empty` distinction, and a
// `std::optional`-like interface.
//
// It owns the view by composition (no inheritance from `std::string_view`),
// forwards the read-only view API, and leaves construction to the child class
// so each can enforce its own invariants.
//
// `child_t` is the concrete class that inherits this base (the curiously
// recurring parameter), and `char_t` selects the character type. Children
// include `opt_string_view` (unconstrained) and `cstring_view` (guarantees
// zero termination); `enum_name` is another example, outside of the `strings`
// namespace.
//
// The split between base and child is deliberate:
//
// - Read and search operations take anything viewable as `std::string_view`
//   and live here; so does `same`, which imposes this null-vs-empty regime
//   on any view.
// - Operations that hand back a value keep the `child_t` type rather than
//   degrading to `std::string_view` (`value`, the child-returning `value_or`,
//   and `as_optional`), so the result still carries the child's invariants;
//   `swap` is likewise `child_t`-typed so a constrained child can't be
//   corrupted by a sibling.
// - Operations that reslice the view (`substr`, `remove_prefix`,
// `remove_suffix`) are not universally safe (they break `enum_name`, and
//   suffix removal breaks `cstring_view`), so they belong in the children.
template<typename Child, CharType Char = char>
class string_view_wrapper {
#pragma region Types
public:
  using child_t = Child;
  using char_t = Char;
  using view_t = std::basic_string_view<char_t>;

  using traits_type = view_t::traits_type;
  using value_type = view_t::value_type;
  using pointer = view_t::pointer;
  using const_pointer = view_t::const_pointer;
  using reference = view_t::reference;
  using const_reference = view_t::const_reference;
  using const_iterator = view_t::const_iterator;
  using iterator = view_t::iterator;
  using const_reverse_iterator = view_t::const_reverse_iterator;
  using reverse_iterator = view_t::reverse_iterator;
  using size_type = view_t::size_type;
  using difference_type = view_t::difference_type;
  static constexpr size_type npos = view_t::npos;

#pragma endregion
#pragma region Passthrough

  [[nodiscard]] constexpr auto begin() const noexcept { return sv_.begin(); }
  [[nodiscard]] constexpr auto end() const noexcept { return sv_.end(); }
  [[nodiscard]] constexpr auto cbegin() const noexcept { return sv_.cbegin(); }
  [[nodiscard]] constexpr auto cend() const noexcept { return sv_.cend(); }
  [[nodiscard]] constexpr auto rbegin() const noexcept { return sv_.rbegin(); }
  [[nodiscard]] constexpr auto rend() const noexcept { return sv_.rend(); }
  [[nodiscard]] constexpr auto crbegin() const noexcept {
    return sv_.crbegin();
  }
  [[nodiscard]] constexpr auto crend() const noexcept { return sv_.crend(); }

  [[nodiscard]] constexpr auto& operator[](size_type pos) const {
    return sv_[pos];
  }
  [[nodiscard]] constexpr auto& at(size_type pos) const { return sv_.at(pos); }
  [[nodiscard]] constexpr auto& front() const { return sv_.front(); }
  [[nodiscard]] constexpr auto& back() const { return sv_.back(); }
  [[nodiscard]] constexpr auto data() const noexcept { return sv_.data(); }

  [[nodiscard]] constexpr auto size() const noexcept { return sv_.size(); }
  [[nodiscard]] constexpr auto length() const noexcept { return sv_.length(); }
  [[nodiscard]] constexpr auto max_size() const noexcept {
    return sv_.max_size();
  }
  [[nodiscard]] constexpr bool empty() const noexcept { return sv_.empty(); }

  constexpr auto copy(char_t* dest, size_type count, size_type pos = 0) const {
    return sv_.copy(dest, count, pos);
  }

  template<typename... Args>
  [[nodiscard]] constexpr auto compare(Args&&... args) const noexcept {
    return sv_.compare(std::forward<Args>(args)...);
  }
  template<typename... Args>
  [[nodiscard]] constexpr auto find(Args&&... args) const noexcept {
    return sv_.find(std::forward<Args>(args)...);
  }
  template<typename... Args>
  [[nodiscard]] constexpr auto rfind(Args&&... args) const noexcept {
    return sv_.rfind(std::forward<Args>(args)...);
  }
  template<typename... Args>
  [[nodiscard]] constexpr auto find_first_of(Args&&... args) const noexcept {
    return sv_.find_first_of(std::forward<Args>(args)...);
  }
  template<typename... Args>
  [[nodiscard]] constexpr auto find_last_of(Args&&... args) const noexcept {
    return sv_.find_last_of(std::forward<Args>(args)...);
  }
  template<typename... Args>
  [[nodiscard]] constexpr auto
  find_first_not_of(Args&&... args) const noexcept {
    return sv_.find_first_not_of(std::forward<Args>(args)...);
  }
  template<typename... Args>
  [[nodiscard]] constexpr auto
  find_last_not_of(Args&&... args) const noexcept {
    return sv_.find_last_not_of(std::forward<Args>(args)...);
  }

#pragma endregion
#pragma region Underlying

  [[nodiscard]] constexpr view_t view() const noexcept { return sv_; }
  [[nodiscard]] constexpr operator view_t() const noexcept { return sv_; }

#pragma endregion
#pragma region Null

  // Whether `data` is `nullptr`, in addition to being `empty`.
  [[nodiscard]] constexpr bool null() const noexcept { return !sv_.data(); }

  // Like `operator==` but distinguishes `null` from `empty`, imposing this
  // null regime on any view it is compared against.
  [[nodiscard]] constexpr bool same(view_t v) const noexcept {
    return view() == v && null() == !v.data();
  }

#pragma endregion
#pragma region Optional

  // Whether a value is present, meaning not null.
  [[nodiscard]] constexpr bool has_value() const noexcept { return !null(); }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return has_value();
  }
  // The value as the `Child` itself, not a bare `SV`, so the child's
  // invariants carry through `value`, `operator*`, and `operator->`. As with
  // `std::optional`, `value` throws `std::bad_optional_access` when `null`,
  // while `operator*` and `operator->` are undefined when `null` (asserted in
  // debug builds).
  [[nodiscard]] constexpr const child_t& value() const {
    if (null()) throw std::bad_optional_access{};
    return as_child();
  }
  [[nodiscard]] constexpr const child_t& operator*() const noexcept {
    assert(!null());
    return as_child();
  }
  [[nodiscard]] constexpr const child_t* operator->() const noexcept {
    assert(!null());
    return &as_child();
  }

  // Value if present, else `v`. Overloaded on the argument: a
  // `std::string_view` fallback returns a `std::string_view`, while a
  // same-typed child fallback returns the child, so a fallback that meets the
  // child's invariants yields a result that does too.
  [[nodiscard]] constexpr view_t value_or(view_t v) const noexcept {
    return has_value() ? sv_ : v;
  }
  template<std::same_as<child_t> D>
  [[nodiscard]] constexpr D value_or(const D& v) const noexcept {
    return has_value() ? as_child() : v;
  }
  // Convert to `std::optional<child_t>`: empty when absent, else the child.
  // This is a named method, not a conversion operator, because `std::optional`
  // would prefer its own constructor (wrapping the wrapper into a present
  // optional) over the operator, making the operator dead.
  [[nodiscard]] constexpr std::optional<child_t> as_optional() const noexcept {
    return has_value() ? std::optional<child_t>{as_child()} : std::nullopt;
  }

  // Reset to the child's default value. Routing through `Child{}` lets the
  // child pick that default rather than forcing a bare empty view, and a child
  // with no default (such as `enum_name`) makes this ill-formed when used.
  constexpr void reset() noexcept { sv_ = child_t{}.view(); }

#pragma endregion
#pragma region Swap

  // Swap with another instance of the same concrete type.
  constexpr void swap(child_t& other) noexcept { sv_.swap(other.sv_); }

#pragma endregion
#pragma region Comparison

  // Content comparison against anything viewable as `std::string_view`. `null`
  // and `empty` compare equal here; use `same` to tell them apart.
  template<typename T>
  requires std::convertible_to<const T&, view_t>
  [[nodiscard]] friend constexpr bool
  operator==(const child_t& l, const T& r) noexcept {
    return l.view() == view_t{r};
  }
  template<typename T>
  requires std::convertible_to<const T&, view_t>
  [[nodiscard]] friend constexpr auto
  operator<=>(const child_t& l, const T& r) noexcept {
    return l.view() <=> view_t{r};
  }

  friend std::basic_ostream<char_t>&
  operator<<(std::basic_ostream<char_t>& os, const child_t& d) {
    return os << d.view();
  }

#pragma endregion
#pragma region Construction
protected:
  // NOLINTBEGIN(bugprone-crtp-constructor-accessibility)
  constexpr explicit string_view_wrapper(view_t sv = view_t{}) noexcept
      : sv_{sv} {}

  // Null-safe pointer construction, so a child that accepts raw pointers
  // routes through here instead of `view_t`'s pointer constructors, which
  // crash (or are undefined) on a null pointer. A null `psz` becomes the null
  // instance; a null `ps` with a non-zero `l` is treated as zero-length.
  constexpr explicit string_view_wrapper(const char_t* psz)
      : sv_{from_ptr(psz)} {}
  constexpr string_view_wrapper(const char_t* ps, size_type l)
      : sv_{from_ptr(ps, l)} {}
  // NOLINTEND(bugprone-crtp-constructor-accessibility)

#pragma endregion
#pragma region Data members

  view_t sv_{};

#pragma endregion
#pragma region Helpers
protected:
  // Downcast to the concrete child (CRTP).
  [[nodiscard]] constexpr const child_t& as_child() const noexcept {
    return static_cast<const child_t&>(*this);
  }

  // Null pointer maps to the null (default) instance.
  [[nodiscard]] static constexpr view_t from_ptr(const char_t* psz) {
    return psz ? view_t{psz} : view_t{};
  }
  // Null pointer is always zero-length.
  [[nodiscard]] static constexpr view_t
  from_ptr(const char_t* ps, size_type l) {
    if (!ps && l) l = 0;
    return view_t{ps, l};
  }

#pragma endregion
};

#pragma endregion
#pragma region Formatter support

// Whether `T` is a concrete `string_view_wrapper` child, identified by
// deriving from the CRTP base instantiated on itself. Gates the
// `std::formatter` below.
template<typename T>
concept StringViewWrapperChild =
    std::derived_from<T, string_view_wrapper<T, typename T::char_t>>;

#pragma endregion

}} // namespace corvid::stringviewwrapper

// The wrappers forward `begin`/`end`, so without this they would be claimed by
// the std range formatter and print as a list of chars. Disabling their range
// format leaves the string formatter below as the only match.
template<corvid::StringViewWrapperChild W>
constexpr std::range_format std::format_kind<W> = std::range_format::disabled;

// Formatter for any `string_view_wrapper` child (opt_string_view,
// cstring_view, enum_name), narrow or wide.
//
// Inherits the `std::basic_string_view` formatter for the full spec grammar
// (fill, align, width, precision, and the `?` debug spec) and formats the
// wrapper's view, so a wrapper renders exactly like the string_view it holds:
// `{}` prints the contents, `{:?}` prints the quoted, escaped debug form.
//
// A null wrapper (null distinct from empty) stays transparent under `{}`,
// printing as empty, and prints as the unquoted marker `(null)` under `{:?}`.
// The marker is distinct from an empty string (`""`) and from a string whose
// contents are those letters (`"(null)"`). Fill, align, width, and precision
// apply to the marker too: a second formatter captures the spec with its `?`
// stripped and formats the marker through that, so the base's debug quoting is
// bypassed while its padding is reused. A dynamic width or precision combined
// with the debug spec (`{:{}?}`) is rejected with a `std::format_error`: its
// argument is bound to the real parse context, which the local marker parse
// cannot read, so the marker could not honor it, and silently dropping the
// padding would corrupt tabular output. The rejection is value-independent
// (null or not); to get dynamic width with debug on a known non-null wrapper,
// format its `view()` directly.
//
// Only a context whose char type matches the wrapper's is claimed; cross-type
// transcoding is out of scope.
template<corvid::StringViewWrapperChild W, corvid::CharType CharT>
requires std::same_as<CharT, typename W::char_t>
struct std::formatter<W, CharT>
    : std::formatter<std::basic_string_view<CharT>, CharT> {
  using base = std::formatter<std::basic_string_view<CharT>, CharT>;

  // The range and map formatters turn on element debug through this rather
  // than a spec `?`, so mirror their call into `debug_` to keep null elements
  // rendering as `(null)` inside a range. A container enables debug here only
  // when no element spec was given, so it never carries element width; a bare
  // (empty-spec) marker is correct. Guard against clobbering a width that a
  // `?`-bearing spec already captured.
  constexpr void set_debug_format() {
    const bool was_debug = debug_;
    debug_ = true;
    base::set_debug_format();
    if (!was_debug) parse_marker({});
  }

  constexpr auto parse(auto& ctx) {
    auto it = base::parse(ctx);
    // The debug `?` type is terminal, so a `?` just before the spec end means
    // debug mode, the only case that changes how a null is rendered. The
    // marker matters only in debug mode, so capture its spec only then.
    if (it != ctx.begin() && *std::prev(it) == static_cast<CharT>('?')) {
      debug_ = true;
      // Capture the spec without the `?` so a null can be padded as the bare
      // `(null)` marker, reusing the base's fill/align/width without its debug
      // quoting. A dynamic `{...}` width or precision binds its arg to the
      // real context, which the local marker parse cannot read, so the marker
      // could not honor it; reject the spec rather than silently drop the
      // padding and corrupt tabular output. (Fill cannot be `{`, so any `{` is
      // a dynamic arg.)
      std::basic_string_view<CharT> spec{std::to_address(ctx.begin()),
          static_cast<size_t>(std::prev(it) - ctx.begin())};
      if (spec.find(static_cast<CharT>('{')) != spec.npos)
        throw std::format_error(
            "dynamic width or precision is unsupported "
            "with the debug spec for a string_view_wrapper");
      parse_marker(spec);
    }
    return it;
  }

  template<typename FormatContext>
  auto format(const W& w, FormatContext& ctx) const {
    if (w.null() && debug_) {
      static constexpr CharT marker[]{CharT('('), CharT('n'), CharT('u'),
          CharT('l'), CharT('l'), CharT(')')};
      return marker_.format(std::basic_string_view<CharT>{marker, 6}, ctx);
    }
    return base::format(w.view(), ctx);
  }

private:
  // Parse `spec` (the padding spec, no `?`) into the marker formatter.
  constexpr void parse_marker(std::basic_string_view<CharT> spec) {
    std::basic_format_parse_context<CharT> ctx{spec};
    marker_.parse(ctx);
  }

  bool debug_{false};
  base marker_;
};
