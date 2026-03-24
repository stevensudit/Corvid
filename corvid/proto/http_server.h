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
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "epoll_loop.h"
#include "stream_conn.h"
#include "terminated_text_parser.h"

namespace corvid { inline namespace proto {

// Minimal HTTP 0.9 server, based on
// https://datatracker.ietf.org/doc/html/rfc1945, where it covers
// Simple-Request and Simple-Response.
//
// Listens for TCP connections, parses each request line with
// `terminated_text_parser`, and sends a canned HTML response for any `GET
// /path` request, and waits for the next.
//
// Construct via the `create` factory, which returns a
// `std::shared_ptr<http_server>`. Each accepted connection is owned by the
// loop and carries its own `terminated_text_parser::state` so that partial
// request lines arriving across multiple `on_data` calls are handled
// correctly.
//
// If the `loop` argument to `create` is null, the server starts its own
// `epoll_loop_runner` (background thread) and owns it for its lifetime.
// Otherwise the caller supplies a shared loop and the server borrows it.
class http_server: public std::enable_shared_from_this<http_server> {
  using conn_t =
      stream_conn_with_state<std::optional<terminated_text_parser::state>>;

public:
  // Create an HTTP 0.9 server listening on `endpoint`.
  //
  // If `loop` is non-null, the server shares it; otherwise it constructs and
  // owns an `epoll_loop_runner`. Returns null if the listen socket cannot be
  // created.
  [[nodiscard]] static std::shared_ptr<http_server>
  create(std::shared_ptr<epoll_loop> loop, const net_endpoint& endpoint) {
    auto self = std::shared_ptr<http_server>(new http_server{});

    // Get an epoll loop.
    if (loop) {
      self->loop_ = std::move(loop);
    } else {
      self->runner_.emplace();
      self->loop_ = self->runner_->loop();
    }

    // Start listening.
    self->listener_ = stream_conn_ptr_with<conn_t>::listen(*self->loop_,
        endpoint,
        {.on_data = [self](stream_conn& conn, recv_buffer_view view) {
          return handle_data(conn, std::move(view));
        }});

    return self->listener_ ? self : nullptr;
  }

  // Return the actual bound address (useful when `endpoint` used port 0).
  [[nodiscard]] net_endpoint local_endpoint() const {
    return listener_->local_endpoint();
  }

private:
  http_server() = default;

  // Handle incoming data for an accepted connection. Parses a single
  // `GET /path` line and sends back a canned HTML response, then closes.
  [[nodiscard]] static bool
  handle_data(stream_conn& conn, recv_buffer_view view) {
    auto& state = conn_t::from(conn).state();
    if (!state) state = terminated_text_parser::state{"\r\n", 8192};
    terminated_text_parser parser{*state};

    auto input = view.active_view();
    std::string_view simple_request;
    const auto r = parser.parse(input, simple_request);

    // If incomplete, wait for more.
    if (!r) return true;

    // If invalid, close the connection. Essentially, we need to find a "\r\n"
    // sentinel before reaching the 8k line-length limit.
    if (!*r) return conn.close() && false;

    // The syntax is trivial.
    if (!simple_request.starts_with("GET /")) return conn.close() && false;

    const std::string_view path{simple_request.substr(4)};
    // TODO: Validate the path to prevent directory traversal.

    // "Look up" the file.
    std::string html{"<html><body><p>I am definitely, totally the file \""};
    // TODO: This needs to be HTML-escaped, for security and such.
    html += path;
    html += "\".</p></body></html>\r\n";

    // Send the response.
    if (!conn.send(std::move(html))) return conn.close() && false;

    // Normally, we would call `view.update_active_view(input);` and
    // `parser.reset();`, but HTTP 0.9 has no way to pipeline because it has no
    // headers, therefore all we can do is gracefully disconnect, trusting the
    // send queue to take care of flushing the response on the way out.
    (void)conn.close();
    return true;
  }

  std::optional<epoll_loop_runner> runner_;
  std::shared_ptr<epoll_loop> loop_;
  stream_conn_ptr_with<conn_t> listener_;
};
}} // namespace corvid::proto
