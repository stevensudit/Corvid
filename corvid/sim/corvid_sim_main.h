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

#include "../proto.h"
#include "ws_handler.h"

using namespace corvid::proto;

int main(int argc, char** argv) {
  // If invoked with "-testonly" (e.g., by the CI build script), skip the
  // server entirely and exit cleanly. This lets the test runner include
  // `corvid_sim` in its sweep without blocking on a live server.
  for (int i = 1; i < argc; ++i) {
    if (std::string_view{argv[i]} == "-testonly") return 0;
  }

  // Default web root: walk up from the executable until a `corvid/sim/web`
  // subdirectory is found. This works regardless of build output location.
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
      auto candidate = dir / "corvid/sim/web";
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

  // Block SIGINT/SIGTERM on all threads so `sigwait` can intercept them.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &mask, nullptr);

  // Create server (owns its own loop and timing wheel). The cache shared_ptr
  // is captured by the factory lambda and passed into each transaction, which
  // holds its own reference to keep the cache alive.
  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 8080},
      [cache](http_server& s) {
        if (!s.add_route({"", "/"},
                [cache](request_head&& req) -> transaction_ptr {
                  return std::make_shared<static_file_transaction>(
                      std::move(req), cache);
                }))
          return false;
        return s.add_route({"", "/ws"}, make_ws_factory(s.loop(), s.wheel()));
      });

  if (!server) {
    std::cerr << "Failed to create HTTP server\n";
    return 1;
  }

  std::cout << "Listening on http://" << server->local_endpoint() << "/\n";

  int sig{};
  sigwait(&mask, &sig);
  std::cout << "\nShutting down\n";
  return 0;
}
