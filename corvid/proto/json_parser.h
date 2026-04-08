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

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "../strings/concat_join.h"
#include "../strings/conversion.h"
#include "../strings/trimming.h"

// Strict JSON parser, non-owning value views, and compact JSON writer.
namespace corvid { inline namespace proto { inline namespace json {

size_t npos = std::string_view::npos;

// Kind of JSON value represented by a `json_value_view`.
enum class json_kind : uint8_t {
  invalid,
  null,
  boolean,
  number,
  string,
  array,
  object,
};

// Parse error category reported in `json_error`.
enum class json_errc : uint8_t {
  none,
  unexpected_end,
  invalid_token,
  invalid_literal,
  invalid_number,
  invalid_string,
  invalid_escape,
  invalid_unicode_escape,
  invalid_surrogate_pair,
  expected_key,
  expected_colon,
  expected_value,
  expected_comma_or_end,
  trailing_data,
  depth_exceeded,
};

// Parse error plus byte offset within the original input.
struct json_error {
  json_errc code{json_errc::none};
  size_t offset{};

  constexpr void clear() {
    code = json_errc::none;
    offset = 0;
  }

  [[nodiscard]] constexpr static bool
  fail(json_error* out_err, json_errc code_, size_t offset_) {
    if (out_err) return out_err->fail(code_, offset_);
    return false;
  }

  [[nodiscard]] constexpr bool fail(json_errc code_, size_t offset_) {
    code = code_;
    offset = offset_;
    return false;
  }
};

// Parser configuration. `max_depth` guards against adversarial nesting.
struct json_parse_options {
  size_t max_depth{64};
};

// String wrapper for bytes already known to be safe inside JSON quotes.
//
// This bypasses the usual escape scan in `json_writer`, so callers must
// ensure the contents contain no characters that would require JSON
// escaping.
struct json_trusted {
  std::string_view value;

  [[nodiscard]] constexpr operator std::string_view() const noexcept {
    return value;
  }
};

// Fwd.
class json_object_view;
class json_array_view;

// Non-owning view of one validated JSON value.
//
// The view borrows a slice of the original JSON source, so that source must
// outlive the view and any derived object/array subviews.
class json_value_view {
public:
  constexpr json_value_view() = default;
  constexpr explicit json_value_view(std::string_view source,
      json_kind kind) noexcept
      : source_{source}, kind_{kind} {
    assert(!source.empty() || is(json_kind::invalid));
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return kind_ != json_kind::invalid;
  }
  [[nodiscard]] constexpr bool empty() const noexcept {
    return kind_ == json_kind::invalid;
  }
  [[nodiscard]] constexpr json_kind kind() const noexcept { return kind_; }
  [[nodiscard]] constexpr std::string_view source() const noexcept {
    return source_;
  }

  [[nodiscard]] constexpr bool is(json_kind kind) const noexcept {
    return kind_ == kind;
  }
  [[nodiscard]] constexpr bool is_null() const noexcept {
    return is(json_kind::null);
  }
  [[nodiscard]] constexpr bool is_bool() const noexcept {
    return is(json_kind::boolean);
  }
  [[nodiscard]] constexpr bool is_number() const noexcept {
    return is(json_kind::number);
  }
  [[nodiscard]] constexpr bool is_string() const noexcept {
    return is(json_kind::string);
  }
  [[nodiscard]] constexpr bool is_array() const noexcept {
    return is(json_kind::array);
  }
  [[nodiscard]] constexpr bool is_object() const noexcept {
    return is(json_kind::object);
  }

  [[nodiscard]] std::optional<bool> as_bool() const noexcept {
    if (!is_bool()) return std::nullopt;
    if (source_ == "true") return true;
    if (source_ == "false") return false;
    return std::nullopt;
  }

  template<std::integral T>
  requires(!std::same_as<std::remove_cvref_t<T>, bool>)
  [[nodiscard]] std::optional<T> as_number() const noexcept {
    if (!is_number()) return std::nullopt;
    T value{};
    auto [ptr, ec] = std::from_chars(source_.data(),
        source_.data() + source_.size(), value);
    if (ec != std::errc{} || ptr != source_.data() + source_.size())
      return std::nullopt;
    return value;
  }

  template<std::floating_point T>
  [[nodiscard]] std::optional<T> as_number() const noexcept {
    if (!is_number()) return std::nullopt;
    T value{};
    auto [ptr, ec] = strings::std_from_chars(source_.data(),
        source_.data() + source_.size(), value);
    if (ec != std::errc{} || ptr != source_.data() + source_.size())
      return std::nullopt;
    return value;
  }

  // If this is a plain JSON string (properly quoted and containing no escape
  // sequences), returns the inner `std::string_view`, which may be empty.
  // Otherwise, returns `std::nullopt`.
  [[nodiscard]] std::optional<std::string_view>
  string_view_if_plain() const noexcept {
    if (!is_string() || source_.size() < 2) return std::nullopt;
    auto inner = source_.substr(1, source_.size() - 2);
    if (inner.contains('\\')) return std::nullopt;
    return inner;
  }

  // Decode a JSON string into `out`, including escape and Unicode handling.
  // Returns false when this value is not a string.
  [[nodiscard]] constexpr bool decode_string(std::string& out) const;

  // If this value is a JSON object, returns a view into it.
  // Otherwise, returns a default-constructed empty view.
  [[nodiscard]] constexpr json_object_view as_object() const noexcept;

  // If this value is a JSON array, returns a view into it.
  // Otherwise, returns a default-constructed empty view.
  [[nodiscard]] constexpr json_array_view as_array() const noexcept;

private:
  std::string_view source_;
  json_kind kind_{json_kind::invalid};
};

// Non-owning view of a validated JSON array.
class json_array_view {
public:
  // Forward iterator over the immediate elements of a validated JSON array.
  class iterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = json_value_view;
    using difference_type = std::ptrdiff_t;
    using reference = value_type;
    using pointer = void;

    constexpr iterator() = default;

    [[nodiscard]] constexpr value_type operator*() const noexcept {
      return current_;
    }
    constexpr iterator& operator++();
    constexpr iterator operator++(int) {
      auto copy = *this;
      ++*this;
      return copy;
    }

    [[nodiscard]] constexpr bool operator==(
        const iterator& other) const noexcept {
      return owner_ == other.owner_ && pos_ == other.pos_;
    }

  private:
    friend class json_array_view;

    constexpr explicit iterator(const json_array_view* owner,
        size_t pos) noexcept
        : owner_{owner}, pos_{pos} {
      load_current();
    }

    // Parse the element starting at `pos_` and cache it in `current_`.
    constexpr void load_current();

    const json_array_view* owner_{};
    size_t pos_{npos};
    size_t next_pos_{npos};
    json_value_view current_;
  };

  constexpr json_array_view() = default;
  constexpr explicit json_array_view(std::string_view source) noexcept
      : source_{source} {}

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return !source_.empty();
  }
  [[nodiscard]] constexpr bool empty_source() const noexcept {
    return source_.empty();
  }
  [[nodiscard]] constexpr std::string_view source() const noexcept {
    return source_;
  }

  [[nodiscard]] constexpr iterator begin() const noexcept;
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  [[nodiscard]] constexpr iterator end() const noexcept { return {}; }

private:
  std::string_view source_;
};

// Non-owning view of a validated JSON object.
//
// Lookup is linear and iteration preserves the original field order.
class json_object_view {
public:
  // One object member as two JSON subviews: key string and value.
  struct entry {
    json_value_view key;
    json_value_view value;
  };

  // Forward iterator over the immediate members of a validated JSON object.
  class iterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = entry;
    using difference_type = std::ptrdiff_t;
    using reference = value_type;
    using pointer = void;

    constexpr iterator() = default;

    [[nodiscard]] constexpr value_type operator*() const noexcept {
      return current_;
    }
    constexpr iterator& operator++();
    constexpr iterator operator++(int) {
      auto copy = *this;
      ++*this;
      return copy;
    }

    [[nodiscard]] constexpr bool operator==(
        const iterator& other) const noexcept {
      return owner_ == other.owner_ && pos_ == other.pos_;
    }

  private:
    friend class json_object_view;

    constexpr explicit iterator(const json_object_view* owner,
        size_t pos) noexcept
        : owner_{owner}, pos_{pos} {
      load_current();
    }

    // Parse the member starting at `pos_` and cache it in `current_`.
    constexpr void load_current();

    const json_object_view* owner_{};
    size_t pos_{npos};
    size_t next_pos_{npos};
    entry current_{};
  };

  constexpr json_object_view() = default;
  constexpr explicit json_object_view(std::string_view source) noexcept
      : source_{source} {}

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return !source_.empty();
  }
  [[nodiscard]] constexpr bool empty_source() const noexcept {
    return source_.empty();
  }
  [[nodiscard]] constexpr std::string_view source() const noexcept {
    return source_;
  }

  [[nodiscard]] constexpr iterator begin() const noexcept;
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  [[nodiscard]] constexpr iterator end() const noexcept { return {}; }

  // Return the value for the first matching key, or an empty view if absent.
  [[nodiscard]] constexpr json_value_view find(std::string_view key) const;
  // Convenience typed getters layered on top of `find`.
  [[nodiscard]] constexpr std::optional<bool> get_bool(
      std::string_view key) const;

  template<typename T>
  requires(std::integral<T> || std::floating_point<T>)
  [[nodiscard]] constexpr std::optional<T>
  get_number(std::string_view key) const {
    return find(key).template as_number<T>();
  }

  // If the value for `key` is a plain JSON string, returns the inner
  // `std::string_view`. Otherwise, returns `std::nullopt`.
  [[nodiscard]] constexpr std::optional<std::string_view>
  get_string_view_if_plain(std::string_view key) const {
    return find(key).string_view_if_plain();
  }
  // Decode the value for `key` as a JSON string into `out`. Returns false if
  // the value is not a string or if decoding fails.
  [[nodiscard]] constexpr bool
  get_string(std::string_view key, std::string& out) const {
    return find(key).decode_string(out);
  }
  // If the value for `key` is a JSON object or array, returns a view into
  // it. Otherwise, returns an empty view.
  [[nodiscard]] constexpr json_object_view get_object(
      std::string_view key) const {
    return find(key).as_object();
  }
  // If the value for `key` is a JSON array, returns a view into it.
  // Otherwise, returns an empty view.
  [[nodiscard]] constexpr json_array_view get_array(
      std::string_view key) const {
    return find(key).as_array();
  }

private:
  std::string_view source_;
};

namespace detail {

// JSON delimiters.
inline constexpr strings::delim json_ws{" \t\r\n"};

// Cursor into JSON input for parsing.
struct json_cursor {
  std::string_view input;
  size_t pos{};

  [[nodiscard]] constexpr bool at_end() const noexcept {
    return pos >= input.size();
  }

  [[nodiscard]] constexpr char operator*() const noexcept {
    return input[pos];
  }

  constexpr json_cursor& operator++() noexcept {
    ++pos;
    return *this;
  }

  constexpr json_cursor operator++(int) noexcept {
    auto tmp = *this;
    ++*this;
    return tmp;
  }

  constexpr json_cursor& operator--() noexcept {
    --pos;
    return *this;
  }

  constexpr json_cursor operator--(int) noexcept {
    auto tmp = *this;
    --*this;
    return tmp;
  }

  constexpr json_cursor& operator+=(size_t n) noexcept {
    pos += n;
    return *this;
  }

  constexpr json_cursor& operator-=(size_t n) noexcept {
    pos -= n;
    return *this;
  }

  [[nodiscard]] constexpr char operator[](std::ptrdiff_t n) const noexcept {
    return input[static_cast<size_t>(static_cast<std::ptrdiff_t>(pos) + n)];
  }

  constexpr char next() noexcept { return input[pos++]; }

  [[nodiscard]] constexpr auto
  substr(size_t pos, size_t count = npos) const noexcept {
    return input.substr(pos, count);
  }

  constexpr auto& skip_ws() {
    const auto rest = substr(pos);
    const auto trimmed = strings::trim_left(rest, json_ws);
    pos += rest.size() - trimmed.size();
    return *this;
  }
};

[[nodiscard]] inline bool is_digit(char ch) noexcept {
  return ch >= '0' && ch <= '9';
}

[[nodiscard]] inline bool is_lc_hex_alpha(char ch) noexcept {
  return (ch >= 'a' && ch <= 'f');
}

[[nodiscard]] inline bool is_uc_hex_alpha(char ch) noexcept {
  return (ch >= 'A' && ch <= 'F');
}

[[nodiscard]] inline bool is_hex_digit(char ch) noexcept {
  return is_digit(ch) || is_lc_hex_alpha(ch) || is_uc_hex_alpha(ch);
}

[[nodiscard]] inline uint16_t hex_digit_value(char ch) noexcept {
  if (is_digit(ch)) return static_cast<uint16_t>(ch - '0');
  if (is_lc_hex_alpha(ch)) return static_cast<uint16_t>(10 + (ch - 'a'));
  return static_cast<uint16_t>(10 + (ch - 'A'));
}

[[nodiscard]] inline std::optional<uint16_t>
parse_hex4(std::string_view s, size_t pos) noexcept {
  if (pos + 4 > s.size()) return std::nullopt;
  uint16_t value{};
  for (size_t i = 0; i < 4; ++i) {
    const auto ch = s[pos + i];
    if (!is_hex_digit(ch)) return std::nullopt;
    value = static_cast<uint16_t>((value << 4U) | hex_digit_value(ch));
  }
  return value;
}

constexpr bool append_utf8(std::string& out, uint32_t code_point) {
  if (code_point <= 0x7FU) {
    out.push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7FFU) {
    out.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else if (code_point <= 0xFFFFU) {
    out.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else {
    out.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  }
  return true;
}

constexpr bool validate_unicode_escape(json_cursor& c, json_error* err) {
  const auto start = c.pos;
  const auto first = parse_hex4(c.input, c.pos);
  if (!first)
    return json_error::fail(err, json_errc::invalid_unicode_escape, start);
  c += 4;

  if (*first >= 0xD800U && *first <= 0xDBFFU) {
    if (c.pos + 6 > c.input.size() || *c != '\\' || c[1] != 'u')
      return json_error::fail(err, json_errc::invalid_surrogate_pair, start);
    const auto second = parse_hex4(c.input, c.pos + 2);
    if (!second || *second < 0xDC00U || *second > 0xDFFFU)
      return json_error::fail(err, json_errc::invalid_surrogate_pair, start);
    c += 6;
    return true;
  }

  if (*first >= 0xDC00U && *first <= 0xDFFFU)
    return json_error::fail(err, json_errc::invalid_surrogate_pair, start);

  return true;
}

constexpr bool
decode_unicode_escape(std::string_view source, size_t& pos, std::string& out) {
  const auto first = parse_hex4(source, pos);
  if (!first) return false;
  pos += 4;

  if (*first >= 0xD800U && *first <= 0xDBFFU) {
    if (pos + 6 > source.size() || source[pos] != '\\' ||
        source[pos + 1] != 'u')
      return false;
    const auto second = parse_hex4(source, pos + 2);
    if (!second || *second < 0xDC00U || *second > 0xDFFFU) return false;
    pos += 6;

    const auto high = static_cast<uint32_t>(*first - 0xD800U);
    const auto low = static_cast<uint32_t>(*second - 0xDC00U);
    const auto code_point = 0x10000U + ((high << 10U) | low);
    return append_utf8(out, code_point);
  }

  if (*first >= 0xDC00U && *first <= 0xDFFFU) return false;
  return append_utf8(out, *first);
}

// Decode the full contents of a JSON string literal, excluding its quotes.
constexpr bool
decode_string_contents(std::string_view source, std::string& out) {
  if (source.size() < 2 || source.front() != '"' || source.back() != '"')
    return false;

  source = source.substr(1, source.size() - 2);

  out.clear();
  out.reserve(source.size());

  for (size_t pos = 0; pos < source.size();) {
    const auto ch = source[pos++];
    if (ch == '\\') {
      if (pos >= source.size()) return false;
      const auto esc = source[pos++];
      switch (esc) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'u':
        if (!decode_unicode_escape(source, pos, out)) return false;
        break;
      default: return false;
      }
      continue;
    }

    if (static_cast<unsigned char>(ch) < 0x20U) return false;
    out.push_back(ch);
  }

  return true;
}

constexpr bool parse_literal(json_cursor& c, std::string_view literal,
    json_kind kind, json_value_view& out, json_error* err) {
  const auto start = c.pos;
  if (!c.substr(c.pos).starts_with(literal))
    return json_error::fail(err, json_errc::invalid_literal, start);
  c += literal.size();
  out = json_value_view{c.substr(start, literal.size()), kind};
  return true;
}

constexpr bool
parse_number(json_cursor& c, json_value_view& out, json_error* err) {
  const auto start = c.pos;
  if (*c == '-') ++c;
  if (c.at_end())
    return json_error::fail(err, json_errc::invalid_number, start);

  if (*c == '0') {
    ++c;
    if (!c.at_end() && is_digit(*c))
      return json_error::fail(err, json_errc::invalid_number, c.pos);
  } else if (is_digit(*c)) {
    while (!c.at_end() && is_digit(*c)) ++c;
  } else {
    return json_error::fail(err, json_errc::invalid_number, c.pos);
  }

  if (!c.at_end() && *c == '.') {
    ++c;
    if (c.at_end() || !is_digit(*c))
      return json_error::fail(err, json_errc::invalid_number, c.pos);
    while (!c.at_end() && is_digit(*c)) ++c;
  }

  if (!c.at_end() && (*c == 'e' || *c == 'E')) {
    ++c;
    if (!c.at_end() && (*c == '+' || *c == '-')) ++c;
    if (c.at_end() || !is_digit(*c))
      return json_error::fail(err, json_errc::invalid_number, c.pos);
    while (!c.at_end() && is_digit(*c)) ++c;
  }

  out = json_value_view{c.substr(start, c.pos - start), json_kind::number};
  return true;
}

constexpr bool
parse_string(json_cursor& c, json_value_view& out, json_error* err) {
  const auto start = c.pos;
  if (*c != '"')
    return json_error::fail(err, json_errc::invalid_string, c.pos);
  ++c;

  while (!c.at_end()) {
    const auto ch = c.next();
    if (ch == '"') {
      out = json_value_view{c.substr(start, c.pos - start), json_kind::string};
      return true;
    }

    if (static_cast<unsigned char>(ch) < 0x20U)
      return json_error::fail(err, json_errc::invalid_string, c.pos - 1);

    if (ch != '\\') continue;
    if (c.at_end())
      return json_error::fail(err, json_errc::unexpected_end, c.pos);

    const auto esc = c.next();
    switch (esc) {
    case '"':
    case '\\':
    case '/':
    case 'b':
    case 'f':
    case 'n':
    case 'r':
    case 't': break;
    case 'u':
      if (!validate_unicode_escape(c, err)) return false;
      break;
    default:
      return json_error::fail(err, json_errc::invalid_escape, c.pos - 1);
    }
  }

  return json_error::fail(err, json_errc::unexpected_end, start);
}

// Recursive-descent parser entrypoint for one JSON value at the current
// cursor.
constexpr bool parse_value(json_cursor& c, json_value_view& out,
    json_error* err, const json_parse_options& opts, size_t depth);

constexpr bool parse_array(json_cursor& c, json_value_view& out,
    json_error* err, const json_parse_options& opts, size_t depth) {
  const auto start = c.pos;
  if (depth >= opts.max_depth)
    return json_error::fail(err, json_errc::depth_exceeded, c.pos);

  ++c;
  if (c.skip_ws().at_end())
    return json_error::fail(err, json_errc::unexpected_end, start);
  if (*c == ']') {
    ++c;
    out = json_value_view{c.substr(start, c.pos - start), json_kind::array};
    return true;
  }

  while (true) {
    json_value_view item;
    if (!parse_value(c, item, err, opts, depth + 1)) return false;

    if (c.skip_ws().at_end())
      return json_error::fail(err, json_errc::unexpected_end, start);

    const auto ch = c.next();
    if (ch == ']') {
      out = json_value_view{c.substr(start, c.pos - start), json_kind::array};
      return true;
    }
    if (ch != ',')
      return json_error::fail(err, json_errc::expected_comma_or_end,
          c.pos - 1);

    if (c.skip_ws().at_end())
      return json_error::fail(err, json_errc::unexpected_end, start);
    if (*c == ']')
      return json_error::fail(err, json_errc::expected_value, c.pos);
  }
}

constexpr bool parse_object(json_cursor& c, json_value_view& out,
    json_error* err, const json_parse_options& opts, size_t depth) {
  const auto start = c.pos;
  if (depth >= opts.max_depth)
    return json_error::fail(err, json_errc::depth_exceeded, c.pos);

  ++c;
  if (c.skip_ws().at_end())
    return json_error::fail(err, json_errc::unexpected_end, start);
  if (*c == '}') {
    ++c;
    out = json_value_view{c.substr(start, c.pos - start), json_kind::object};
    return true;
  }

  while (true) {
    json_value_view key;
    if (*c != '"')
      return json_error::fail(err, json_errc::expected_key, c.pos);
    if (!parse_string(c, key, err)) return false;

    if (c.skip_ws().at_end())
      return json_error::fail(err, json_errc::unexpected_end, start);
    if (*c != ':')
      return json_error::fail(err, json_errc::expected_colon, c.pos);
    ++c;

    json_value_view value;
    if (!parse_value(c, value, err, opts, depth + 1)) return false;

    if (c.skip_ws().at_end())
      return json_error::fail(err, json_errc::unexpected_end, start);

    const auto ch = c.next();
    if (ch == '}') {
      out = json_value_view{c.substr(start, c.pos - start), json_kind::object};
      return true;
    }
    if (ch != ',')
      return json_error::fail(err, json_errc::expected_comma_or_end,
          c.pos - 1);

    if (c.skip_ws().at_end())
      return json_error::fail(err, json_errc::unexpected_end, start);
    if (*c == '}')
      return json_error::fail(err, json_errc::expected_key, c.pos);
  }
}

constexpr bool parse_value(json_cursor& c, json_value_view& out,
    json_error* err, const json_parse_options& opts, size_t depth) {
  if (c.skip_ws().at_end())
    return json_error::fail(err, json_errc::unexpected_end, c.pos);

  switch (*c) {
  case 'n': return parse_literal(c, "null", json_kind::null, out, err);
  case 't': return parse_literal(c, "true", json_kind::boolean, out, err);
  case 'f': return parse_literal(c, "false", json_kind::boolean, out, err);
  case '"': return parse_string(c, out, err);
  case '[': return parse_array(c, out, err, opts, depth);
  case '{': return parse_object(c, out, err, opts, depth);
  default:
    if (*c == '-' || (is_digit(*c))) return parse_number(c, out, err);
    return json_error::fail(err, json_errc::invalid_token, c.pos);
  }
}

// Compare a JSON string value against plain text, decoding only when needed.
constexpr bool
string_equals(json_value_view candidate, std::string_view wanted) {
  if (!candidate.is_string()) return false;
  if (const auto plain = candidate.string_view_if_plain(); plain)
    return *plain == wanted;
  std::string decoded;
  return candidate.decode_string(decoded) && decoded == wanted;
}

template<AppendTarget Target, std::floating_point Number>
constexpr void append_float(Target& target, Number value,
    std::chars_format fmt, int precision) {
  if (!std::isfinite(value)) {
    target += "null";
    return;
  }

  char buffer[128];
  std::to_chars_result result;
  if (precision >= 0)
    result =
        std::to_chars(buffer, buffer + sizeof(buffer), value, fmt, precision);
  else
    result = std::to_chars(buffer, buffer + sizeof(buffer), value, fmt);

  if (result.ec != std::errc{}) return;
  target += std::string_view{buffer, static_cast<size_t>(result.ptr - buffer)};
}

constexpr json_parse_options iterator_parse_options{
    std::numeric_limits<size_t>::max()};

} // namespace detail

// Parse and validate a complete JSON document.
//
// On success, `out` becomes a view of the root value. On failure, `out` is
// cleared and `err`, when provided, receives the first parse failure.
[[nodiscard]] constexpr bool parse_json(std::string_view input,
    json_value_view& out, json_error* err = nullptr,
    json_parse_options opts = {}) {
  if (err) err->clear();

  json_value_view parsed;
  detail::json_cursor c{input};
  if (!detail::parse_value(c.skip_ws(), parsed, err, opts, 0)) {
    out = {};
    return false;
  }

  if (!c.skip_ws().at_end()) {
    out = {};
    return json_error::fail(err, json_errc::trailing_data, c.pos);
  }

  out = parsed;
  return true;
}

[[nodiscard]] constexpr bool needs_json_escaping(std::string_view s) noexcept {
  return strings::needs_escaping(s);
}

constexpr bool json_value_view::decode_string(std::string& out) const {
  if (!is_string()) return false;
  return detail::decode_string_contents(source_, out);
}

constexpr json_object_view json_value_view::as_object() const noexcept {
  return is_object() ? json_object_view{source_} : json_object_view{};
}

constexpr json_array_view json_value_view::as_array() const noexcept {
  return is_array() ? json_array_view{source_} : json_array_view{};
}

constexpr json_array_view::iterator json_array_view::begin() const noexcept {
  if (source_.size() < 2 || source_.front() != '[' || source_.back() != ']')
    return {};
  return iterator{this, 1};
}

constexpr void json_array_view::iterator::load_current() {
  current_ = {};
  next_pos_ = npos;
  if (!owner_ || pos_ == npos) return;

  detail::json_cursor c{owner_->source_, pos_};
  if (c.skip_ws().at_end() || owner_->source_[c.pos] == ']') {
    owner_ = nullptr;
    pos_ = npos;
    return;
  }

  if (!detail::parse_value(c, current_, nullptr,
          detail::iterator_parse_options, 0))
  {
    owner_ = nullptr;
    pos_ = npos;
    return;
  }

  pos_ = c.pos;
  next_pos_ = c.pos;
}

constexpr json_array_view::iterator& json_array_view::iterator::operator++() {
  if (!owner_ || pos_ == npos) return *this;

  detail::json_cursor c{owner_->source_, next_pos_};
  if (c.skip_ws().at_end()) {
    owner_ = nullptr;
    pos_ = npos;
    return *this;
  }

  if (owner_->source_[c.pos] == ',') {
    pos_ = c.pos + 1;
    load_current();
    return *this;
  }

  owner_ = nullptr;
  pos_ = npos;
  return *this;
}

constexpr json_object_view::iterator json_object_view::begin() const noexcept {
  if (source_.size() < 2 || source_.front() != '{' || source_.back() != '}')
    return {};
  return iterator{this, 1};
}

constexpr void json_object_view::iterator::load_current() {
  current_ = {};
  next_pos_ = npos;
  if (!owner_ || pos_ == npos) return;

  detail::json_cursor c{owner_->source_, pos_};
  if (c.skip_ws().at_end() || *c == '}') {
    owner_ = nullptr;
    pos_ = npos;
    return;
  }

  if (!detail::parse_string(c, current_.key, nullptr)) {
    owner_ = nullptr;
    pos_ = npos;
    return;
  }

  if (c.skip_ws().at_end() || *c != ':') {
    owner_ = nullptr;
    pos_ = npos;
    return;
  }
  ++c;

  if (!detail::parse_value(c, current_.value, nullptr,
          detail::iterator_parse_options, 0))
  {
    owner_ = nullptr;
    pos_ = npos;
    return;
  }

  pos_ = c.pos;
  next_pos_ = c.pos;
}

constexpr json_object_view::iterator&
json_object_view::iterator::operator++() {
  if (!owner_ || pos_ == npos) return *this;

  detail::json_cursor c{owner_->source_, next_pos_};
  if (c.skip_ws().at_end()) {
    owner_ = nullptr;
    pos_ = npos;
    return *this;
  }

  if (owner_->source_[c.pos] == ',') {
    pos_ = c.pos + 1;
    load_current();
    return *this;
  }

  owner_ = nullptr;
  pos_ = npos;
  return *this;
}

constexpr json_value_view json_object_view::find(std::string_view key) const {
  for (const auto entry : *this)
    if (detail::string_equals(entry.key, key)) return entry.value;
  return {};
}

constexpr std::optional<bool> json_object_view::get_bool(
    std::string_view key) const {
  return find(key).as_bool();
}

template<AppendTarget Target>
// Stateful compact JSON writer over a `std::string` or `std::ostream`.
//
// The writer tracks container nesting and comma placement internally. It
// does not validate call ordering aggressively; callers are expected to use
// matching `begin_*` / `end_*` pairs and to write object keys before their
// values.
class json_writer {
  template<typename Begin, typename End>
  class scoped_writer {
  public:
    constexpr scoped_writer(json_writer& writer, Begin&& begin,
        End&& end) noexcept
        : writer_{&writer}, end_{std::forward<End>(end)} {
      std::forward<Begin>(begin)(*writer_);
    }

    scoped_writer(const scoped_writer&) = delete;
    scoped_writer(const scoped_writer&&) = delete;
    scoped_writer& operator=(const scoped_writer&) = delete;
    scoped_writer& operator=(scoped_writer&&) = delete;

    constexpr ~scoped_writer() { end_(*writer_); }

    [[nodiscard]] constexpr json_writer* operator->() noexcept {
      return writer_;
    }

    [[nodiscard]] constexpr json_writer& operator*() noexcept {
      return *writer_;
    }

    [[nodiscard]] constexpr json_writer& writer() noexcept { return *writer_; }

    // This lets you scope the the instance to an `if` statement, so that you
    // don't need to have braces without explanation.
    [[nodiscard]] operator bool() const noexcept { return true; }

  private:
    json_writer* writer_;
    [[no_unique_address]] End end_;
  };

  template<typename Begin, typename End>
  [[nodiscard]] constexpr auto scoped(Begin&& begin, End&& end) noexcept {
    return scoped_writer<std::decay_t<Begin>, std::decay_t<End>>{*this,
        std::forward<Begin>(begin), std::forward<End>(end)};
  }

  // Begin/end a JSON object or array value.
  constexpr json_writer& begin_object() {
    before_value();
    strings::append(target_, '{');
    stack_.push_back(frame{frame_kind::object});
    return *this;
  }

  constexpr json_writer& end_object() {
    if (stack_.empty()) return *this;
    strings::append(target_, '}');
    stack_.pop_back();
    return *this;
  }

  constexpr json_writer& begin_array() {
    before_value();
    strings::append(target_, '[');
    stack_.push_back(frame{frame_kind::array});
    return *this;
  }

  constexpr json_writer& end_array() {
    if (stack_.empty()) return *this;
    strings::append(target_, ']');
    stack_.pop_back();
    return *this;
  }

public:
  explicit constexpr json_writer(Target& target) : target_{target} {}

  [[nodiscard]] constexpr Target& target() noexcept { return target_; }

  [[nodiscard]] constexpr auto object() noexcept {
    return scoped([](json_writer& writer) { writer.begin_object(); },
        [](json_writer& writer) { writer.end_object(); });
  }

  [[nodiscard]] constexpr auto array() noexcept {
    return scoped([](json_writer& writer) { writer.begin_array(); },
        [](json_writer& writer) { writer.end_array(); });
  }

  template<StringViewConvertible S>
  requires(!SameAs<json_trusted, S>)
  [[nodiscard]] constexpr auto member_object(const S& key_text) noexcept {
    key(key_text);
    return object();
  }

  [[nodiscard]] constexpr auto member_object(json_trusted key_text) noexcept {
    key(key_text);
    return object();
  }

  template<StringViewConvertible S>
  requires(!SameAs<json_trusted, S>)
  [[nodiscard]] constexpr auto member_array(const S& key_text) noexcept {
    key(key_text);
    return array();
  }

  [[nodiscard]] constexpr auto member_array(json_trusted key_text) noexcept {
    key(key_text);
    return array();
  }

  // Write an object key. `json_trusted` skips the escape scan.
  template<StringViewConvertible S>
  requires(!SameAs<json_trusted, S>)
  constexpr json_writer& key(const S& key_text) {
    write_key(std::string_view{key_text}, false);
    return *this;
  }

  constexpr json_writer& key(json_trusted key_text) {
    write_key(key_text.value, true);
    return *this;
  }

  // Write scalar values. String overloads emit quoted JSON strings.
  constexpr json_writer& value(std::nullptr_t) {
    before_value();
    strings::append(target_, "null");
    return *this;
  }

  constexpr json_writer& value(bool v) {
    before_value();
    strings::append(target_, v ? "true" : "false");
    return *this;
  }

  template<std::integral T>
  requires(!std::same_as<std::remove_cvref_t<T>, bool>)
  constexpr json_writer& value(T v) {
    before_value();
    strings::append(target_, v);
    return *this;
  }

  template<std::floating_point T>
  constexpr json_writer& value(T v,
      std::chars_format fmt = std::chars_format::general, int precision = -1) {
    before_value();
    detail::append_float(target_, v, fmt, precision);
    return *this;
  }

  template<StringViewConvertible S>
  requires(!SameAs<json_trusted, S>)
  constexpr json_writer& value(const S& s) {
    before_value();
    write_quoted(std::string_view{s}, false);
    return *this;
  }

  constexpr json_writer& value(json_trusted s) {
    before_value();
    write_quoted(s.value, true);
    return *this;
  }

  // Write one `"key": value` object member in a single call.
  template<typename T>
  constexpr json_writer&
  member(std::string_view key_text, const T& value_text) {
    return key(key_text).value(value_text);
  }

  constexpr json_writer&
  member(std::string_view key_text, std::nullptr_t value_text = nullptr) {
    return key(key_text).value(value_text);
  }

  constexpr json_writer& member(std::string_view key_text,
      std::floating_point auto value_text, std::chars_format fmt,
      int precision = -1) {
    return key(key_text).value(value_text, fmt, precision);
  }

  template<typename T>
  constexpr json_writer& member(json_trusted key_text, const T& value_text) {
    return key(key_text).value(value_text);
  }

  constexpr json_writer&
  member(json_trusted key_text, std::nullptr_t value_text = nullptr) {
    return key(key_text).value(value_text);
  }

  constexpr json_writer& member(json_trusted key_text,
      std::floating_point auto value_text, std::chars_format fmt,
      int precision = -1) {
    return key(key_text).value(value_text, fmt, precision);
  }

private:
  enum class frame_kind : uint8_t { array, object };

  // Per-container writer state. Objects use `expect_value` to distinguish
  // between key and value positions after a colon has been emitted.
  struct frame {
    frame_kind kind;
    bool first{true};
    bool expect_value{};
  };

  // Emit any comma needed before the next array element or object value.
  constexpr void before_value() {
    if (stack_.empty()) return;

    auto& top = stack_.back();
    if (top.kind == frame_kind::array) {
      if (!top.first) strings::append(target_, ',');
      top.first = false;
      return;
    }

    if (top.expect_value) {
      top.expect_value = false;
      return;
    }
  }

  // Emit an escaped or trusted object key followed by `:`.
  constexpr void write_key(std::string_view key_text, bool trusted) {
    if (stack_.empty()) return;
    auto& top = stack_.back();
    if (top.kind != frame_kind::object || top.expect_value) return;

    if (!top.first) strings::append(target_, ',');
    top.first = false;
    write_quoted(key_text, trusted);
    strings::append(target_, ':');
    top.expect_value = true;
  }

  // Emit one quoted JSON string, escaping unless explicitly trusted.
  constexpr void write_quoted(std::string_view text, bool trusted) {
    strings::append(target_, '"');
    if (trusted)
      strings::append(target_, text);
    else
      strings::append_escaped(target_, text);
    strings::append(target_, '"');
  }

  Target& target_;
  std::vector<frame> stack_;
};

template<AppendTarget Target>
json_writer(Target&) -> json_writer<Target>;
}}} // namespace corvid::proto::json
