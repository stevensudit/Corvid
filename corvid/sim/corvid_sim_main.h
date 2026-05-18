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
// CorvidSim main entry point: static-file HTTP server for the sim web assets.
//
// Included exactly once from `tests/corvid_sim.cpp`. All implementation is
// here so the translation unit stays trivial.
#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>
#include <pthread.h>

#include "../proto.h"
#include "sim_ws_handler.h"

// Blocks SIGINT and SIGTERM on all threads (including those spawned after
// construction) so that `sigwait` can intercept them cleanly. Must be
// constructed before any threads are created.
class signal_waiter {
public:
  signal_waiter() {
    sigemptyset(&mask_);
    sigaddset(&mask_, SIGINT);
    sigaddset(&mask_, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &mask_, nullptr);
  }

  // Block until one of the registered signals is delivered. Returns the
  // signal number.
  [[nodiscard]] int wait() const {
    int sig{};
    sigwait(&mask_, &sig);
    return sig;
  }

private:
  sigset_t mask_{};
};

using namespace corvid::proto;
using namespace corvid::concurrency;

int do_main(int argc, char** argv) {
  // Default web root: walk up from the executable until a
  // `corvid/sim/web/dist` subdirectory is found. This works regardless of
  // build output location.
  std::filesystem::path web_root;
  if (argc > 1) {
    web_root = argv[1];
  } else {
    std::error_code exe_ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", exe_ec);
    if (exe_ec) {
      std::cerr << "Cannot resolve executable path\n";
      return 1;
    }
    for (auto dir = exe.parent_path(); dir != dir.parent_path();
        dir = dir.parent_path())
    {
      auto candidate = dir / "corvid/sim/web/dist";
      std::error_code dir_ec;
      if (std::filesystem::is_directory(candidate, dir_ec)) {
        web_root = std::move(candidate);
        break;
      }
    }
  }

  std::error_code ec;
  if (!std::filesystem::is_directory(web_root, ec)) {
    std::cerr << "Web root not found: " << web_root << "\n";
    return 1;
  }

  auto cache = std::make_shared<const static_file_cache>(web_root);
  std::cout << "Loaded " << cache->size() << " file(s) from " << web_root
            << "\n";

  signal_waiter signals;

  epoll_loop_runner loop;
  // Run the server and sim on a 50ms wheel so the world loop can achieve its
  // intended 20 Hz cadence. Use 1200 slots so the wheel still spans nearly
  // 60 seconds, which keeps the HTTP server's default 30s request timeout
  // within range.
  timing_wheel_runner wheel{1200, 50ms};

  http_server::duration_t request_timeout = 30s;
  http_server::duration_t write_timeout = 5s;

  // Create server using the same explicit loop and 50ms timing wheel that the
  // sim WebSocket route will use. The cache shared_ptr is captured by the
  // factory lambda and passed into each transaction, which holds its own
  // reference to keep the cache alive.
  auto server = http_server::create(
      net_endpoint{ipv4_addr::loopback, 8080},
      [cache](http_server& s) {
        if (!s.add_route({"", "/"},
                [cache](request_head&& req) -> transaction_ptr {
                  return std::make_shared<static_file_transaction>(
                      std::move(req), cache);
                }))
          return false;
        return s.add_route({"", "/ws"},
            SimWsHandler::make_factory(s.loop(), s.wheel()));
      },
      loop.loop()->self(), wheel.wheel(), request_timeout, write_timeout);

  if (!server) {
    std::cerr << "Failed to create HTTP server\n";
    return 1;
  }

  std::cout << "Listening on http://" << server->local_endpoint() << "/\n";

  (void)signals.wait();
  std::cout << "\nShutting down\n";
  return 0;
}

int main(int argc, char** argv) {
  try {
    return do_main(argc, argv);
  }
  catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << "\n";
    return 1;
  }
}
