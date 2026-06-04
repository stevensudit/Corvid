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
#include <cstdint>
#include <cstddef>
#include <concepts>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <nghttp3/nghttp3.h>

#include "../../containers/opt_find.h"
#include "../../enums/bitmask_enum.h"
#include "../../enums/sequence_enum.h"

// HTTP/3 header-field vocabulary: the QPACK field-name tokens, the canonical
// field-name strings, the per-field QPACK flags, the stream-FIN marker, and
// the `http3_field` submit type.

namespace corvid { inline namespace proto { namespace quic {

using namespace std::string_view_literals;

#pragma region qpack_token

// Wrapper over `nghttp3_qpack_token`, the well-known HTTP field-name tokens
// nghttp3 reports in `on_recv_header` so the application can identify a known
// header name without a string compare. The values mirror
// `nghttp3_qpack_token` one-for-one. The pseudo-header tokens, whose C names
// carry a doubled underscore for the leading ':', are spelled here without the
// leading underscore (`authority` is `:authority`, and so on). `unknown`
// (`-1`) is the sentinel nghttp3 passes when the field name is not one it
// recognizes.
// NOLINTNEXTLINE(performance-enum-size)
enum class qpack_token : int32_t {
  unknown = -1,
  authority = NGHTTP3_QPACK_TOKEN__AUTHORITY, // :authority
  method = NGHTTP3_QPACK_TOKEN__METHOD,       // :method
  path = NGHTTP3_QPACK_TOKEN__PATH,           // :path
  scheme = NGHTTP3_QPACK_TOKEN__SCHEME,       // :scheme
  status = NGHTTP3_QPACK_TOKEN__STATUS,       // :status
  accept = NGHTTP3_QPACK_TOKEN_ACCEPT,
  accept_encoding = NGHTTP3_QPACK_TOKEN_ACCEPT_ENCODING,
  accept_language = NGHTTP3_QPACK_TOKEN_ACCEPT_LANGUAGE,
  accept_ranges = NGHTTP3_QPACK_TOKEN_ACCEPT_RANGES,
  access_control_allow_credentials =
      NGHTTP3_QPACK_TOKEN_ACCESS_CONTROL_ALLOW_CREDENTIALS,
  access_control_allow_headers =
      NGHTTP3_QPACK_TOKEN_ACCESS_CONTROL_ALLOW_HEADERS,
  access_control_allow_methods =
      NGHTTP3_QPACK_TOKEN_ACCESS_CONTROL_ALLOW_METHODS,
  access_control_allow_origin =
      NGHTTP3_QPACK_TOKEN_ACCESS_CONTROL_ALLOW_ORIGIN,
  access_control_expose_headers =
      NGHTTP3_QPACK_TOKEN_ACCESS_CONTROL_EXPOSE_HEADERS,
  access_control_request_headers =
      NGHTTP3_QPACK_TOKEN_ACCESS_CONTROL_REQUEST_HEADERS,
  access_control_request_method =
      NGHTTP3_QPACK_TOKEN_ACCESS_CONTROL_REQUEST_METHOD,
  age = NGHTTP3_QPACK_TOKEN_AGE,
  alt_svc = NGHTTP3_QPACK_TOKEN_ALT_SVC,
  authorization = NGHTTP3_QPACK_TOKEN_AUTHORIZATION,
  cache_control = NGHTTP3_QPACK_TOKEN_CACHE_CONTROL,
  content_disposition = NGHTTP3_QPACK_TOKEN_CONTENT_DISPOSITION,
  content_encoding = NGHTTP3_QPACK_TOKEN_CONTENT_ENCODING,
  content_length = NGHTTP3_QPACK_TOKEN_CONTENT_LENGTH,
  content_security_policy = NGHTTP3_QPACK_TOKEN_CONTENT_SECURITY_POLICY,
  content_type = NGHTTP3_QPACK_TOKEN_CONTENT_TYPE,
  cookie = NGHTTP3_QPACK_TOKEN_COOKIE,
  date = NGHTTP3_QPACK_TOKEN_DATE,
  early_data = NGHTTP3_QPACK_TOKEN_EARLY_DATA,
  etag = NGHTTP3_QPACK_TOKEN_ETAG,
  expect_ct = NGHTTP3_QPACK_TOKEN_EXPECT_CT,
  forwarded = NGHTTP3_QPACK_TOKEN_FORWARDED,
  if_modified_since = NGHTTP3_QPACK_TOKEN_IF_MODIFIED_SINCE,
  if_none_match = NGHTTP3_QPACK_TOKEN_IF_NONE_MATCH,
  if_range = NGHTTP3_QPACK_TOKEN_IF_RANGE,
  last_modified = NGHTTP3_QPACK_TOKEN_LAST_MODIFIED,
  link = NGHTTP3_QPACK_TOKEN_LINK,
  location = NGHTTP3_QPACK_TOKEN_LOCATION,
  origin = NGHTTP3_QPACK_TOKEN_ORIGIN,
  purpose = NGHTTP3_QPACK_TOKEN_PURPOSE,
  range = NGHTTP3_QPACK_TOKEN_RANGE,
  referer = NGHTTP3_QPACK_TOKEN_REFERER,
  server = NGHTTP3_QPACK_TOKEN_SERVER,
  set_cookie = NGHTTP3_QPACK_TOKEN_SET_COOKIE,
  strict_transport_security = NGHTTP3_QPACK_TOKEN_STRICT_TRANSPORT_SECURITY,
  timing_allow_origin = NGHTTP3_QPACK_TOKEN_TIMING_ALLOW_ORIGIN,
  upgrade_insecure_requests = NGHTTP3_QPACK_TOKEN_UPGRADE_INSECURE_REQUESTS,
  user_agent = NGHTTP3_QPACK_TOKEN_USER_AGENT,
  vary = NGHTTP3_QPACK_TOKEN_VARY,
  x_content_type_options = NGHTTP3_QPACK_TOKEN_X_CONTENT_TYPE_OPTIONS,
  x_forwarded_for = NGHTTP3_QPACK_TOKEN_X_FORWARDED_FOR,
  x_frame_options = NGHTTP3_QPACK_TOKEN_X_FRAME_OPTIONS,
  x_xss_protection = NGHTTP3_QPACK_TOKEN_X_XSS_PROTECTION,
  host = NGHTTP3_QPACK_TOKEN_HOST,
  connection = NGHTTP3_QPACK_TOKEN_CONNECTION,
  keep_alive = NGHTTP3_QPACK_TOKEN_KEEP_ALIVE,
  proxy_connection = NGHTTP3_QPACK_TOKEN_PROXY_CONNECTION,
  transfer_encoding = NGHTTP3_QPACK_TOKEN_TRANSFER_ENCODING,
  upgrade = NGHTTP3_QPACK_TOKEN_UPGRADE,
  te = NGHTTP3_QPACK_TOKEN_TE,
  protocol = NGHTTP3_QPACK_TOKEN__PROTOCOL, // :protocol
  priority = NGHTTP3_QPACK_TOKEN_PRIORITY,
};
consteval auto corvid_enum_spec(qpack_token*) {
  return corvid::enums::sequence::make_sequence_enum_spec<qpack_token,
      "0,:authority,:method|8,:path,:scheme,,:status|25,accept,,accept-"
      "encoding,accept-language,accept-ranges,access-control-allow-"
      "credentials,,access-control-allow-headers|35,access-control-allow-"
      "methods|38,access-control-allow-origin,access-control-expose-headers,"
      "access-control-request-headers,access-control-request-method,,age,alt-"
      "svc,authorization,cache-control|52,content-disposition,content-"
      "encoding,,content-length,content-security-policy,content-type|68,"
      "cookie,date,early-data,etag,expect-ct,forwarded,if-modified-since,if-"
      "none-match,if-range,last-modified,link,location,origin,purpose,range,"
      "referer,server,set-cookie,strict-transport-security|89,timing-allow-"
      "origin,upgrade-insecure-requests,user-agent,vary,,x-content-type-"
      "options,x-forwarded-for,x-frame-options,,x-xss-protection|1000,host,"
      "connection,keep-alive,proxy-connection,transfer-encoding,upgrade,te,"
      ":protocol,priority">();
}

#pragma endregion
#pragma region nv_flags

// Flags accompanying a decoded HTTP field in `on_recv_header`, mirroring the
// `NGHTTP3_NV_FLAG_*` set. `never_index` marks a sensitive field the peer
// asked never to be QPACK-indexed; it is the only flag normally seen on the
// receive path. `no_copy_name`, `no_copy_value`, and `try_index` are
// encoder-side hints the application sets when submitting fields, included
// here for completeness of the wrapped set.
enum class nv_flags : uint8_t {
  none = NGHTTP3_NV_FLAG_NONE,                   // 0x00
  never_index = NGHTTP3_NV_FLAG_NEVER_INDEX,     // 0x01
  no_copy_name = NGHTTP3_NV_FLAG_NO_COPY_NAME,   // 0x02
  no_copy_value = NGHTTP3_NV_FLAG_NO_COPY_VALUE, // 0x04
  try_index = NGHTTP3_NV_FLAG_TRY_INDEX          // 0x08
};
consteval auto corvid_enum_spec(nv_flags*) {
  return corvid::enums::bitmask::make_bitmask_enum_spec<nv_flags,
      "try_index,no_copy_value,no_copy_name,never_index">();
}

#pragma endregion
#pragma region stream_chunk

// Whether a chunk of stream data is the last the sender will put on this
// stream (carries the QUIC stream FIN) or whether more may follow.
enum class stream_chunk : uint8_t {
  more = 0,
  fin = 1,
};
consteval auto corvid_enum_spec(stream_chunk*) {
  return corvid::enums::sequence::make_sequence_enum_spec<stream_chunk,
      "more,fin">();
}

#pragma endregion
#pragma region http3_method

// The HTTP/3 methods, for use in the `:method` pseudo-header.
enum class http3_method : uint8_t {
  invalid,
  ACL,
  BASELINE_CONTROL,
  BIND,
  CHECKIN,
  CHECKOUT,
  CONNECT,
  COPY,
  DELETE,
  GET,
  HEAD,
  LABEL,
  LINK,
  LOCK,
  MERGE,
  MKACTIVITY,
  MKCALENDAR,
  MKCOL,
  MKREDIRECTREF,
  MKWORKSPACE,
  MOVE,
  OPTIONS,
  ORDERPATCH,
  PATCH,
  POST,
  PRI,
  PROPFIND,
  PROPPATCH,
  PUT,
  QUERY,
  REBIND,
  REPORT,
  SEARCH,
  TRACE,
  UNBIND,
  UNCHECKOUT,
  UNLINK,
  UNLOCK,
  UPDATE,
  UPDATEREDIRECTREF,
  VERSION_CONTROL
};
consteval auto corvid_enum_spec(http3_method*) {
  return corvid::enums::sequence::make_sequence_enum_spec<http3_method,
      "ACL,BASELINE-CONTROL,BIND,CHECKIN,CHECKOUT,CONNECT,COPY,DELETE,GET,"
      "HEAD,LABEL,LINK,LOCK,MERGE,MKACTIVITY,MKCALENDAR,MKCOL,MKREDIRECTREF,"
      "MKWORKSPACE,MOVE,OPTIONS,ORDERPATCH,PATCH,POST,PRI,PROPFIND,PROPPATCH,"
      "PUT,QUERY,REBIND,REPORT,SEARCH,TRACE,UNBIND,UNCHECKOUT,UNLINK,UNLOCK,"
      "UPDATE,UPDATEREDIRECTREF,VERSION-CONTROL",
      wrapclip{}, http3_method{1}>();
}

#pragma endregion

#pragma region header_name

// Compile-time checked header name, with associated QPACK token if it's a
// known name.
using header_name = enums::sequence::enum_name<qpack_token>;
using header_name_and_enum = enums::sequence::enum_named_value<qpack_token>;

namespace http3_literals {
// HTTP/3 Header field literal.
consteval header_name operator""_header(const char* s, std::size_t n) {
  return header_name{s, n};
}

} // namespace http3_literals

#pragma endregion
#pragma region method_name

// Compile-time checked HTTP method name, for use in the `:method`
// pseudo-header.
using method_name = enums::sequence::enum_name<http3_method>;

namespace http3_literals {
consteval method_name operator""_method(const char* s, std::size_t n) {
  return method_name{s, n};
}
} // namespace http3_literals

#pragma endregion
#pragma region http3_field

// An HTTP field with owned storage.
struct http3_field {
  qpack_token token{qpack_token::unknown};
  std::string name;
  std::string value;
  nv_flags flags{nv_flags::none};

  static http3_field make(header_name_and_enum key, std::string_view value,
      nv_flags flags = nv_flags::none) {
    return {key.as_enum(), std::string{key}, std::string{value}, flags};
  }
};

#pragma endregion
#pragma region http3_headers

// A collection of HTTP fields, with support for the QPACK token lookups.
class http3_headers {
public:
  static constexpr size_t npos = static_cast<size_t>(-1);

#pragma region Construction

  http3_headers() = default;
  http3_headers(const http3_headers&) = delete;
  http3_headers(http3_headers&&) = default;
  http3_headers& operator=(const http3_headers&) = delete;
  http3_headers& operator=(http3_headers&&) = default;

#pragma endregion
#pragma region Accessors

  [[nodiscard]] auto chunk_fin() const noexcept { return chunk_fin_; }
  void set_chunk_fin(stream_chunk chunk_fin) noexcept {
    chunk_fin_ = chunk_fin;
  }

  // Add a field with name and token. Returns its index.
  size_t add(header_name_and_enum key, std::string_view value,
      nv_flags flags = nv_flags::none) {
    return add(key, std::string{value}, flags);
  }
  template<std::same_as<std::string> S>
  size_t
  add(header_name_and_enum key, S&& value, nv_flags flags = nv_flags::none) {
    const auto token = key.as_enum();
    const auto name = std::string_view{key};
    // NOLINTNEXTLINE(bugprone-move-forwarding-reference)
    fields_.emplace_back(token, std::string{name}, std::move(value), flags);
    return fields_.size() - 1;
  }

  // Add a field. Returns its index.
  size_t add(const http3_field& field) {
    return add(header_name_and_enum::force(field.name, field.token),
        field.value, field.flags);
  }

  // Set the field `value` (and `flags`): modify the first existing field with
  // that name if one is present, otherwise add a new one. Returns index.
  size_t set_value(header_name_and_enum key, std::string_view value,
      nv_flags flags = nv_flags::none) {
    return set_value(key, std::string{value}, flags);
  }
  template<std::same_as<std::string> S>
  size_t set_value(header_name_and_enum key, S&& value,
      nv_flags flags = nv_flags::none) {
    size_t ndx = find_next(key, 0);
    // NOLINTBEGIN(bugprone-move-forwarding-reference)
    if (ndx == npos) return add(key, std::move(value), flags);
    auto& field = fields_[ndx];
    field.value = std::move(value);
    // NOLINTEND(bugprone-move-forwarding-reference)
    field.flags |= flags;
    return ndx;
  }

  // Find the first field with the given name or token, or `nullptr` if none.
  auto find(this auto& self, header_name_and_enum key) noexcept
      -> decltype(self.fields_.data()) {
    const auto ndx = self.find_next(key, 0);
    return ndx != npos ? &self.fields_[ndx] : nullptr;
  }

  // Find the unique field with the given name or token, or `nullptr` if none
  // or more than one.
  auto find_unique(this auto& self, header_name_and_enum key) noexcept
      -> decltype(self.fields_.data()) {
    const auto ndx = self.find_next(key, 0);
    if (ndx == npos) return nullptr;
    if (self.find_next(key, ndx + 1) != npos) return nullptr;
    return &self.fields_[ndx];
  }

  // Count the number of fields with the given name or token.
  [[nodiscard]] size_t count(header_name_and_enum key) const noexcept {
    size_t count{0};
    for (size_t ndx = find_next(key, 0); ndx != npos;
        ndx = find_next(key, ndx + 1))
      ++count;
    return count;
  }

  // Find index of the next field with the given name or token, starting with
  // `start`, or `npos` if none.
  [[nodiscard]] size_t
  find_next(header_name_and_enum key, size_t start) const noexcept {
    if (auto token = key.as_enum(); token != qpack_token::unknown) {
      for (size_t ndx = start; ndx < fields_.size(); ++ndx)
        if (fields_[ndx].token == token) return ndx;
    } else {
      const auto name = std::string_view{key};
      for (size_t ndx = start; ndx < fields_.size(); ++ndx)
        if (fields_[ndx].name == name) return ndx;
    }
    return npos;
  }

  // Access the field at `ndx`. Returns a const reference on a const
  // instance.
  [[nodiscard]] auto& operator[](this auto& self, size_t ndx) noexcept {
    return self.fields_[ndx];
  }

  // Iterators over the fields, in insertion order. Yield const fields on a
  // const instance.
  [[nodiscard]] auto begin(this auto& self) noexcept {
    return self.fields_.begin();
  }
  [[nodiscard]] auto end(this auto& self) noexcept {
    return self.fields_.end();
  }

  // Number of fields in this collection.
  [[nodiscard]] size_t size() const noexcept { return fields_.size(); }

  // Whether this collection has no fields.
  [[nodiscard]] bool empty() const noexcept { return fields_.empty(); }

  // Erase the field at `ndx`. Returns false if `ndx` is out of range.
  bool erase(size_t ndx) noexcept {
    if (ndx >= fields_.size()) return false;
    fields_.erase(fields_.begin() + static_cast<std::ptrdiff_t>(ndx));
    return true;
  }

  // Remove all fields.
  bool clear() {
    fields_.clear();
    fields_.reserve(32);
    chunk_fin_ = stream_chunk::more;
    return true;
  }

  // Reserve space for at least `capacity` fields.
  void reserve(size_t capacity) { fields_.reserve(capacity); }

  // Convert to span for submission.
  [[nodiscard]] operator std::span<const http3_field>() const noexcept {
    return {fields_.data(), fields_.size()};
  }

#pragma endregion
#pragma region Data members
private:
  std::vector<http3_field> fields_;
  stream_chunk chunk_fin_{stream_chunk::more};

#pragma endregion
};

#pragma endregion
}}} // namespace corvid::proto::quic
