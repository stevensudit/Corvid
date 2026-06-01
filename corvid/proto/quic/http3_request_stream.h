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
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "http3_plugins.h"

namespace corvid { inline namespace proto { namespace quic {

#pragma region request_stream

// A client request stream, usable two ways.
//
// Without inheritance: construct it with a completion callback, fill in the
// request line via `set_request` (or by editing `request_headers()` directly,
// so any extra fields are yours to add), optionally append body chunks to
// `send_queue()`, and hand it to a plain `http3_router::add_stream`.
//
// With inheritance: override `on_send_data_ready` to stream or generate the
// body on the fly, `on_recv_data` to consume the response body differently, or
// any other `http3_stream` hook.
//
// Responses land in `response_headers()` / `response_trailers()` (and
// `:status` within them). The callback fires once, from `on_close`, with
// `completed()` and `app_error_code()` already set, so it can tell a clean
// finish from an error.
//
// The default `on_recv_data` appends a copy of each received buffer to
// `receive_queue()`, accumulating the response body as a gather without
// coalescing it; the callback reads it from there.
class request_stream: public http3_stream {
public:
  using completion_callback = std::function<void(request_stream&)>;

  explicit request_stream(completion_callback&& on_complete)
      : on_complete_{std::move(on_complete)} {
    assert(on_complete_);
    set_role(connection_role::client);
  }

  // Fill the request line (method, authority, path) on `headers`, leaving any
  // other fields untouched. A free-standing helper so a caller can configure
  // the headers however it likes; the pseudo-headers are placeholders the
  // constructor already added, so this just sets their values.
  static bool configure_request(http3_headers& headers, method_name method,
      std::string_view path, std::string_view authority) {
    if (path.empty()) return headers.clear() && false;

    headers.set_value(":method", method);
    headers.set_value(":authority", authority);
    headers.set_value(":path", path);
    return true;
  }

  [[nodiscard]] bool on_added() override {
    return router()->submit_request(this, request_headers(),
        send_queue().appended());
  }

  [[nodiscard]] bool on_close(h3_error_code app_error_code) override {
    const bool ok = http3_stream::on_close(app_error_code);
    on_complete_(*this);
    return ok;
  }

private:
  completion_callback on_complete_;
};

#pragma endregion

}}} // namespace corvid::proto::quic
