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
#include <flat_map>
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
// the `http3_field_view` submit type. Split out of `http3_conn.h` so callers
// that only build or inspect header fields need not pull in the whole
// `nghttp3_conn` wrapper.

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

#pragma endregion
#pragma region http3_field_view

// An HTTP field (name / value pair plus QPACK flags) to submit in a request or
// response HEADERS frame: the Corvid-facing form of `nghttp3_nv`. `name` and
// `value` are borrowed for the duration of the submit call only; nghttp3
// copies them unless a `no_copy_*` flag says otherwise, so the default (`flags
// == nv_flags::none`) lets the caller pass ephemeral views. Pseudo-headers use
// their ':' name (":method", ":path", ":status", and so on).
struct http3_field_view {
  std::string_view name;
  std::string_view value;
  nv_flags flags{nv_flags::none};
};

#pragma endregion
#pragma region stream_chunk

// Whether a chunk of stream data is the last the sender will put on this
// stream (carries the QUIC stream FIN) or whether more may follow.
enum class stream_chunk : uint8_t {
  more = 0,
  fin = 1,
};

#pragma endregion
#pragma region http3_field

// An HTTP field with owned storage.
struct http3_field {
  qpack_token token{qpack_token::unknown};
  std::string name;
  std::string value;
  nv_flags flags{nv_flags::none};

  [[nodiscard]] auto as_view() const noexcept {
    return http3_field_view{name, value, flags};
  }

  operator http3_field_view() const noexcept { return as_view(); }
};

}}} // namespace corvid::proto::quic

template<>
constexpr inline auto
    corvid::enums::registry::enum_spec_v<corvid::proto::quic::qpack_token> =
        corvid::enums::sequence::make_sequence_enum_spec<
            corvid::proto::quic::qpack_token, "">();

template<>
constexpr inline auto
    corvid::enums::registry::enum_spec_v<corvid::proto::quic::nv_flags> =
        corvid::enums::bitmask::make_bitmask_enum_spec<
            corvid::proto::quic::nv_flags,
            "try_index, no_copy_value, no_copy_name, never_index">();

template<>
constexpr inline auto
    corvid::enums::registry::enum_spec_v<corvid::proto::quic::stream_chunk> =
        corvid::enums::sequence::make_sequence_enum_spec<
            corvid::proto::quic::stream_chunk, "more, fin">();

namespace corvid { inline namespace proto { namespace quic {
#pragma region http3_headers

// A collection of HTTP fields, with support for the QPACK token lookups.
class http3_headers {
public:
  static constexpr size_t npos = static_cast<size_t>(-1);

#pragma region Construction

#pragma endregion
#pragma region Accessors

  // Add a field with token. Returns its index (valid until the next mutation).
  size_t add(const http3_field_view& field, qpack_token token) {
    fields_.emplace_back(token, std::string{field.name},
        std::string{field.value}, field.flags);
    return fields_.size() - 1;
  }

  // Add a field, looking up its token from the name. Returns its index (valid
  // until the next mutation).
  size_t add(const http3_field_view& field) {
    return add(field, token_from_name(field.name));
  }

  // Set the field `value` (and `flags`): modify the first existing field with
  // that name if one is present, otherwise add a new one. If `token` is
  // specified, uses that for search; otherwise, uses `name`. Returns index.
  size_t set_value(std::string_view name, std::string_view value,
      qpack_token token = qpack_token::unknown,
      nv_flags flags = nv_flags::none) {
    assert(!name.empty());
    size_t ndx;
    if (token != qpack_token::unknown)
      ndx = find_next(token, 0);
    else
      ndx = find_next(name, 0);
    if (ndx != npos) {
      auto& field = fields_[ndx];
      field.value = std::string{value};
      field.flags |= flags;
    } else {
      if (token == qpack_token::unknown) token = token_from_name(name);
      ndx = add({name, value, flags}, token);
    }
    return ndx;
  }

  // Find the first field with the given name or token, or `nullptr` if none.
  auto find(this auto& self, const auto& key) noexcept
      -> decltype(self.fields_.data()) {
    const auto ndx = self.find_next(key, 0);
    return ndx != npos ? &self.fields_[ndx] : nullptr;
  }

  // Find the unique field with the given name or token, or `nullptr` if none
  // or more than one.
  auto find_unique(this auto& self, const auto& key) noexcept
      -> decltype(self.fields_.data()) {
    const auto ndx = self.find_next(key, 0);
    if (ndx == npos) return nullptr;
    if (self.find_next(key, ndx + 1) != npos) return nullptr;
    return &self.fields_[ndx];
  }

  // Count the number of fields with the given name or token.
  [[nodiscard]] size_t count(const auto& key) const noexcept {
    size_t count{0};
    for (size_t ndx = find_next(key, 0); ndx != npos;
        ndx = find_next(key, ndx + 1))
      ++count;
    return count;
  }

  // Find index of the next field with the given name or token, starting with
  // `start`, or `npos` if none.
  [[nodiscard]] size_t
  find_next(const auto& key, size_t start) const noexcept {
    for (size_t ndx = start; ndx < fields_.size(); ++ndx) {
      if constexpr (std::is_same_v<std::remove_cvref_t<decltype(key)>,
                        qpack_token>)
      {
        if (fields_[ndx].token == key) return ndx;
      } else {
        if (fields_[ndx].name == key) return ndx;
      }
    }
    return npos;
  }

  // Access the field at `ndx`. Returns a const reference on a const instance.
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
  void clear() noexcept { fields_.clear(); }

  // Convert to span for submission.
  [[nodiscard]] operator std::span<const http3_field>() const noexcept {
    return {fields_.data(), fields_.size()};
  }

#pragma endregion
#pragma region Field names

  static constexpr auto authority = ":authority"sv;
  static constexpr auto method = ":method"sv;
  static constexpr auto path = ":path"sv;
  static constexpr auto scheme = ":scheme"sv;
  static constexpr auto status = ":status"sv;
  static constexpr auto accept = "accept"sv;
  static constexpr auto accept_encoding = "accept-encoding"sv;
  static constexpr auto accept_language = "accept-language"sv;
  static constexpr auto accept_ranges = "accept-ranges"sv;
  static constexpr auto access_control_allow_credentials =
      "access-control-allow-credentials"sv;
  static constexpr auto access_control_allow_headers =
      "access-control-allow-headers"sv;
  static constexpr auto access_control_allow_methods =
      "access-control-allow-methods"sv;
  static constexpr auto access_control_allow_origin =
      "access-control-allow-origin"sv;
  static constexpr auto access_control_expose_headers =
      "access-control-expose-headers"sv;
  static constexpr auto access_control_request_headers =
      "access-control-request-headers"sv;
  static constexpr auto access_control_request_method =
      "access-control-request-method"sv;
  static constexpr auto age = "age"sv;
  static constexpr auto alt_svc = "alt-svc"sv;
  static constexpr auto authorization = "authorization"sv;
  static constexpr auto cache_control = "cache-control"sv;
  static constexpr auto content_disposition = "content-disposition"sv;
  static constexpr auto content_encoding = "content-encoding"sv;
  static constexpr auto content_length = "content-length"sv;
  static constexpr auto content_security_policy = "content-security-policy"sv;
  static constexpr auto content_type = "content-type"sv;
  static constexpr auto cookie = "cookie"sv;
  static constexpr auto date = "date"sv;
  static constexpr auto early_data = "early-data"sv;
  static constexpr auto etag = "etag"sv;
  static constexpr auto expect_ct = "expect-ct"sv;
  static constexpr auto forwarded = "forwarded"sv;
  static constexpr auto if_modified_since = "if-modified-since"sv;
  static constexpr auto if_none_match = "if-none-match"sv;
  static constexpr auto if_range = "if-range"sv;
  static constexpr auto last_modified = "last-modified"sv;
  static constexpr auto link = "link"sv;
  static constexpr auto location = "location"sv;
  static constexpr auto origin = "origin"sv;
  static constexpr auto purpose = "purpose"sv;
  static constexpr auto range = "range"sv;
  static constexpr auto referer = "referer"sv;
  static constexpr auto server = "server"sv;
  static constexpr auto set_cookie = "set-cookie"sv;
  static constexpr auto strict_transport_security =
      "strict-transport-security"sv;
  static constexpr auto timing_allow_origin = "timing-allow-origin"sv;
  static constexpr auto upgrade_insecure_requests =
      "upgrade-insecure-requests"sv;
  static constexpr auto user_agent = "user-agent"sv;
  static constexpr auto vary = "vary"sv;
  static constexpr auto x_content_type_options = "x-content-type-options"sv;
  static constexpr auto x_forwarded_for = "x-forwarded-for"sv;
  static constexpr auto x_frame_options = "x-frame-options"sv;
  static constexpr auto x_xss_protection = "x-xss-protection"sv;
  static constexpr auto host = "host"sv;
  static constexpr auto connection = "connection"sv;
  static constexpr auto keep_alive = "keep-alive"sv;
  static constexpr auto proxy_connection = "proxy-connection"sv;
  static constexpr auto transfer_encoding = "transfer-encoding"sv;
  static constexpr auto upgrade = "upgrade"sv;
  static constexpr auto te = "te"sv;
  static constexpr auto protocol = ":protocol"sv;
  static constexpr auto priority = "priority"sv;

#pragma endregion
#pragma region Lookups

  // Maps a `qpack_token` to its canonical field-name string, or `{}` if the
  // token is `unknown`
  [[nodiscard]] static std::string_view name_from_token(
      qpack_token token) noexcept {
    static const std::flat_map<qpack_token, std::string_view> names{
        {qpack_token::authority, authority},
        {qpack_token::method, method},
        {qpack_token::path, path},
        {qpack_token::scheme, scheme},
        {qpack_token::status, status},
        {qpack_token::accept, accept},
        {qpack_token::accept_encoding, accept_encoding},
        {qpack_token::accept_language, accept_language},
        {qpack_token::accept_ranges, accept_ranges},
        {qpack_token::access_control_allow_credentials,
            access_control_allow_credentials},
        {qpack_token::access_control_allow_headers,
            access_control_allow_headers},
        {qpack_token::access_control_allow_methods,
            access_control_allow_methods},
        {qpack_token::access_control_allow_origin,
            access_control_allow_origin},
        {qpack_token::access_control_expose_headers,
            access_control_expose_headers},
        {qpack_token::access_control_request_headers,
            access_control_request_headers},
        {qpack_token::access_control_request_method,
            access_control_request_method},
        {qpack_token::age, age},
        {qpack_token::alt_svc, alt_svc},
        {qpack_token::authorization, authorization},
        {qpack_token::cache_control, cache_control},
        {qpack_token::content_disposition, content_disposition},
        {qpack_token::content_encoding, content_encoding},
        {qpack_token::content_length, content_length},
        {qpack_token::content_security_policy, content_security_policy},
        {qpack_token::content_type, content_type},
        {qpack_token::cookie, cookie},
        {qpack_token::date, date},
        {qpack_token::early_data, early_data},
        {qpack_token::etag, etag},
        {qpack_token::expect_ct, expect_ct},
        {qpack_token::forwarded, forwarded},
        {qpack_token::if_modified_since, if_modified_since},
        {qpack_token::if_none_match, if_none_match},
        {qpack_token::if_range, if_range},
        {qpack_token::last_modified, last_modified},
        {qpack_token::link, link},
        {qpack_token::location, location},
        {qpack_token::origin, origin},
        {qpack_token::purpose, purpose},
        {qpack_token::range, range},
        {qpack_token::referer, referer},
        {qpack_token::server, server},
        {qpack_token::set_cookie, set_cookie},
        {qpack_token::strict_transport_security, strict_transport_security},
        {qpack_token::timing_allow_origin, timing_allow_origin},
        {qpack_token::upgrade_insecure_requests, upgrade_insecure_requests},
        {qpack_token::user_agent, user_agent},
        {qpack_token::vary, vary},
        {qpack_token::x_content_type_options, x_content_type_options},
        {qpack_token::x_forwarded_for, x_forwarded_for},
        {qpack_token::x_frame_options, x_frame_options},
        {qpack_token::x_xss_protection, x_xss_protection},
        {qpack_token::host, host},
        {qpack_token::connection, connection},
        {qpack_token::keep_alive, keep_alive},
        {qpack_token::proxy_connection, proxy_connection},
        {qpack_token::transfer_encoding, transfer_encoding},
        {qpack_token::upgrade, upgrade},
        {qpack_token::te, te},
        {qpack_token::protocol, protocol},
        {qpack_token::priority, priority},
    };
    if (auto found = find_opt(names, token)) return *found;
    return {};
  }

  // Maps a field name to its `qpack_token`, or `qpack_token::unknown`.
  [[nodiscard]] static qpack_token token_from_name(
      std::string_view name) noexcept {
    static const std::unordered_map<std::string_view, qpack_token> tokens{
        {authority, qpack_token::authority},
        {method, qpack_token::method},
        {path, qpack_token::path},
        {scheme, qpack_token::scheme},
        {status, qpack_token::status},
        {accept, qpack_token::accept},
        {accept_encoding, qpack_token::accept_encoding},
        {accept_language, qpack_token::accept_language},
        {accept_ranges, qpack_token::accept_ranges},
        {access_control_allow_credentials,
            qpack_token::access_control_allow_credentials},
        {access_control_allow_headers,
            qpack_token::access_control_allow_headers},
        {access_control_allow_methods,
            qpack_token::access_control_allow_methods},
        {access_control_allow_origin,
            qpack_token::access_control_allow_origin},
        {access_control_expose_headers,
            qpack_token::access_control_expose_headers},
        {access_control_request_headers,
            qpack_token::access_control_request_headers},
        {access_control_request_method,
            qpack_token::access_control_request_method},
        {age, qpack_token::age},
        {alt_svc, qpack_token::alt_svc},
        {authorization, qpack_token::authorization},
        {cache_control, qpack_token::cache_control},
        {content_disposition, qpack_token::content_disposition},
        {content_encoding, qpack_token::content_encoding},
        {content_length, qpack_token::content_length},
        {content_security_policy, qpack_token::content_security_policy},
        {content_type, qpack_token::content_type},
        {cookie, qpack_token::cookie},
        {date, qpack_token::date},
        {early_data, qpack_token::early_data},
        {etag, qpack_token::etag},
        {expect_ct, qpack_token::expect_ct},
        {forwarded, qpack_token::forwarded},
        {if_modified_since, qpack_token::if_modified_since},
        {if_none_match, qpack_token::if_none_match},
        {if_range, qpack_token::if_range},
        {last_modified, qpack_token::last_modified},
        {link, qpack_token::link},
        {location, qpack_token::location},
        {origin, qpack_token::origin},
        {purpose, qpack_token::purpose},
        {range, qpack_token::range},
        {referer, qpack_token::referer},
        {server, qpack_token::server},
        {set_cookie, qpack_token::set_cookie},
        {strict_transport_security, qpack_token::strict_transport_security},
        {timing_allow_origin, qpack_token::timing_allow_origin},
        {upgrade_insecure_requests, qpack_token::upgrade_insecure_requests},
        {user_agent, qpack_token::user_agent},
        {vary, qpack_token::vary},
        {x_content_type_options, qpack_token::x_content_type_options},
        {x_forwarded_for, qpack_token::x_forwarded_for},
        {x_frame_options, qpack_token::x_frame_options},
        {x_xss_protection, qpack_token::x_xss_protection},
        {host, qpack_token::host},
        {connection, qpack_token::connection},
        {keep_alive, qpack_token::keep_alive},
        {proxy_connection, qpack_token::proxy_connection},
        {transfer_encoding, qpack_token::transfer_encoding},
        {upgrade, qpack_token::upgrade},
        {te, qpack_token::te},
        {protocol, qpack_token::protocol},
        {priority, qpack_token::priority},
    };
    if (auto found = find_opt(tokens, name)) return *found;
    return qpack_token::unknown;
  }
#pragma endregion
#pragma region Data members
private:
  std::vector<http3_field> fields_;

#pragma endregion
};

#pragma endregion
}}} // namespace corvid::proto::quic
