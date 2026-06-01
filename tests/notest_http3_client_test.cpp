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

// A minimal HTTP/3 GET client, essentially a hardwired `curl --http3`: it
// resolves a host, runs the QUIC + HTTP/3 handshake against the live server,
// issues `GET <path>`, and prints the response status, headers, trailers, and
// body size. Prefixed `notest_` so it is built but never run by the sweep
// (which cannot depend on connectivity or a third-party endpoint). Run it
// manually:
//
//   ./notest_http3_client_test [host] [path]   (defaults: cloudflare-quic.com
//   /)

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "../corvid/proto/dns_resolver.h"
#include "../corvid/proto/io_uring/iou_loop.h"
#include "../corvid/proto/quic/http3_plugins.h"
#include "../corvid/proto/quic/http3_request_stream.h"
#include "../corvid/proto/quic/quic_conn.h"
#include "../corvid/proto/quic/quic_dgram_plugins.h"
#include "../corvid/proto/quic/quic_ssl_ctx.h"

using namespace corvid;
using namespace corvid::iouring;
using namespace corvid::proto;
using namespace corvid::proto::quic;
using namespace std::chrono_literals;

namespace {

// RFC 9114 ALPN for HTTP/3.
constexpr std::string_view h3_alpn = "h3";

bool WaitFor(const auto& pred, std::chrono::milliseconds timeout = 10s) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(5ms);
  return pred();
}

// Loop-thread-owned capture of one response. The caller holds a shared_ptr to
// it; the `request_stream` completion callback fills it (on the loop thread,
// in `on_close`), so the result outlives the stream. main reads it via
// post_and_wait once `complete` is set.
struct response_capture {
  bool complete{false};
  bool failed{false};
  std::string status;
  std::vector<std::pair<std::string, std::string>> headers;
  std::vector<std::pair<std::string, std::string>> trailers;
  size_t body_bytes{0};
};

using protocol_t = quic_dgram_protocol<http3_router>;

// Copy the finished stream's response into `out`. Runs on the loop thread from
// the stream's completion callback (in `on_close`), so `out` outlives it.
void capture_response(request_stream& s, response_capture& out) {
  out.failed = s.app_error_code() != h3_error_code::no_error || !s.completed();
  for (const auto& f : s.response_headers())
    out.headers.emplace_back(f.name, f.value);
  if (const auto* f = s.response_headers().find(qpack_token::status))
    out.status = f->value;
  for (const auto& f : s.response_trailers())
    out.trailers.emplace_back(f.name, f.value);
  out.body_bytes = s.receive_queue().appended();
  out.complete = true; // set last: main waits on this
}

// Build a configured request stream: request line, an optional body (chunked
// small so a sizeable body spans more than the 16 vec slots, exercising the
// multi-segment gather and the veccnt truncation path), and the completion
// callback.
std::unique_ptr<request_stream> make_request(http_method method,
    std::string_view path, std::span<const uint8_t> body,
    request_stream::completion_callback on_complete) {
  auto stream = std::make_unique<request_stream>(std::move(on_complete));
  request_stream::configure_request(stream->request_headers(), method, path);
  if (!body.empty()) {
    stream->request_headers().set_value("content-type", "text/plain");
    stream->request_headers().set_value("content-length",
        std::to_string(body.size()));
    constexpr size_t piece = 8;
    for (size_t off = 0; off < body.size(); off += piece) {
      const size_t len = std::min(piece, body.size() - off);
      stream->send_queue().append(std::vector<uint8_t>(
          body.begin() + static_cast<std::ptrdiff_t>(off),
          body.begin() + static_cast<std::ptrdiff_t>(off + len)));
    }
  }
  return stream;
}

} // namespace

int main(int argc, char** argv) {
  const std::string host = argc > 1 ? argv[1] : "cloudflare-quic.com";
  const std::string path = argc > 2 ? argv[2] : "/";
  // A third arg is a request body, which turns the GET into a POST.
  const std::string body_str = argc > 3 ? argv[3] : "";
  const std::vector<uint8_t> body(body_str.begin(), body_str.end());
  const http_method method =
      body.empty() ? http_method::GET : http_method::POST;
  std::cout << strings::enum_as_string(method) << " https://" << host << path
            << " over HTTP/3";
  if (!body.empty()) std::cout << " (" << body.size() << "-byte body)";
  std::cout << "\n";

  const auto peer = dns_resolver::find_one(host, 443, AF_INET);
  if (peer.empty()) {
    std::cerr << "error: could not resolve " << host << "\n";
    return 1;
  }
  std::cout << "resolved " << host << " -> " << peer << "\n";

  quic_ssl_ctx client_tls{h3_alpn};
  if (!client_tls) {
    std::cerr << "error: TLS context init failed\n";
    return 1;
  }

  iou_loop_runner runner;

  auto client_router =
      iou_dgram_router_handle<protocol_t::router_plugin>::bind(*runner.loop(),
          net_endpoint::any_v4(), shot_type::multi, client_tls);
  if (!client_router) {
    std::cerr << "error: could not bind client router\n";
    return 1;
  }

  std::shared_ptr<protocol_t::session_plugin::session_t> sess;
  if (!runner.loop()->post_and_wait([&]() -> bool {
        sess = protocol_t::session_plugin::make_client(
            *client_router.pointer(), peer, host);
        return sess != nullptr;
      }))
  {
    std::cerr << "error: could not create client session\n";
    return 1;
  }

  auto& client = sess->plugin().protocol_plugin();

  // Wait for the handshake and the server's SETTINGS: that proves 1-RTT is up
  // and the QPACK / control streams are bound, so a request can go out.
  if (!WaitFor([&] {
        return runner.loop()->post_and_wait([&]() -> bool {
          return client.has_peer_settings();
        });
      }))
  {
    std::cerr << "error: handshake / SETTINGS exchange timed out\n";
    return 1;
  }
  std::cout << "handshake complete, server SETTINGS received\n";

  auto out = std::make_shared<response_capture>();
  auto on_complete = [out](request_stream& s) { capture_response(s, *out); };
  if (!runner.loop()->post_and_wait([&]() -> bool {
        return client.add_stream(
            make_request(method, path, body, on_complete));
      }))
  {
    std::cerr << "error: could not submit request\n";
    return 1;
  }

  if (!WaitFor([&] {
        return runner.loop()->post_and_wait([&]() -> bool {
          return out->complete;
        });
      }))
  {
    std::cerr << "error: response timed out\n";
    return 1;
  }

  // Print the captured response from the loop thread.
  int rc = 0;
  if (!runner.loop()->post_and_wait([&]() -> bool {
        std::cout << "\nHTTP/3 " << out->status << "\n";
        for (const auto& [name, value] : out->headers)
          std::cout << name << ": " << value << "\n";
        for (const auto& [name, value] : out->trailers)
          std::cout << "(trailer) " << name << ": " << value << "\n";
        std::cout << "\n[" << out->body_bytes << " body bytes]\n";
        rc = (!out->failed && !out->status.empty() &&
                 out->status.starts_with("2"))
                 ? 0
                 : 1;
        return true;
      }))
  {
    std::cerr << "error: could not read response\n";
    return 1;
  }

  return rc;
}
