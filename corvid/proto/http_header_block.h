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

#include "../enums.h"
#include "../strings/cases.h"
#include "../strings/conversion.h"
#include "../strings/trimming.h"
#include "../strings/token_parser.h"
#include "../strings/enum_conversion.h"
#include "../containers/transparent.h"

namespace corvid { inline namespace proto { inline namespace http_proto {

using namespace std::string_view_literals;

// HTTP protocol version.
enum class http_version : uint8_t { invalid, http_09, http_10, http_11 };

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
  static constexpr auto npos = std::string_view::npos;
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
// Header names are normalized in the index to "Content-Type" form.
class http_headers: http {
  using kvp = std::pair<std::string, std::string>;
  using kvp_vector = std::vector<kvp>;
  using index_vector = std::vector<size_t>;
  using index_map = string_unordered_map<index_vector>;

  kvp_vector entries_;
  index_map index_;

public:
  // Normalize a header name to Train-Case, in place. Only alphanumeric
  // characters, hyphens, and the token special characters are permitted.
  //
  // If any other character is found, `field_name` is left unchanged and
  // `nullopt` is returned to signal error.
  //
  // Otherwise, every character is lowercased, unless it's the first character
  // or follows a '-'. Returns `true` iff `field_name` was changed, `false` if
  // it was already normalized.
  //
  // Note: This is fully compliant with RFC 9110, but the modern practice,
  // which is strictly required for HTTP/2.0 and HTTP/3.0, is to lowercase call
  // field headers.
  static std::optional<bool> normalize(std::string& field_name) {
    if (field_name.empty()) return std::nullopt;
    if (field_name.find_first_not_of(http::valid_field_name_chars) != npos)
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

  // Add a header, storing `field_name` as-is (no validation or
  // normalization). The caller is responsible for providing a valid,
  // normalized name. Returns success. (May fail when too many fields are
  // added.)
  [[nodiscard]] bool
  add_raw(std::string_view field_name, std::string_view field_value) {
    const size_t ndx{entries_.size()};
    entries_.emplace_back(field_name, std::string{field_value});
    index_[std::string{field_name}].push_back(ndx);
    return true;
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

  // Add a header, normalizing `field_name` and `field_value` before storage.
  // Returns success. Fails if `field_name` is empty or contains invalid
  // characters, which merits a "400 Bad Request".
  [[nodiscard]] bool
  add(std::string_view field_name, std::string_view field_value) {
    std::string canon{field_name};
    if (canon.empty() || !normalize(canon)) return false;
    if (!is_valid_field_value(field_value)) return false;
    return add_raw(canon, field_value);
  }

  // Return a `string_view` into the stored value for the first entry whose
  // name matches `field_name`. The `field_name` is expected to be canonical
  // and the value is expected to be valid. Returns `nullopt` if not found, as
  // opposed to empty if it was found with an empty value.
  [[nodiscard]] std::optional<std::string_view> get(
      std::string_view field_name) const noexcept {
    auto ids = find_opt(index_, field_name);
    if (!ids || ids->empty()) return std::nullopt;
    return entries_[ids->front()].second;
  }

  // Return all values for `field_name` concatenated with `", "`.
  // The `field_name` is expected to be canonical.
  // Returns an empty string if not found.
  [[nodiscard]] std::string combine(std::string_view field_name) const {
    auto ids = find_opt(index_, field_name);
    if (!ids || ids->empty()) return {};
    std::string result;
    for (const size_t ndx : *ids) {
      result += entries_[ndx].second;
      result += ", ";
    }
    if (!result.empty()) result.resize(result.size() - 2);
    return result;
  }

  // Process a single header-field line, which does not include the trailing
  // crlf. The caller is responsible for detecting the end-of-headers blank
  // line (empty lines must not be passed here). Returns false for obs-fold,
  // missing colon, empty or invalid field name, or unnormalizable name.
  [[nodiscard]] bool extract_line(std::string_view line) {
    if (line.empty()) return false;
    if (line.front() == ' ' || line.front() == '\t') return false;
    // Trailing colon is required, hence it's a terminator, not a delimiter.
    auto found = strings::token_parser::next_terminated(":", line);
    if (!found) return false;
    const auto name = *found;
    const auto value = strings::trim(line);
    return add(name, value);
  }

  // Parse header-field lines from `header_lines` (the bytes after the first
  // request/response line and its trailing crlf). Empty lines are rejected
  // (RFC 9112 section 2.2 request-smuggling defence). Returns false if any
  // line uses obs-fold, lacks a colon, or either the field name or value is
  // invalid.
  [[nodiscard]] bool extract(std::string_view header_lines) {
    strings::token_parser parser{crlf};
    std::string_view line = parser.next_delimited(header_lines);
    for (; !line.empty(); line = parser.next_delimited(header_lines))
      if (line.empty() || !extract_line(line)) return false;
    return true;
  }

  // Append all headers as `"Name: value\r\n"` lines, then append `"\r\n"`.
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
    return version == http_version::http_11;
  }

  // Return the `Content-Length` value, or `nullopt` if absent or unparseable.
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

  // TODO: We need some way to walk through all values for a given field name,
  // but this can probably wait for the next version, which links the entries
  // into a list while storing just the head in the index.
};

// Parsed HTTP request header block.
//
// All fields are owned values: no pointers into the recv_buffer. Obtained via
// `extract()` after `terminated_text_parser` delivers the raw header block
// (the bytes before the crlfcrlf sentinel).
struct request_header_block: http {
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
  // This is a "Full Request". Essentially, it is the text up to but not
  // including the trailing crlfcrlf.
  //
  // For HTTP/1.1, the headers that follow the request line are required. For
  // HTTP/1.0, the headers can be empty, but the crlfcrlf sentinel is still
  // required. For HTTP/0.9, headers are not allowed, and there is no crlfcrlf
  // sentinel; it ends at the crlf of the request line.
  //
  // To support these variations, this method can parse just the request line,
  // in which case no attempt will be made to parse the headers. It is up to
  // the caller to look at both the version and the headers to decide whether
  // the combination is valid.
  //
  // Returns true on success, false if any part is malformed. The reason
  // for failure is stored in `target` for debugging purposes.
  [[nodiscard]] bool extract(std::string_view head);

  // Serialize to `"METHOD target HTTP/1.x\r\nHeaders\r\n\r\n"`.
  // HTTP/0.9 omits the version token. Returns an empty string for
  // `http_method::invalid` or `http_version::invalid`.
  [[nodiscard]] std::string serialize() const;

private:
  bool fail(std::string failure) {
    target = std::move(failure);
    return false;
  }
};

// Parsed or constructed HTTP response header block.
//
// Populate the fields and call `serialize()` to produce the wire-format
// response string to pass to `stream_conn::send()`.
struct response_header_block: http {
  http_version version{http_version::http_11};
  int status_code{500};
  std::string reason;
  http_headers headers;

  // Reset to default-constructed state.
  void clear() {
    version = http_version::http_11;
    status_code = 500;
    reason.clear();
    headers = {};
  }

  // Parse a raw response header block (the bytes before the crlfcrlf
  // sentinel). Returns true on success, false if any part is malformed. The
  // reason for failure is stored in `reason` for debugging purposes.
  [[nodiscard]] bool extract(std::string_view raw);

  // Produce `"HTTP/1.x code reason\r\nHeaders\r\n\r\n"`.
  // The caller is responsible for adding `Content-Type` and `Content-Length`
  // headers and for sending the body separately.
  [[nodiscard]] std::string serialize() const;

private:
  bool fail(std::string failure) {
    reason = std::move(failure);
    return false;
  }
};

// --- implementations ---

inline bool request_header_block::extract(std::string_view raw) {
  // Skip leading CRLF lines (RFC 9112 section 2.2).
  for (int leading_crlfs = 0; raw.starts_with(crlf); ++leading_crlfs) {
    if (leading_crlfs == 5) return fail("Too many leading CRLF lines.");
    raw.remove_prefix(crlf.size());
  }

  strings::token_parser crlf_parser{crlf};
  auto request_line = crlf_parser.next_delimited(raw);
  if (request_line.empty()) return fail("Empty request line.");

  strings::token_parser space_parser{" "sv};
  auto method_sv = space_parser.next_delimited(request_line);
  if (method_sv.empty())
    return fail("Malformed request line: no SP after method.");

  if (!strings::convert_text_enum(method, method_sv) ||
      method == http_method::invalid)
    return fail("Invalid HTTP method");

  target = std::string{space_parser.next_delimited(request_line)};
  if (target.empty()) return fail("Malformed request line: target is empty.");
  if (target[0] != '/')
    return fail("Malformed request line: target must start with '/'.");

  auto version_sv = request_line;
  if (version_sv.empty())
    version = http_version::http_09;
  else if (!strings::convert_text_enum(version, version_sv) ||
           version == http_version::invalid)
    return fail("Invalid HTTP version");

  // HTTP/0.9: no headers allowed.
  auto header_lines = raw;
  if (version == http_version::http_09) return header_lines.empty();

  // HTTP/1.x: headers required, but that requirement is enforced by
  // `http_server`.
  if (header_lines.empty()) return true;

  return headers.extract(header_lines);
}

inline std::string response_header_block::serialize() const {
  std::string_view version_str;
  switch (version) {
  case http_version::http_10: version_str = "HTTP/1.0"sv; break;
  default: version_str = "HTTP/1.1"sv; break;
  }

  std::string result;
  result += version_str;
  result += ' ';
  result += std::to_string(status_code);
  result += ' ';
  result += reason;
  result += "\r\n";

  headers.serialize(result);
  return result;
}

inline std::string request_header_block::serialize() const {
  std::string_view method_sv;
  switch (method) {
  case http_method::GET: method_sv = "GET"sv; break;
  case http_method::HEAD: method_sv = "HEAD"sv; break;
  case http_method::POST: method_sv = "POST"sv; break;
  case http_method::PUT: method_sv = "PUT"sv; break;
  case http_method::DELETE: method_sv = "DELETE"sv; break;
  case http_method::OPTIONS: method_sv = "OPTIONS"sv; break;
  case http_method::PATCH: method_sv = "PATCH"sv; break;
  case http_method::CONNECT: method_sv = "CONNECT"sv; break;
  case http_method::TRACE: method_sv = "TRACE"sv; break;
  default: return {};
  }

  std::string result;
  result += method_sv;
  result += ' ';
  result += target;

  if (version != http_version::http_09) {
    std::string_view version_sv;
    switch (version) {
    case http_version::http_10: version_sv = "HTTP/1.0"sv; break;
    case http_version::http_11: version_sv = "HTTP/1.1"sv; break;
    default: return {};
    }
    result += ' ';
    result += version_sv;
  }

  result += "\r\n";
  headers.serialize(result);
  return result;
}

inline bool response_header_block::extract(std::string_view raw) {
  // Find end of status line.
  const auto line_end = raw.find("\r\n"sv);
  const std::string_view status_line{
      line_end == npos ? raw : raw.substr(0, line_end)};

  // Parse version: text before first SP.
  const auto sp1 = status_line.find(' ');
  if (sp1 == npos) return false;

  const std::string_view version_sv{status_line.substr(0, sp1)};
  if (version_sv == "HTTP/1.1"sv)
    version = http_version::http_11;
  else if (version_sv == "HTTP/1.0"sv)
    version = http_version::http_10;
  else
    return false;

  // Parse status code: digits between first and second SP.
  const std::string_view after_version{status_line.substr(sp1 + 1)};
  const auto sp2 = after_version.find(' ');
  const std::string_view code_sv{
      sp2 == npos ? after_version : after_version.substr(0, sp2)};
  const auto code = strings::parse_num<int>(code_sv);
  if (!code || *code < 100 || *code > 999) return false;
  status_code = *code;

  // Reason phrase (optional).
  reason =
      sp2 == npos ? std::string{} : std::string{after_version.substr(sp2 + 1)};

  // Parse header lines.
  if (line_end != npos && !headers.extract(raw.substr(line_end + 2)))
    return false;

  return true;
}

// --- stream operators for diagnostics ---

inline std::ostream& operator<<(std::ostream& os, http_version v) {
  switch (v) {
  case http_version::invalid: return os << "invalid";
  case http_version::http_09: return os << "http_09";
  case http_version::http_10: return os << "http_10";
  case http_version::http_11: return os << "http_11";
  }
  return os << "http_version(" << static_cast<int>(v) << ')';
}

inline std::ostream& operator<<(std::ostream& os, http_method m) {
  switch (m) {
  case http_method::invalid: return os << "invalid";
  case http_method::GET: return os << "GET";
  case http_method::HEAD: return os << "HEAD";
  case http_method::POST: return os << "POST";
  case http_method::PUT: return os << "PUT";
  case http_method::DELETE: return os << "DELETE";
  case http_method::OPTIONS: return os << "OPTIONS";
  case http_method::PATCH: return os << "PATCH";
  case http_method::CONNECT: return os << "CONNECT";
  case http_method::TRACE: return os << "TRACE";
  }
  return os << "http_method(" << static_cast<int>(m) << ')';
}

}}} // namespace corvid::proto::http_proto
