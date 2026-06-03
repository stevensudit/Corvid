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

#pragma region http3_client_stream

// A client-side HTTP/3 stream, usable two ways.
//
// Without inheritance: construct it with a completion callback, fill in the
// request line via `configure_request` (and/or by editing `request_headers`
// directly), optionally append body chunks to `send_queue`, and hand it to a
// plain `http3_router::add_stream`.
//
// With inheritance: override `on_send_data_ready` to generate the body on the
// fly, `on_recv_data` to consume the response body differently, or any other
// `http3_stream` hook. A body produced lazily, with nothing appended to
// `send_queue` before the stream is added, must also set
// `send_queue().state().body_production = production_policy::streaming` so the
// request is submitted with a body and nghttp3 pulls `on_send_data_ready`.
//
// Responses land in `response_headers` / `response_trailers` (and `:status`
// within them). The callback fires once, from `on_close`, with `completed`
// and `app_error_code` already set, so it can tell a clean finish from an
// error.
//
// The default `on_recv_data` appends a copy of each received buffer to
// `receive_queue`, accumulating the response body as a gather without
// coalescing it; the callback reads it from there.
class http3_client_stream: public http3_stream {
public:
  using completion_callback = std::function<void(http3_client_stream&)>;

  explicit http3_client_stream(completion_callback&& on_complete)
      : on_complete_{std::move(on_complete)} {
    // No router yet, so we can't assert that we're on the loop thread.
    assert(on_complete_);
    set_role(connection_role::client);
  }

  // Fill the request line (method, authority, path) on `headers`, leaving any
  // other fields untouched. A free-standing helper so a caller can configure
  // the headers however it likes; the pseudo-headers are placeholders the
  // constructor already added, so this just sets their values.
  static bool configure_request(http3_headers& headers, method_name method,
      std::string_view path) {
    if (path.empty()) return headers.clear() && false;

    headers.set_value(":method", method);
    headers.set_value(":path", path);
    return true;
  }

  // Send request once added.
  [[nodiscard]] bool on_added() override {
    assert(router()->is_loop_thread());
    auto& headers = request_headers();

    // Default the authority to the server name. Does not overwrite.
    if (auto* a = headers.find(":authority"); a && a->value.empty())
      a->value = router()->server_name();

    return router()->submit_request(this, headers, has_body());
  }

  // Invoke callback once the response is available.
  [[nodiscard]] bool on_close(h3_error_code app_error_code) override {
    assert(router()->is_loop_thread());
    const bool ok = http3_stream::on_close(app_error_code);
    on_complete_(*this);
    return ok;
  }

private:
  completion_callback on_complete_;
};

#pragma endregion

}}} // namespace corvid::proto::quic
