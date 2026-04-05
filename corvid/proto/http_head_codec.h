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
#include "../strings/splitting.h"
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
enum class http_version : uint8_t { invalid, http_0_9, http_1_0, http_1_1 };

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

// Connection disposition: whether to keep a connection alive after responding
// or to close it.
enum class after_response : uint8_t { close = 0, keep_alive = 1 };

// Canonical media type from a `Content-Type` header field.
// `unknown`: header present but value not recognized.
enum class content_type_value : uint8_t {
  unknown,
  text_html,
  text_plain,
  application_json
};

// Transfer encoding from a `Transfer-Encoding` header field.
// `unknown`: header present but value not recognized.
enum class transfer_encoding_value : uint8_t { unknown, identity, chunked };

// Upgrade protocol from an `Upgrade` header field.
// `unknown`: header present but value not recognized.
enum class upgrade_value : uint8_t { unknown, websocket };

// HTTP response status code.
enum class http_status_code : uint16_t {
  invalid = 0,
  CONTINUE = 100,
  SWITCHING_PROTOCOLS = 101,
  PROCESSING = 102,
  EARLY_HINTS = 103,
  OK = 200,
  CREATED = 201,
  ACCEPTED = 202,
  NON_AUTHORITATIVE_INFORMATION = 203,
  NO_CONTENT = 204,
  RESET_CONTENT = 205,
  PARTIAL_CONTENT = 206,
  MULTIPLE_CHOICES = 300,
  MOVED_PERMANENTLY = 301,
  FOUND = 302,
  SEE_OTHER = 303,
  NOT_MODIFIED = 304,
  USE_PROXY = 305,
  TEMPORARY_REDIRECT = 307,
  PERMANENT_REDIRECT = 308,
  BAD_REQUEST = 400,
  UNAUTHORIZED = 401,
  PAYMENT_REQUIRED = 402,
  FORBIDDEN = 403,
  NOT_FOUND = 404,
  METHOD_NOT_ALLOWED = 405,
  NOT_ACCEPTABLE = 406,
  PROXY_AUTHENTICATION_REQUIRED = 407,
  REQUEST_TIMEOUT = 408,
  CONFLICT = 409,
  GONE = 410,
  LENGTH_REQUIRED = 411,
  PRECONDITION_FAILED = 412,
  CONTENT_TOO_LARGE = 413,
  URI_TOO_LONG = 414,
  UNSUPPORTED_MEDIA_TYPE = 415,
  RANGE_NOT_SATISFIABLE = 416,
  EXPECTATION_FAILED = 417,
  IM_A_TEAPOT = 418,
  MISDIRECTED_REQUEST = 421,
  UNPROCESSABLE_CONTENT = 422,
  LOCKED = 423,
  FAILED_DEPENDENCY = 424,
  TOO_EARLY = 425,
  UPGRADE_REQUIRED = 426,
  PRECONDITION_REQUIRED = 428,
  TOO_MANY_REQUESTS = 429,
  REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
  UNAVAILABLE_FOR_LEGAL_REASONS = 451,
  INTERNAL_SERVER_ERROR = 500,
  NOT_IMPLEMENTED = 501,
  BAD_GATEWAY = 502,
  SERVICE_UNAVAILABLE = 503,
  GATEWAY_TIMEOUT = 504,
  HTTP_VERSION_NOT_SUPPORTED = 505,
  VARIANT_ALSO_NEGOTIATES = 506,
  INSUFFICIENT_STORAGE = 507,
  LOOP_DETECTED = 508,
  NOT_EXTENDED = 510,
  NETWORK_AUTHENTICATION_REQUIRED = 511,
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

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::http_proto::after_response> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::proto::http_proto::after_response, "close, keep-alive">();

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::http_proto::http_status_code> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::proto::http_proto::http_status_code, "invalid">();

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::http_proto::content_type_value> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::proto::http_proto::content_type_value,
        "unknown, text/html, text/plain, application/json">();

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::http_proto::transfer_encoding_value> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::proto::http_proto::transfer_encoding_value,
        "unknown, identity, chunked">();

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::http_proto::upgrade_value> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::proto::http_proto::upgrade_value, "unknown, websocket">();

namespace corvid { inline namespace proto { inline namespace http_proto {

// Old-school struct as namespace.
struct http_constants {
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
class http_headers: http_constants {
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
  // A lazy, non-owning view over all field values for a given field name,
  // returned by `get_values`. Iterates in insertion order.
  class value_range {
  public:
    class iterator {
    public:
      using iterator_category = std::forward_iterator_tag;
      using value_type = std::string_view;
      using difference_type = std::ptrdiff_t;
      using reference = std::string_view;
      using pointer = void;

      [[nodiscard]] const std::string& operator*() const noexcept {
        return (*entries_)[*it_].value;
      }
      [[nodiscard]] const std::string& get_value() const noexcept {
        return (*entries_)[*it_].value;
      }
      [[nodiscard]] const std::string& get_name() const noexcept {
        return (*entries_)[*it_].name;
      }
      // Replace the value of the entry this iterator points at.
      void set(std::string value) {
        (*entries_)[*it_].value = std::move(value);
      }
      void tombstone() { (*entries_)[*it_].name.clear(); }

      iterator& operator++() noexcept {
        ++it_;
        skip_tombstones();
        return *this;
      }
      iterator operator++(int) noexcept {
        auto t = *this;
        ++(*this);
        return t;
      }
      [[nodiscard]] bool operator==(const iterator&) const noexcept = default;

    private:
      friend class value_range;
      field_line_vector* entries_{};
      index_vector::const_iterator it_;
      index_vector::const_iterator end_;

      iterator(field_line_vector* entries, index_vector::const_iterator it,
          index_vector::const_iterator end) noexcept
          : entries_{entries}, it_{it}, end_{end} {
        skip_tombstones();
      }

      void skip_tombstones() noexcept {
        while (it_ != end_ && (*entries_)[*it_].name.empty()) ++it_;
      }
    };

    [[nodiscard]] iterator begin() const noexcept {
      return {entries_, indices_->begin(), indices_->end()};
    }
    [[nodiscard]] iterator end() const noexcept {
      return {entries_, indices_->end(), indices_->end()};
    }
    // Note that these do not account for tombstones, so the actual count could
    // be lower.
    [[nodiscard]] bool empty() const noexcept { return indices_->empty(); }
    [[nodiscard]] size_t size() const noexcept { return indices_->size(); }

  private:
    friend class http_headers;
    static inline const index_vector empty_{};

    field_line_vector* entries_;
    const index_vector* indices_;

    value_range(field_line_vector& entries,
        const index_vector& indices) noexcept
        : entries_{&entries}, indices_{&indices} {}
  };

  // Normalize a field name to Train-Case, in place. Only alphanumeric
  // characters, hyphens, and the token special characters are permitted.
  //
  // If any other character is found, `field_name` is left unchanged and
  // `std::nullopt` is returned to signal error.
  //
  // Otherwise, every character is lowercased, unless it's the first
  // character or follows a '-'. Returns `true` iff `field_name` was changed,
  // `false` if it was already normalized.
  //
  // Note: This is fully compliant with RFC 9110, but the modern practice,
  // which is strictly required for HTTP/2.0 and HTTP/3.0, is to lowercase
  // all field names.
  static std::optional<bool> normalize(std::string& field_name) {
    if (field_name.empty() ||
        (field_name.find_first_not_of(http_headers::valid_field_name_chars) !=
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
    return normalize(normalized_field_name) == std::optional{false};
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

  // Add a field line from parts, storing `field_name` and `field_value`
  // as-is (no validation or normalization). If specified `raw_field_name` is
  // stored for the line, while `field_name` is stored for the index.
  // Otherwise, the two are the same.
  //
  // The use case for this method is when the caller has already validated
  // and normalized the field name and value, such as when they're hardcoded.
  //
  // The caller is responsible for providing a valid,
  // normalized field name and a valid field value. Fails if too many fields
  // are added.
  [[nodiscard]] bool add_raw(std::string_view field_name,
      std::string field_value, std::string_view raw_field_name = {}) {
    assert(is_normalized(field_name) && is_valid_field_value(field_value));
    if (raw_field_name.empty()) raw_field_name = field_name;
    entries_.push_back({std::string{raw_field_name}, std::move(field_value)});
    index_[std::string{field_name}].push_back(entries_.size() - 1);
    return true;
  }

  // Set `field_name` to a single `field_value`, replacing all existing
  // entries for that field. If the field already exists, the first entry is
  // updated in place (preserving its position and normalizing its name);
  // any additional entries are tombstoned. Returns true in this case.
  // If no entry exists, a new one is appended and false is returned.
  // The caller is responsible for providing a valid, normalized field name
  // and a valid field value.
  [[nodiscard]] bool
  reset_raw(std::string_view field_name, std::string field_value) {
    assert(is_normalized(field_name) && is_valid_field_value(field_value));
    auto ids = find_opt(index_, field_name);
    if (!ids) return add_raw(field_name, std::move(field_value)) && false;
    entries_[ids->front()].name = std::string{field_name};
    entries_[ids->front()].value = std::move(field_value);
    for (size_t i = 1; i < ids->size(); ++i) entries_[(*ids)[i]].name.clear();
    ids->resize(1);
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
    return add_raw(normal_field_name, std::string{field_value}, field_name);
  }

  // Return a `string_view` into the field value for the field line whose
  // name matches `field_name`. The `field_name` is expected to be
  // normalized. Returns `std::nullopt` if not found, as opposed to empty if
  // it was found with an empty value.
  [[nodiscard]] std::optional<std::string_view> get(
      std::string_view field_name) const {
    assert(is_normalized(field_name));
    if (auto ids = find_opt(index_, field_name); ids)
      for (size_t ndx : *ids)
        if (!entries_[ndx].name.empty()) return entries_[ndx].value;
    return std::nullopt;
  }

  // Return all values for `field_name`, concatenated with `", "`. The
  // `field_name` is expected to be normalized. Returns an empty string if
  // not found.
  [[nodiscard]] std::string get_combined(std::string_view field_name) const {
    assert(is_normalized(field_name));
    auto ids = find_opt(index_, field_name);
    if (!ids || ids->empty()) return {};
    std::string result;
    result.reserve(128);
    for (const size_t ndx : *ids) {
      if (entries_[ndx].name.empty()) continue;
      result += entries_[ndx].value;
      result += ", ";
    }
    if (!result.empty()) result.resize(result.size() - 2);
    return result;
  }

  // Return a non-allocating range over all values for `field_name` in
  // insertion order. The `field_name` is expected to be normalized.
  // Returns an empty range if not found.
  [[nodiscard]] value_range get_values(std::string_view field_name) {
    assert(is_normalized(field_name));
    auto ids = find_opt(index_, field_name);
    const auto& ref = ids ? *ids : value_range::empty_;
    return {entries_, ref};
  }

  // Add a field line by parsing `line`, which does not include the trailing
  // crlf. Must not be empty: the caller is responsible for detecting the
  // end-of-headers blank line. The field name is required to be non-empty
  // and must be followed by a colon, but the field value is optional, and
  // may be whitespace-padded. Returns false for obs-fold, missing colon,
  // empty or invalid field name.
  [[nodiscard]] bool add_line(std::string_view line) {
    if (line.empty()) return false;
    if (line.front() == ' ' || line.front() == '\t') return false;
    auto found = strings::token_parser::next_terminated(":", line);
    if (!found) return false;
    const auto name = *found;
    const auto value = strings::trim(line);
    return add(name, value);
  }

  // Add multiple field lines by parsing `header_lines`, which is the block
  // of text after the first request/response line and its trailing crlf, up
  // to but not including the crlfcrlf sentinel. Each line, except the last,
  // must end with a crlf. Empty lines are rejected (RFC 9112 section 2.2
  // request-smuggling defence). Returns false if any line uses obs-fold,
  // lacks a colon, or either the field name or value is invalid.
  [[nodiscard]] bool add_lines(std::string_view header_lines) {
    strings::token_parser parser{crlf};
    std::string_view line;
    do {
      line = parser.next_delimited(header_lines);
      if (!add_line(line)) return false;
    } while (!header_lines.empty());
    return true;
  }

  // Serialize all headers into wire format, which is the block of text after
  // the request/status line. Tombstoned entries (empty name) are skipped.
  //
  // Makes no attempt to encode the field values or reserve adequate space.
  void serialize(std::string& out) const {
    for (const auto& [name, value] : entries_) {
      if (name.empty()) continue; // tombstoned by `remove_key`
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

  // Tombstone all entries for the normalized `field_name` and remove it
  // from the index entirely.
  void remove_key(std::string_view field_name) {
    assert(is_normalized(field_name));
    auto it = index_.find(std::string{field_name});
    if (it == index_.end()) return;
    for (size_t ndx : it->second) entries_[ndx].name.clear();
    index_.erase(it);
  }
};

// Parsed representation of key HTTP header fields. Populated from an
// `http_headers` instance after parsing via `extract`, and written back
// before serialization via `apply`.
//
// `std::nullopt` means the corresponding header was absent. For enum fields,
// `unknown` means the header was present but the value was not recognized.
struct http_options {
  std::optional<after_response> connection;
  std::optional<size_t> content_length;
  std::optional<content_type_value> content_type;
  std::optional<transfer_encoding_value> transfer_encoding;
  std::optional<upgrade_value> upgrade;

  // Populate all fields by parsing `headers`. Call after headers are parsed.
  void extract(http_headers& headers) {
    bool has_upgrade = do_extract_connection(headers);
    do_extract_content_length(headers);
    do_extract_content_type(headers);
    do_extract_transfer_encoding(headers);
    do_extract_upgrade(headers);
    // If the `Connection` header did not explicitly specify "Upgrade", then
    // any `Upgrade` header is ignored, per RFC 9112 section 6.7.1.
    if (!has_upgrade) upgrade.reset();
  }

  // Write non-null values into `headers`, replacing existing entries.
  // Enum fields set to `unknown` are not written. Call before serializing.
  void apply(http_headers& headers) const {
    if (connection)
      (void)headers.reset_raw("Connection",
          strings::enum_as_string(*connection));
    if (content_length)
      (void)headers.reset_raw("Content-Length",
          std::to_string(*content_length));
    if (content_type && *content_type != content_type_value::unknown)
      (void)headers.reset_raw("Content-Type",
          strings::enum_as_string(*content_type));
    if (transfer_encoding &&
        *transfer_encoding != transfer_encoding_value::unknown)
      (void)headers.reset_raw("Transfer-Encoding",
          strings::enum_as_string(*transfer_encoding));
    if (upgrade && *upgrade != upgrade_value::unknown)
      (void)headers.reset_raw("Upgrade", strings::enum_as_string(*upgrade));
  }

  // Return the resolved connection disposition, applying the HTTP version
  // default when `connection` is `std::nullopt`. HTTP/0.9 always returns
  // `close` regardless of any `Connection` header.
  [[nodiscard]] after_response keep_alive(
      http_version version) const noexcept {
    if (version == http_version::http_0_9) return after_response::close;
    if (connection) return *connection;
    return version == http_version::http_1_1
               ? after_response::keep_alive
               : after_response::close;
  }

private:
  bool do_extract_connection(http_headers& headers) {
    bool has_close{};
    bool has_keep_alive{};
    bool has_upgrade{};
    std::string t;
    for (const auto& val : headers.get_values("Connection")) {
      for (auto token : strings::split(val, ",")) {
        t = strings::trim(token);
        strings::to_lower(t);
        after_response ar{};
        if (strings::convert_text_enum(ar, t)) {
          if (ar == after_response::close)
            has_close = true;
          else if (ar == after_response::keep_alive)
            has_keep_alive = true;
        }
        if (t == "upgrade") {
          has_keep_alive = true;
          has_upgrade = true;
        }
      }
    }
    if (has_close)
      connection = after_response::close;
    else if (has_keep_alive)
      connection = after_response::keep_alive;
    return has_upgrade;
  }

  void do_extract_content_length(const http_headers& headers) {
    const auto sv = headers.get("Content-Length");
    if (sv) content_length = strings::parse_num<size_t>(*sv);
  }

  void do_extract_content_type(const http_headers& headers) {
    const auto sv = headers.get("Content-Type");
    if (!sv) return;
    // Strip parameters (e.g., `"; charset=utf-8"`) before matching.
    const auto media_type = strings::trim(strings::split(*sv, ";").front());
    content_type_value ct{};
    content_type =
        strings::convert_text_enum(ct, strings::as_lower(media_type))
            ? ct
            : content_type_value::unknown;
  }

  // This can be a list of compression types, none of which we support, but
  // "chunked" must always be the last encoding applied, if present. Multiple
  // `Transfer-Encoding` fields are treated as a single concatenated list, so
  // only the last token of the last field determines whether chunked is
  // active.
  void do_extract_transfer_encoding(http_headers& headers) {
    std::string t;
    for (const auto& val : headers.get_values("Transfer-Encoding")) {
      if (val.empty()) continue;
      const auto encodings = strings::split(val, ",");
      if (encodings.empty()) continue;
      auto v = strings::trim(encodings.back());
      if (v.empty()) continue;
      t = v;
    }
    if (t.empty()) return;
    strings::to_lower(t);
    transfer_encoding_value te{};
    if (strings::convert_text_enum(te, t) &&
        te == transfer_encoding_value::chunked)
      transfer_encoding = te;
  }

  void do_extract_upgrade(http_headers& headers) {
    std::string t;
    for (const auto& val : headers.get_values("Upgrade")) {
      for (auto token : strings::split(val, ",")) {
        auto v = strings::trim(token);
        if (v.empty()) continue;
        t = v;
        strings::to_lower(t);
        upgrade_value up{};
        (void)strings::convert_text_enum(up, t);
        if (up == upgrade_value::websocket || !upgrade) upgrade = up;
      }
    }
  }
};

// Shared base for `request_head` and `response_head`. Holds the fields common
// to both: the HTTP version, the parsed header fields, and the extracted
// options.
struct head_base: http_constants {
  http_version version{};
  http_headers headers;
  http_options options;
};

// HTTP request head, consisting of the request line and the header fields.
// The body is not included.
//
// Obtained via `parse` after `terminated_text_parser` delivers the head (the
// bytes before the crlfcrlf sentinel).
struct request_head: head_base {
  http_method method{};
  std::string target;

  // Reset to default-constructed state.
  void clear() {
    version = {};
    headers = {};
    options = {};
    method = {};
    target.clear();
  }

  // Parse the head of a request, containing the request line and the
  // headers. This is a "Full Request", per RFC. Essentially, it is the text
  // up to but not including the trailing crlfcrlf.
  //
  // For HTTP/1.1, the headers that follow the request line are required. For
  // HTTP/1.0, the headers can be empty, but the crlfcrlf sentinel is still
  // required. For HTTP/0.9, headers are not allowed, and there is no
  // crlfcrlf sentinel; it ends at the crlf of the request line.
  //
  // To support these variations, this method can parse just the request line
  // when that's all that's passed. Also, it makes no attempt to parse
  // headers when it's HTTP/0.9. It is up to the caller to look at both the
  // version and the headers to decide whether the combination is valid.
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
      version = http_version::http_0_9;
    else if (!strings::convert_text_enum(version, version_sv) ||
             version == http_version::invalid)
      return fail("Invalid HTTP version");

    // HTTP/0.9: no headers allowed.
    auto header_lines = head;
    if (version == http_version::http_0_9) {
      if (!header_lines.empty())
        return fail("HTTP/0.9 does not allow headers");
      return true;
    }

    // HTTP/1.x: headers required, but that requirement is enforced by
    // `http_server`.
    if (header_lines.empty()) return true;

    if (!headers.add_lines(header_lines))
      return fail("Malformed header lines");
    options.extract(headers);
    return true;
  }

  // Serialize to `"METHOD target HTTP/1.x\r\nHeaders\r\n\r\n"`.
  // HTTP/0.9 omits the version token. Returns an empty string for
  // `http_version::invalid`.
  [[nodiscard]] std::string serialize() {
    std::string result;
    if (version == http_version::invalid) return result;
    options.apply(headers);
    result += strings::enum_as_string(method);
    result += ' ';
    result += target;

    if (version != http_version::http_0_9) {
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

// HTTP response head, consisting of the status line and the header fields.
// The body is not included.
//
// Populate the fields and call `serialize()` to produce the wire-format
// response string to pass to `stream_conn::send()`.
struct response_head: head_base {
  http_status_code status_code{};
  std::string reason;

  // Reset to default-constructed state.
  void clear() {
    version = {};
    headers = {};
    options = {};
    status_code = {};
    reason.clear();
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

    // The reason after the status code is optional, but the trailing space
    // is required, hence it's a terminator, not a delimiter.
    auto status_code_sv_opt = space_parser.next_terminated(status_line);
    if (!status_code_sv_opt) return fail("Status code not terminated by SP");
    auto status_code_sv = *status_code_sv_opt;

    if (!strings::convert_enum(status_code, status_code_sv) ||
        *status_code < 100 || *status_code > 999)
      return fail("Invalid status code");

    reason = std::string{status_line};

    // Headers are not strictly required, but the trailing crlfcrlf is.
    auto header_lines = head;
    if (header_lines.empty()) return true;

    if (!headers.add_lines(header_lines))
      return fail("Malformed header lines");
    options.extract(headers);
    return true;
  }

  // Serialize to `"HTTP/1.x code reason\r\nHeaders\r\n\r\n"`.
  // Returns an empty string for `http_version::invalid`.
  [[nodiscard]] std::string serialize() {
    std::string result;
    if (version == http_version::invalid) return result;
    options.apply(headers);

    result += strings::enum_as_string(version);
    result += ' ';
    result += std::to_string(static_cast<int>(status_code));
    result += ' ';
    result += reason;
    result += "\r\n";

    headers.serialize(result);
    return result;
  }

  // Build a minimal HTTP/1.1 error response with no body. Used when the
  // server needs to respond before a `request_head` is available
  // (e.g., parse failure).
  [[nodiscard]] static std::string
  make_error_response(after_response keep_alive = after_response::close,
      http_version version = http_version::http_1_1,
      http_status_code code = http_status_code::BAD_REQUEST,
      std::string_view phrase = "Bad Request") {
    response_head resp;
    resp.version = version;
    resp.status_code = code;
    resp.reason = std::string{phrase};
    resp.options.connection = keep_alive;
    resp.options.content_length = 0;
    return resp.serialize();
  }

private:
  bool fail(std::string failure) {
    reason = std::move(failure);
    return false;
  }
};
}}} // namespace corvid::proto::http_proto
