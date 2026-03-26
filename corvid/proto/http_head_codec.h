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
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../enums/sequence_enum.h"
#include "../strings/cases.h"
#include "../strings/conversion.h"
#include "../strings/trimming.h"
#include "../strings/token_parser.h"
#include "../strings/enum_conversion.h"
#include "../containers/transparent.h"

namespace corvid { inline namespace proto { inline namespace http_proto {

// HTTP head parser and serializer for HTTP/0.9, HTTP/1.0, and HTTP/1.1.
//
// The head consists of the response or status line, and the header fields, but
// not the body.

using namespace std::string_view_literals;

// HTTP protocol version.
enum class http_version : uint8_t { invalid, http_09, http_1_0, http_1_1 };

// HTTP request method.
enum class http_method : uint8_t {
  invalid,
  GET,
  HEAD,
  POST,
  PUT,
  DELETE,
  OPTIONS,
  PATCH,
  CONNECT,
  TRACE
};

}}} // namespace corvid::proto::http_proto

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::http_proto::http_version> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::proto::http_proto::http_version,
        "invalid, HTTP/0.9, HTTP/1.0, HTTP/1.1">();

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::http_proto::http_method> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::proto::http_proto::http_method,
        "invalid, GET, HEAD, POST, PUT, DELETE, OPTIONS, PATCH, CONNECT, "
        "TRACE">();

namespace corvid { inline namespace proto { inline namespace http_proto {

// Old-school struct as namespace.
struct http {
  static constexpr auto valid_field_name_chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
      "0123456789-!#$%&'*+.^_`|~"sv;
  static constexpr auto crlf = "\r\n"sv;
  static constexpr auto crlfcrlf = "\r\n\r\n"sv;
};

// Ordered multimap of HTTP header fields with O(1) average lookup.
//
// Insertion order is preserved via `entries_`. A parallel `index_` maps
// canonical header names to index lists into `entries_`, enabling O(1)
// average lookup without linear scan.
//
// Field names are normalized in the index to "Content-Type" form.
class http_headers: http {
  struct field_line {
    std::string name;
    std::string value;
  };
  using field_line_vector = std::vector<field_line>;
  using index_vector = std::vector<size_t>;
  using index_map = string_unordered_map<index_vector>;

  field_line_vector entries_;
  index_map index_;

public:
  // Normalize a field name to Train-Case, in place. Only alphanumeric
  // characters, hyphens, and the token special characters are permitted.
  //
  // If any other character is found, `field_name` is left unchanged and
  // `std::nullopt` is returned to signal error.
  //
  // Otherwise, every character is lowercased, unless it's the first character
  // or follows a '-'. Returns `true` iff `field_name` was changed, `false` if
  // it was already normalized.
  //
  // Note: This is fully compliant with RFC 9110, but the modern practice,
  // which is strictly required for HTTP/2.0 and HTTP/3.0, is to lowercase all
  // field names.
  static std::optional<bool> normalize(std::string& field_name) {
    if (field_name.empty() ||
        (field_name.find_first_not_of(http::valid_field_name_chars) !=
            std::string_view::npos))
      return std::nullopt;
    bool changed{false};
    bool capitalize{true};
    for (char& c : field_name) {
      // Capitalize the first character following the hyphen.
      if (c == '-') {
        capitalize = true;
        continue;
      }

      char old = c;
      c = capitalize ? strings::to_upper(c) : strings::to_lower(c);

      if (c != old) changed = true;
      capitalize = false;
    }

    return changed;
  }

  // Utility function to confirm that a field name is normalized.
  [[nodiscard]] static bool is_normalized(std::string_view field_name) {
    std::string normalized_field_name{std::string{field_name}};
    return normalize(normalized_field_name).has_value();
  }

  // Return true iff `field_value` is a valid HTTP field value, per RFC 9110:
  // SP / HTAB / VCHAR / obs-text.
  [[nodiscard]] static bool is_valid_field_value(
      std::string_view field_value) noexcept {
    if (field_value.empty()) return true;
    for (unsigned char c : field_value)
      if (c != '\t' && (c < 0x20 || c > 0x7E) && c < 0x80) return false;
    return true;
  }

  // Add a field line from parts, storing `field_name` and `field_value` as-is
  // (no validation or normalization). If specified `raw_field_name` is stored
  // for the line, while `field_name` is stored for the index. Otherwise, the
  // two are the same.
  //
  // The use case for this method is when the caller has already validated and
  // normalized the field name and value, such as when they're hardcoded.
  //
  // The caller is responsible for providing a valid,
  // normalized field name and a valid field value. Fails if too many fields
  // are added.
  [[nodiscard]] bool add_raw(std::string_view field_name,
      std::string_view field_value, std::string_view raw_field_name = {}) {
    assert(is_normalized(field_name) && is_valid_field_value(field_value));
    if (raw_field_name.empty()) raw_field_name = field_name;
    entries_.push_back(
        {std::string{raw_field_name}, std::string{field_value}});
    index_[std::string{field_name}].push_back(entries_.size() - 1);
    return true;
  }

  // Add a field line from parts, performing normalization and validation.
  // Returns false if the line is malformed, or if either the field name or
  // value is invalid. Fails if either parameter is invalid, which merits a
  // "400 Bad Request".
  [[nodiscard]] bool
  add(std::string_view field_name, std::string_view field_value) {
    std::string normal_field_name{field_name};
    if (!normalize(normal_field_name)) return false;
    if (!is_valid_field_value(field_value)) return false;
    return add_raw(normal_field_name, field_value, field_name);
  }

  // Return a `string_view` into the field value for the field line whose name
  // matches `field_name`. The `field_name` is expected to be normalized.
  // Returns `std::nullopt` if not found, as opposed to empty if it was found
  // with an empty value.
  [[nodiscard]] std::optional<std::string_view> get(
      std::string_view field_name) const noexcept {
    assert(is_normalized(field_name));
    auto ids = find_opt(index_, field_name);
    if (!ids || ids->empty()) return std::nullopt;
    return entries_[ids->front()].value;
  }

  // Return all values for `field_name`, concatenated with `", "`. The
  // `field_name` is expected to be normalized. Returns an empty string if not
  // found.
  [[nodiscard]] std::string get_combined(std::string_view field_name) const {
    assert(is_normalized(field_name));
    auto ids = find_opt(index_, field_name);
    if (!ids || ids->empty()) return {};
    std::string result;
    result.reserve(128);
    for (const size_t ndx : *ids) {
      result += entries_[ndx].value;
      result += ", ";
    }
    if (!result.empty()) result.resize(result.size() - 2);
    return result;
  }

  // Add a field line by parsing `line`, which does not include the trailing
  // crlf. Must not be empty: the caller is responsible for detecting the
  // end-of-headers blank line. The field name is required to be non-empty and
  // must be followed by a colon, but the field value is optional, and may be
  // whitespace-padded. Returns false for obs-fold, missing colon, empty or
  // invalid field name.
  [[nodiscard]] bool add_line(std::string_view line) {
    if (line.empty()) return false;
    if (line.front() == ' ' || line.front() == '\t') return false;
    auto found = strings::token_parser::next_terminated(":", line);
    if (!found) return false;
    const auto name = *found;
    const auto value = strings::trim(line);
    return add(name, value);
  }

  // Add multiple field lines by parsing `header_lines`, which is the block of
  // text after the first request/response line and its trailing crlf, up to
  // but not including the crlfcrlf sentinel. Each line, except the last, must
  // end with a crlf. Empty lines are rejected (RFC 9112 section 2.2
  // request-smuggling defence). Returns false if any line uses obs-fold, lacks
  // a colon, or either the field name or value is invalid.
  [[nodiscard]] bool add_lines(std::string_view header_lines) {
    strings::token_parser parser{crlf};
    std::string_view line = parser.next_delimited(header_lines);
    for (; !line.empty(); line = parser.next_delimited(header_lines))
      if (line.empty() || !add_line(line)) return false;
    return true;
  }

  // Serialize all headers into wire format, which is the block of text after
  // the request/status line.
  //
  // Makes no attempt to encode the field values or reserve adequate space.
  void serialize(std::string& out) const {
    for (const auto& [name, value] : entries_) {
      out += name;
      out += ':';
      if (!value.empty()) {
        out += ' ';
        out += value;
      }
      out += crlf;
    }
    out += crlf;
  }

  // Ordered iteration (insertion order).
  [[nodiscard]] auto begin() const noexcept { return entries_.begin(); }
  [[nodiscard]] auto end() const noexcept { return entries_.end(); }
  [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
  [[nodiscard]] size_t size() const noexcept { return entries_.size(); }

  // Return true iff the connection should remain open.
  // HTTP/1.1 defaults to keep-alive; HTTP/1.0 and HTTP/0.9 default to close.
  // Overridden by `"Connection: close"` or `"Connection: keep-alive"`.
  [[nodiscard]] bool keep_alive(http_version version) const noexcept {
    if (version == http_version::http_09) return false;
    const auto c = strings::as_lower(get("Connection").value_or(""sv));
    if (c == "close") return false;
    if (c == "keep-alive") return true;
    return version == http_version::http_1_1;
  }

  // Return the `Content-Length` value, or `std::nullopt` if absent or
  // unparseable.
  [[nodiscard]] std::optional<size_t> content_length() const noexcept {
    const auto sv = get("Content-Length");
    if (!sv) return std::nullopt;
    return strings::parse_num<size_t>(*sv);
  }

  // Return true iff `"Transfer-Encoding: chunked"` is present.
  [[nodiscard]] bool is_chunked() const noexcept {
    return strings::as_lower(get("Transfer-Encoding").value_or(""sv)) ==
           "chunked";
  }

  // TODO: Need ways to remove individual lines.
  // TODO: We need some way to walk through all values for a given field name,
  // but this can probably wait for the next version, which links the entries
  // into a list while storing just the head in the index.
};

// HTTP request head, consisting of the request line and the header fields. The
// body is not included.
//
// Obtained via `parse` after `terminated_text_parser` delivers the head (the
// bytes before the crlfcrlf sentinel).
struct request_head: http {
  http_version version{};
  http_method method{};
  std::string target;
  http_headers headers;

  // Reset to default-constructed state.
  void clear() {
    version = {};
    method = {};
    target.clear();
    headers = {};
  }

  // Parse the head of a request, containing the request line and the headers.
  // This is a "Full Request", per RFC. Essentially, it is the text up to but
  // not including the trailing crlfcrlf.
  //
  // For HTTP/1.1, the headers that follow the request line are required. For
  // HTTP/1.0, the headers can be empty, but the crlfcrlf sentinel is still
  // required. For HTTP/0.9, headers are not allowed, and there is no crlfcrlf
  // sentinel; it ends at the crlf of the request line.
  //
  // To support these variations, this method can parse just the request line
  // when that's all that's passed. Also, it makes no attempt to parse headers
  // when it's HTTP/0.9. It is up to the caller to look at both the version and
  // the headers to decide whether the combination is valid.
  //
  // The instance should be empty or cleared before calling this method,
  // although it's ok to reuse without clearing if you're parsing the request
  // line and then the entire head. Alternately, you could parse just the
  // headers directly.
  //
  // Returns true on success, false if any part is malformed. The reason
  // for failure is stored in `target`, for logging and debugging purposes.
  [[nodiscard]] bool parse(std::string_view head) {
    // Skip leading CRLF lines (RFC 9112 section 2.2).
    for (int leading_crlfs = 0; head.starts_with(crlf); ++leading_crlfs) {
      if (leading_crlfs == 5) return fail("Too many leading CRLFs");
      head.remove_prefix(crlf.size());
    }

    strings::token_parser crlf_parser{crlf};
    auto request_line = crlf_parser.next_delimited(head);
    if (request_line.empty()) return fail("Empty request line");

    strings::token_parser space_parser{" "sv};
    auto method_sv = space_parser.next_delimited(request_line);
    if (method_sv.empty()) return fail("No SP after method");

    if (!strings::convert_text_enum(method, method_sv) ||
        method == http_method::invalid)
      return fail("Invalid HTTP method");

    target = std::string{space_parser.next_delimited(request_line)};
    if (target.empty()) return fail("Empty target");
    if (target[0] != '/') return fail("Target not a path");

    auto version_sv = request_line;
    if (version_sv.empty())
      version = http_version::http_09;
    else if (!strings::convert_text_enum(version, version_sv) ||
             version == http_version::invalid)
      return fail("Invalid HTTP version");

    // HTTP/0.9: no headers allowed.
    auto header_lines = head;
    if (version == http_version::http_09) {
      if (!header_lines.empty())
        return fail("HTTP/0.9 does not allow headers");
      return true;
    }

    // HTTP/1.x: headers required, but that requirement is enforced by
    // `http_server`.
    if (header_lines.empty()) return true;

    if (!headers.add_lines(header_lines))
      return fail("Malformed header lines");
    return true;
  }

  // Serialize to `"METHOD target HTTP/1.x\r\nHeaders\r\n\r\n"`.
  // HTTP/0.9 omits the version token. Returns an empty string for
  // `http_method::invalid` or `http_version::invalid`.
  [[nodiscard]] std::string serialize() const {
    std::string result;
    if (version == http_version::invalid) return result;
    result += strings::enum_as_string(method);
    result += ' ';
    result += target;

    if (version != http_version::http_09) {
      result += ' ';
      result += strings::enum_as_string(version);
    }

    result += "\r\n";
    headers.serialize(result);
    return result;
  }

private:
  bool fail(std::string failure) {
    target = std::move(failure);
    return false;
  }
};

// HTTP response head, consisting of the status line and the header fields. The
// body is not included.
//
// Populate the fields and call `serialize()` to produce the wire-format
// response string to pass to `stream_conn::send()`.
struct response_head: http {
  http_version version{};
  int status_code{};
  std::string reason;
  http_headers headers;

  // Reset to default-constructed state.
  void clear() {
    version = {};
    status_code = 0;
    reason.clear();
    headers = {};
  }

  // Parse a response head (the bytes before the crlfcrlf
  // sentinel). Returns true on success, false if any part is malformed. The
  // reason for failure is stored in `reason`, for logging and debugging
  // purposes.
  [[nodiscard]] bool parse(std::string_view head) {
    strings::token_parser crlf_parser{crlf};
    auto status_line = crlf_parser.next_delimited(head);
    if (status_line.empty()) return fail("Empty status line");

    strings::token_parser space_parser{" "sv};
    auto version_sv = space_parser.next_delimited(status_line);
    if (!strings::convert_text_enum(version, version_sv) ||
        version == http_version::invalid)
      return fail("Invalid HTTP version");

    // The reason is optional, but the trailing space is required, hence it's a
    // terminator, not a delimiter.
    auto status_code_sv_opt = space_parser.next_terminated(status_line);
    if (!status_code_sv_opt) return fail("Status code not terminated by SP");
    auto status_code_sv = *status_code_sv_opt;

    auto status_code_opt = strings::parse_num<int>(status_code_sv);
    if (!status_code_opt || *status_code_opt < 100 || *status_code_opt > 999)
      return fail("Invalid status code");
    status_code = *status_code_opt;

    reason = std::string{status_line};

    // Headers are not strictly required, but the trailing crlfcrlf is.
    auto header_lines = head;
    if (header_lines.empty()) return true;

    if (!headers.add_lines(header_lines))
      return fail("Malformed header lines");
    return true;
  }

  // Produce `"HTTP/1.x code reason\r\nHeaders\r\n\r\n"`.
  // The caller is responsible for adding `Content-Type` and `Content-Length`
  // headers and for sending the body separately.

  // Serialize to `"HTTP/1.x code reason\r\nHeaders\r\n\r\n"`.
  // Returns an empty string for `http_method::invalid` or
  // `http_version::invalid`.
  [[nodiscard]] std::string serialize() const {
    std::string result;
    if (version == http_version::invalid) return result;

    result += strings::enum_as_string(version);
    result += ' ';
    result += std::to_string(status_code);
    result += ' ';
    result += reason;
    result += "\r\n";

    headers.serialize(result);
    return result;
  }

private:
  bool fail(std::string failure) {
    reason = std::move(failure);
    return false;
  }
};

}}} // namespace corvid::proto::http_proto
