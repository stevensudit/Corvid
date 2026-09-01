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
#include <cstddef>
#include <format>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "../meta/concepts.h"
#include "../meta/formatting.h"
#include "string_literals.h"

// Format wrappers that let you use maps and variants with `std::format`.
//
// Wrapping a keyed collection in `enable_format` makes it a format arg whose
// fields are looked up by key at format time.
//
// For example, `std::format("{0:city}: {0:temperature:.2f}",
// enable_format{rec})` pulls both fields from one map, applying any nested
// spec to the looked-up value.
//
// A missing key throws, unless the wrapper was constructed with a stand-in
// value to report instead.
//
// The key does not have to be hardcoded in the format string: `{n}` (or `{}`
// under automatic numbering) reads it from another string-like arg, so
// `std::format("{0:{1}}", enable_format{m}, chosen)` selects the field at
// runtime.
//
// The collection can just as well be a `std::multimap` or
// `std::unordered_multimap`. Then the key selects the whole equal range of
// values, displayed through the std range formatter: `["NYC", "LA"]`.
//
// Wrapping a `std::variant` formats whichever alternative is active. Since
// `std::formatter` cannot legally be specialized for these std types directly,
// each wrapper composes the value and pairs with a formatter on the wrapper
// itself.
//
// These combine in one direction: a keyed collection can hold variants, and
// each looked-up value routes through the variant wrapper. A variant can hold
// a keyed collection too, but it then formats whole, through the std map
// formatter; key lookup does not reach through a variant. If you know you have
// a map inside a variant and want to look up values by key, extract a
// reference to it with `std::get` and wrap it in `enable_format`.

namespace corvid::strings { inline namespace format_wrapping {

// A keyed collection with string-like keys, whether ordered or hashed.
template<typename M>
concept KeyedCollection =
    StringViewLike<typename M::key_type> &&
    requires(const M& m, M::key_type k) {
      typename M::mapped_type;
      m.find(k);
      m.equal_range(k);
      m.end();
    };

// A keyed collection with unique keys, as opposed to a multimap-style one.
// Only the unique containers offer `at`.
template<typename M>
concept UniqueKeyedCollection =
    KeyedCollection<M> && requires(const M& m, M::key_type k) { m.at(k); };

#pragma region enable_format

// Allow `std::format` to take a value it cannot take directly, by wrapping.
//
// Each specialization composes the wrapped value and pairs with a
// `std::formatter` on the wrapper, following the band rule of formatting a
// wrapper rather than specializing `std::formatter` for a std type. Two are
// provided: one over a keyed collection, with key lookup in the format spec,
// and one over a `std::variant`, formatting the active alternative.
template<typename T>
class enable_format;

// Format wrapper over a keyed collection, erasing the difference between
// `std::map`, `std::unordered_map`, and their multi variants.
//
// Its `std::formatter` looks keys up at format time, so a single wrapper arg
// serves any number of fields:
//
//   std::format("{0:city}: {0:temperature:.2f}", enable_format{rec});
//
// The spec grammar is `key`, optionally followed by `:` and a nested spec for
// the looked-up value. The key may also be dynamic: `{n}` (or `{}` under
// automatic numbering) reads the key from another string-like arg, so
// `std::format("{0:{1}}", enable_format{m}, name)` selects the field at
// runtime.
//
// A missing key throws `std::format_error` by default. You can pass a default
// value in the constructor, which is used when the key is not found. That
// value is coerced into the collection's mapped type.
//
// A `std::variant` mapped type formats its active alternative by routing
// through the variant wrapper below, and every alternative must itself be
// formattable. A multi-keyed collection formats the whole equal range of
// values through the std range formatter, even when the range holds a single
// value, since the presentation follows from the container type rather than
// the runtime hit count; variant values are wrapped per element, so the two
// combine freely.
//
// Lookups arrive as string views, so the collection should ideally be
// transparent to `string_view` search, sparing a temporary key per field; the
// comparators and `string_map` / `string_unordered_map` aliases in
// "containers/core/transparent.h" make that easy. This is not a requirement,
// much less a dependency: an ordinary `std::map<std::string, T>` works, at
// the cost of that temporary.
//
// The wrapper holds the collection by reference and is meant to be created in
// the format call itself, not stored.
template<KeyedCollection M>
class enable_format<M> {
public:
  using key_type = M::key_type;
  using mapped_type = M::mapped_type;
  using char_type = char_type_of_t<key_type>;
  using view_t = std::basic_string_view<char_type>;

  constexpr explicit enable_format(const M& m) noexcept : map_{m} {}
  constexpr enable_format(const M& m, mapped_type missing_value)
      : map_{m}, missing_{std::move(missing_value)} {}

  // Look up `key`, returning the mapped value. When the key is absent, this
  // returns the `missing_value` as if all was well, or throws
  // `std::format_error` when none was provided.
  [[nodiscard]] constexpr const mapped_type& lookup(view_t key) const {
    const auto it = do_find(key);
    if (it != map_.end()) return it->second;
    if (missing_) return *missing_;
    throw std::format_error{"enable_format: key not found"};
  }

  // Return a view over the values mapped to `key`, possibly empty. The
  // missing-value stand-in does not apply here; see `missing`.
  [[nodiscard]] constexpr auto equal_values(view_t key) const {
    const auto [first, last] = do_equal_range(key);
    return std::ranges::subrange{first, last} | std::views::values;
  }

  // The missing-value stand-in, when one was provided.
  [[nodiscard]] constexpr const std::optional<mapped_type>&
  missing() const noexcept {
    return missing_;
  }

private:
  // Find `key`, going through a temporary `key_type` unless the collection
  // supports heterogeneous lookup (a transparent comparator or hash).
  [[nodiscard]] constexpr auto do_find(view_t key) const {
    if constexpr (requires { map_.find(key); })
      return map_.find(key);
    else
      return map_.find(key_type{key});
  }

  [[nodiscard]] constexpr auto do_equal_range(view_t key) const {
    if constexpr (requires { map_.equal_range(key); })
      return map_.equal_range(key);
    else
      return map_.equal_range(key_type{key});
  }

  const M& map_;
  std::optional<mapped_type> missing_;
};

// Format wrapper over a `std::variant`, formatting the active alternative.
//
// A `std::formatter` cannot legally be specialized for `std::variant` itself,
// so wrap: `std::format("{:.2f}", enable_format{v})`. The whole spec applies
// to whichever alternative is active; a spec the runtime alternative rejects
// throws `std::format_error`, and every alternative must be formattable. The
// keyed wrapper routes variant values through this one, which is also what
// lets a multi-keyed collection of variants format as a range.
template<Variant V>
class enable_format<V> {
public:
  const V& value;
};

// Deduction guides: the wrapped type picks the specialization.
template<KeyedCollection M>
enable_format(const M&) -> enable_format<M>;
template<KeyedCollection M>
enable_format(const M&, typename M::mapped_type) -> enable_format<M>;
template<Variant V>
enable_format(const V&) -> enable_format<V>;

#pragma endregion

}} // namespace corvid::strings::format_wrapping

// The keyed-collection formatter, on the collection's own code unit.
//
// Grammar: `{arg:key}`, `{arg:key:spec}`, and the dynamic-key forms
// `{arg:{n}}` / `{arg:{n}:spec}` (or `{}` for the key under automatic
// numbering).
//
// Keys cannot contain ':' or '}' or start with '{'; a key is  required, since
// a bare `{arg}` has no meaning yet.
//
// Because the key and the value's type are runtime facts, compile-time
// checking stops at this grammar; key lookup and nested-spec errors surface as
// `std::format_error` at format time. Within the nested spec, dynamic width
// and precision work with manual arg ids (`{0:city:>{2}}`); automatic `{}`
// ids there would renumber from zero and read the wrong args, so avoid them.
template<typename M, corvid::CharType CharT>
requires corvid::strings::KeyedCollection<M> &&
         std::same_as<CharT,
             typename corvid::strings::enable_format<M>::char_type>
// NOLINTNEXTLINE(bugprone-std-namespace-modification)
struct std::formatter<corvid::strings::enable_format<M>, CharT> {
private:
  using wrapper_t = corvid::strings::enable_format<M>;
  using mapped_type = wrapper_t::mapped_type;
  using view_t = std::basic_string_view<CharT>;
  using arg_value_t = corvid::meta::spec_parser<CharT>::arg_value_t;

public:
  constexpr auto parse(std::basic_format_parse_context<CharT>& ctx) {
    const view_t spec{ctx.begin(), ctx.end()};
    const auto cnt = spec.size();
    size_t ndx{};
    // Key: dynamic `{n}` / `{}`, or literal text up to ':' or '}'.
    if (ndx < cnt && spec[ndx] == CharT{'{'}) {
      key_arg_ = arg_value_t::make_from_parse(spec, ndx);
      key_arg_.register_arg_id(ctx);
    } else {
      const auto start = ndx;
      while (ndx < cnt && spec[ndx] != CharT{':'} && spec[ndx] != CharT{'}'})
        ++ndx;
      key_ = spec.substr(start, ndx - start);
      if (key_.empty()) throw std::format_error{"enable_format: key required"};
    }
    // Optional nested spec for the looked-up value.
    auto value_start = ndx;
    if (ndx < cnt && spec[ndx] == CharT{':'}) {
      value_start = ++ndx;
      ndx += corvid::meta::calc_nested_spec_size(spec.substr(value_start));
    }
    if (ndx >= cnt || spec[ndx] != CharT{'}'})
      throw std::format_error{"enable_format: unterminated spec"};
    // Keep the tail, not just the nested spec, so the value's own parse sees
    // the closing '}' the way it would under `std::format`.
    spec_tail_ = spec.substr(value_start);
    return ctx.begin() + ndx;
  }

  template<typename FormatContext>
  auto format(const wrapper_t& w, FormatContext& ctx) const {
    const auto key =
        key_arg_.is_dynamic()
            ? arg_value_t::get_dynamic_str(ctx, key_arg_.value)
            : key_;
    if constexpr (corvid::strings::UniqueKeyedCollection<M>) {
      const auto& v = w.lookup(key);
      if constexpr (corvid::meta::Variant<mapped_type>)
        return corvid::meta::format_with_spec(
            corvid::strings::enable_format{v}, spec_tail_, ctx);
      else
        return corvid::meta::format_with_spec(v, spec_tail_, ctx);
    } else {
      const auto values = w.equal_values(key);
      if (values.empty()) {
        const auto& missing = w.missing();
        if (!missing) throw std::format_error{"enable_format: key not found"};
        return corvid::meta::format_with_spec(
            wrap_variants(std::span<const mapped_type>(&*missing, 1)),
            spec_tail_, ctx);
      }
      return corvid::meta::format_with_spec(wrap_variants(values), spec_tail_,
          ctx);
    }
  }

private:
  // Route a range of variants through the variant wrapper so the std range
  // formatter can take it; any other range passes through unchanged.
  static constexpr auto wrap_variants(const auto& r) {
    if constexpr (corvid::meta::Variant<mapped_type>)
      return std::views::transform(r, [](const mapped_type& m) {
        return corvid::strings::enable_format{m};
      });
    else
      return r;
  }

  arg_value_t key_arg_;
  view_t key_;
  view_t spec_tail_;
};

// The variant formatter.
//
// The whole spec applies to the active alternative, with the same
// runtime-error caveats as the keyed formatter. When used as a range element,
// `set_debug_format` propagates to alternatives that support it, so strings
// inside variants quote the way plain strings do.
template<typename... Ts, corvid::CharType CharT>
requires(std::formattable<Ts, CharT> && ...)
// NOLINTNEXTLINE(bugprone-std-namespace-modification)
struct std::formatter<corvid::strings::enable_format<std::variant<Ts...>>,
    CharT> {
  constexpr void set_debug_format() noexcept { debug_ = true; }

  constexpr auto parse(std::basic_format_parse_context<CharT>& ctx) {
    const std::basic_string_view<CharT> spec{ctx.begin(), ctx.end()};
    const auto cnt = corvid::meta::calc_nested_spec_size(spec);
    // Keep the tail, not just our own spec, so the alternative's parse sees
    // the closing '}' the way it would under `std::format`.
    spec_tail_ = spec;
    return ctx.begin() + cnt;
  }

  template<typename FormatContext>
  auto format(const corvid::strings::enable_format<std::variant<Ts...>>& w,
      FormatContext& ctx) const {
    return std::visit(
        [&](const auto& alt) {
          return corvid::meta::format_with_spec(alt, spec_tail_, ctx, debug_);
        },
        w.value);
  }

private:
  std::basic_string_view<CharT> spec_tail_;
  bool debug_{};
};
