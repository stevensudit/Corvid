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
#include <string_view>

#include "../../strings/cases.h"
#include "../../strings/token_parser.h"

// Helpers for the authority component shared across HTTP versions: the `Host`
// field value and the HTTP/2+ `:authority` pseudo-header (RFC 9110 section
// 7.2: `Host = uri-host [ ":" port ]`).
//
// `strip_host_port("example.com:8080")` yields `"example.com"`, so hosts can
// be compared without regard to the port.

namespace corvid { inline namespace proto { inline namespace http_proto {

#pragma region strip_host_port

// Strip the `":" port` suffix, if any, from a `Host` or `:authority` value,
// returning the `uri-host`.
//
// A bracketed IPv6 literal keeps its brackets, and a port can begin only
// after its closing bracket. Only a genuine port, which is a colon followed by
// one or more digits, is stripped. A value with anything else in port position
// (an empty port, a non-numeric suffix, an unbracketed IPv6 literal, an
// unclosed bracket) is returned unchanged, not repaired.
//
// The functions can throw, but they won't.
// NOLINTBEGIN(bugprone-exception-escape)
[[nodiscard]] inline std::string_view strip_host_port(
    std::string_view host) noexcept {
  const auto is_port = [](std::string_view port) {
    return !port.empty() && std::ranges::all_of(port, strings::is_digit);
  };
  auto text = host;
  if (host.starts_with('[')) {
    const auto v6 = strings::token_parser::next_terminated(']', text);
    if (!v6 || !text.starts_with(':')) return host;
    text.remove_prefix(1);
    if (!is_port(text)) return host;
    return host.substr(0, v6->size() + 1);
  }
  const auto name = strings::token_parser::next_delimited(':', text);
  if (name.size() == host.size()) return host;
  if (!is_port(text)) return host;
  return name;
}
// NOLINTEND(bugprone-exception-escape)

#pragma endregion

}}} // namespace corvid::proto::http_proto
