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

//!!! #include "../enums.h"
#include "../strings/cases.h"
#include "../strings/conversion.h"
#include "../strings/trimming.h"
#include "../containers/transparent.h"

namespace corvid { inline namespace proto { inline namespace http_proto {

using namespace std::string_view_literals;

// HTTP protocol version.
//
// `invalid` (= 0) is the default-constructed value. It is also returned when
// the request line contains a version token that is not "HTTP/1.0" or
// "HTTP/1.1" (e.g., "HTTP/2.0"). `http_09` is returned when no version
// token is present (HTTP/0.9-style request, where the request line has only
// method and target with no trailing version). `http_10` and `http_11`
// correspond to the recognized version strings.
enum class http_version : uint8_t { invalid, http_09, http_10, http_11 };

}}} // namespace corvid::proto::http_proto

#if 0
template<>
constexpr auto corvid::enums::registry::enum_spec_v<
    corvid::proto::http_proto::http_version> =
    corvid::enums::make_sequence_enum_spec<
        corvid::proto::http_proto::http_version,
        "invalid, HTTP/0.9, HTTP/1.0, HTTP/1.1">();
#endif

namespace corvid { inline namespace proto { inline namespace http_proto {

// HTTP request method.
//
// `invalid` (= 0) is the default-constructed value and is also used for
// unrecognized method tokens. The remaining enumerators are uppercase to
// match the wire representation.
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

#if 0
template<>
constexpr auto corvid::enums::registry::enum_spec_v<
    corvid::proto::http_proto::http_method> =
    corvid::enums::make_sequence_enum_spec<
        corvid::proto::http_proto::http_method,
        "invalid, GET, HEAD, POST, PUT, DELETE, OPTIONS, PATCH, CONNECT, "
        "TRACE">();
#endif

namespace corvid { inline namespace proto { inline namespace http_proto {

// Old-school struct as namespace.
struct http {
  static constexpr auto npos = std::string_view::npos;
  static constexpr auto valid_field_name_chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
      "0123456789-!#$%&'*+.^_`|~"sv;
  static constexpr auto crlf = "\r\n"sv;
  static constexpr auto crlfcrlf = "\r\n\r\n"sv;
  static constexpr auto version10 = "HTTP/1.0"sv;
  static constexpr auto version11 = "HTTP/1.1"sv;
  static constexpr auto GET = "GET"sv;
  static constexpr auto HEAD = "HEAD"sv;
  static constexpr auto POST = "POST"sv;
  static constexpr auto PUT = "PUT"sv;
  static constexpr auto DELETE = "DELETE"sv;
  static constexpr auto OPTIONS = "OPTIONS"sv;
  static constexpr auto PATCH = "PATCH"sv;
  static constexpr auto CONNECT = "CONNECT"sv;
  static constexpr auto TRACE = "TRACE"sv;
};

// Ordered multimap of HTTP header fields with O(1) average lookup.
//
// Insertion order is preserved via `entries_`. A parallel `index_` maps
// canonical header names to index lists into `entries_`, enabling O(1)
// average lookup without linear scan.
//
// Header names are canonicalized in the index to "Content-Type" form.
class http_headers: http {
  using kvp = std::pair<std::string, std::string>;
  using kvp_vector = std::vector<kvp>;
  using index_vector = std::vector<size_t>;
  using index_map = string_unordered_map<index_vector>;

  kvp_vector entries_;
  index_map index_;

public:
  // Canonicalize a header name to title-case-with-hyphens, in place.
  // Only alphanumeric characters, hyphens, and the token special characters
  // are permitted.
  //
  // If any other character is found, `field_name` is left unchanged and
  // `nullopt` is returned. Otherwise, every character is lowercased, unless
  // it's the first character or follows a '-'.  Returns `true` iff
  // `field_name` was changed, `false` if it was already canonical.
  static std::optional<bool> canonicalize(std::string& field_name) {
    if (field_name.find_first_not_of(http::valid_field_name_chars) != npos)
      return std::nullopt;
    bool changed{false};
    bool capitalize{true};
    for (char& c : field_name) {
      // Determine the target character based on the state
      char target = capitalize ? strings::to_upper(c) : strings::to_lower(c);

      // Update 'changed' and the character if they differ
      if (c != target) {
        c = target;
        changed = true;
      }

      // Update state for the next character: capitalize after a hyphen
      capitalize = (c == '-');
    }
    return changed;
  }

public:
  // Add a header, storing `field_name` as-is (no validation or
  // canonicalization). The caller is responsible for providing a valid,
  // canonical name. Returns success. (May fail when too many fields are
  // added.)
  [[nodiscard]] bool add(std::string_view field_name, std::string_view value) {
    const size_t ndx{entries_.size()};
    entries_.emplace_back(field_name, std::string{value});
    index_[std::string{field_name}].push_back(ndx);
    return true;
  }

  // Add a header, canonicalizing `field_name` before storage. Returns success.
  // Fails if `field_name` is empty or contains invalid characters, which
  // merits a "400 Bad Request".
  [[nodiscard]] bool
  add_canonical(std::string_view field_name, std::string_view value) {
    if (field_name.empty()) return false;
    const size_t ndx{entries_.size()};
    std::string canon{field_name};
    if (canon.empty() || !canonicalize(canon)) return false;
    entries_.emplace_back(field_name, std::string{value});
    index_[std::move(canon)].push_back(ndx);
    return true;
  }

  // Return a `string_view` into the stored value for the first entry whose
  // name matches `field_name`. The `field_name` is expected to be canonical.
  // Returns an empty view if not found.
  [[nodiscard]] std::string_view get(
      std::string_view field_name) const noexcept {
    const auto it = index_.find(field_name);
    if (it == index_.end() || it->second.empty()) return {};
    return entries_[it->second.front()].second;
  }

  // Return all values for `field_name` concatenated with `", "`.
  // The `field_name` is expected to be canonical.
  // Returns an empty string if not found.
  [[nodiscard]] std::string combine(std::string_view field_name) const {
    const auto it = index_.find(field_name);
    if (it == index_.end() || it->second.empty()) return {};
    const auto& indices = it->second;
    std::string result;
    bool first{true};
    for (const size_t index : indices) {
      if (!first) result += ", ";
      first = false;
      result += entries_[index].second;
    }
    return result;
  }

  // Process a single header-field line. The caller is responsible for
  // detecting the end-of-headers blank line (empty lines must not be passed
  // here). Returns false for obs-fold, missing colon, empty or invalid field
  // name, or uncanonicalizable name.
  [[nodiscard]] bool extract_line(std::string_view line) {
    assert(!line.empty());
    if (line.front() == ' ' || line.front() == '\t') return false;
    const auto colon = line.find(':');
    if (colon == npos) return false;
    const auto name = line.substr(0, colon);
    const auto value = strings::trim(line.substr(colon + 1));
    return add_canonical(name, value);
  }

  // Parse header-field lines from `header_lines` (the bytes after the first
  // request/response line and its trailing crlf). Empty lines are rejected
  // (RFC 9112 section 2.2 request-smuggling defence). Returns false if any
  // line uses obs-fold, lacks a colon, or has an invalid field name.
  [[nodiscard]] bool extract(std::string_view header_lines) {
    while (!header_lines.empty()) {
      const auto eol = header_lines.find(crlf);
      const std::string_view line{
          eol == npos ? header_lines : header_lines.substr(0, eol)};
      if (line.empty()) return false;
      if (!extract_line(line)) return false;
      if (eol == npos) break;
      header_lines.remove_prefix(eol + 2);
    }
    return true;
  }

  // Append all headers as `"Name: value\r\n"` lines, then append `"\r\n"`.
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
    const auto c = strings::as_lower(get("Connection"));
    if (c == "close") return false;
    if (c == "keep-alive") return true;
    return version == http_version::http_11;
  }

  // Return the `Content-Length` value, or `nullopt` if absent or unparseable.
  [[nodiscard]] std::optional<size_t> content_length() const noexcept {
    const auto sv = get("Content-Length");
    if (sv.empty()) return std::nullopt;
    return strings::parse_num<size_t>(sv);
  }

  // Return true iff `"Transfer-Encoding: chunked"` is present.
  [[nodiscard]] bool is_chunked() const noexcept {
    return strings::as_lower(get("Transfer-Encoding")) == "chunked";
  }

  // TODO: We need some way to walk through all values for a given field name,
  // but this can probably wait for the next version, which links the entries
  // into a list while storing just the head in the index.
};

// Parsed HTTP request header block.
//
// All fields are owned values: no pointers into the recv_buffer. Obtained via
// `extract()` after `terminated_text_parser` delivers the raw header block
// (the bytes before the "\r\n\r\n" sentinel).
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

  // Parse a raw request block. Leading crlf lines are skipped per RFC 9112
  // section 2.2. `raw` may be just the request line (no crlf, no headers),
  // the request line followed by crlf-terminated header lines, or a full
  // block with a trailing crlf as produced by `serialize`. Returns true on
  // success, false if the request line or any header line is malformed.
  [[nodiscard]] bool extract(std::string_view raw);

  // Serialize to `"METHOD target HTTP/1.x\r\nHeaders\r\n\r\n"`.
  // HTTP/0.9 omits the version token. Returns an empty string for
  // `http_method::invalid` or `http_version::invalid`.
  [[nodiscard]] std::string serialize() const;
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

  // Parse a raw response header block (the bytes before the `"\r\n\r\n"`
  // sentinel). Returns true on success, false if the status line is malformed.
  [[nodiscard]] bool extract(std::string_view raw);

  // Produce `"HTTP/1.x code reason\r\nHeaders\r\n\r\n"`.
  // `http_09` and `invalid` versions serialize as `"HTTP/1.1"`.
  // The caller is responsible for adding `Content-Type` and `Content-Length`
  // headers and for sending the body separately.
  [[nodiscard]] std::string serialize() const;
};

// --- implementations ---

inline bool request_header_block::extract(std::string_view raw) {
  // Skip leading CRLF lines (RFC 9112 section 2.2).
  while (raw.starts_with(crlf)) raw.remove_prefix(crlf.size());

  // Find the end of the request line.
  const auto line_end = raw.find(crlf);
  const std::string_view request_line{
      line_end == npos ? raw : raw.substr(0, line_end)};

  // Parse method: first SP.
  const auto sp1 = request_line.find(' ');
  if (sp1 == npos) return false;

  // Parse method token.
  const std::string_view method_sv{request_line.substr(0, sp1)};
  if (method_sv == "GET"sv)
    method = http_method::GET;
  else if (method_sv == "HEAD"sv)
    method = http_method::HEAD;
  else if (method_sv == "POST"sv)
    method = http_method::POST;
  else if (method_sv == "PUT"sv)
    method = http_method::PUT;
  else if (method_sv == "DELETE"sv)
    method = http_method::DELETE;
  else if (method_sv == "OPTIONS"sv)
    method = http_method::OPTIONS;
  else if (method_sv == "PATCH"sv)
    method = http_method::PATCH;
  else if (method_sv == "CONNECT"sv)
    method = http_method::CONNECT;
  else if (method_sv == "TRACE"sv)
    method = http_method::TRACE;
  else
    return false;

  // Parse target and version.
  const std::string_view after_method{request_line.substr(sp1 + 1)};
  const auto sp2 = after_method.find(' ');
  if (sp2 == npos) {
    // No second SP: HTTP/0.9-style, no version token.
    target = std::string{after_method};
    version = http_version::http_09;
  } else {
    target = std::string{after_method.substr(0, sp2)};
    const std::string_view version_sv{after_method.substr(sp2 + 1)};
    if (version_sv == "HTTP/1.1"sv)
      version = http_version::http_11;
    else if (version_sv == "HTTP/1.0"sv)
      version = http_version::http_10;
    else
      return false;
  }

  const std::string_view header_lines{
      line_end == npos
          ? std::string_view{}
          : raw.substr(line_end + crlf.size())};

  // HTTP/0.9: no headers allowed.
  if (version == http_version::http_09) return header_lines.empty();

  // HTTP/1.x: headers required.
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
