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
// HTTP transaction for serving pre-cached static files.
//
// `epoll_static_file_cache` loads and owns the in-memory file cache.
// `epoll_static_file_transaction` is an `epoll_http_transaction` subclass that
// serves entries from an `epoll_static_file_cache`.
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "../../containers/core/opt_find.h"
#include "../../containers/core/transparent.h"
#include "../../strings/token_parser.h"
#include "../misc/http_head_codec.h"
#include "epoll_http_transaction.h"

namespace corvid { inline namespace proto {

#pragma region epoll_static_file_cache
// `epoll_static_file_cache` is an in-memory cache of static files, keyed by
// URL path.
//
// Walks a directory tree at construction time, reads every regular file into
// memory, and serves them by URL path. Also adds "/" as an alias for
// "/index.html" when that file is present.
//
// Content types are derived from file extensions at load time, so
// directory-style URLs (e.g., "/") inherit the correct type from the file they
// alias rather than from the URL itself.
class epoll_static_file_cache {
#pragma region Types
public:
  // A single cached file: its pre-read body and `Content-Type` string.
  struct entry {
    std::string body;
    std::string content_type;
  };
#pragma endregion
#pragma region Construction

  // Load all regular files under `web_root`.
  //
  // A filesystem error on an entry silently skips that entry; an error
  // advancing the walk itself ends the load early; an unreadable `web_root`
  // produces an empty cache.
  explicit epoll_static_file_cache(const std::filesystem::path& web_root) {
    // The `error_code` overloads are used throughout because the throwing ones
    // would abandon the load on the first failure.
    std::error_code walk_ec;
    const std::filesystem::recursive_directory_iterator end;
    for (std::filesystem::recursive_directory_iterator it{web_root, walk_ec};
        !walk_ec && it != end; it.increment(walk_ec))
    {
      const auto& e = *it;
      std::error_code entry_ec;
      if (!e.is_regular_file(entry_ec)) continue;
      const auto rel = e.path().lexically_relative(web_root);
      const auto url = "/" + rel.generic_string();
      auto body = read_file(e.path());
      if (!body) continue;
      map_[url] = {std::move(*body), std::string{content_type_for(url)}};
    }
    if (auto index = find_opt(map_, "/index.html")) map_["/"] = *index;
  }

  epoll_static_file_cache(epoll_static_file_cache&&) noexcept = default;
  epoll_static_file_cache(const epoll_static_file_cache&) = delete;

  epoll_static_file_cache& operator=(const epoll_static_file_cache&) = delete;
  epoll_static_file_cache& operator=(
      epoll_static_file_cache&&) noexcept = default;
#pragma endregion
#pragma region Accessors

  // Number of cached entries (including aliases such as "/").
  [[nodiscard]] size_t size() const noexcept { return map_.size(); }

  // Look up `url`. Returns a pointer into the cache (valid for the lifetime
  // of this object), or null if not found.
  [[nodiscard]] const entry* find(std::string_view url) const {
    return find_opt(map_, url);
  }
#pragma endregion
#pragma region Helpers
private:
  // Return the `Content-Type` string for a URL or filename by extension.
  // TODO: This should be a lookup table.
  [[nodiscard]] static std::string_view content_type_for(
      std::string_view name) noexcept {
    if (name.ends_with(".html")) return "text/html";
    if (name.ends_with(".js")) return "text/javascript";
    if (name.ends_with(".css")) return "text/css";
    return "application/octet-stream";
  }

  // Read `path` into a string. Returns empty optional on failure.
  [[nodiscard]] static std::optional<std::string> read_file(
      const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    return std::string{std::istreambuf_iterator<char>{f}, {}};
  }
#pragma endregion
#pragma region Data members
  string_map<entry> map_;
#pragma endregion
};
#pragma endregion

#pragma region epoll_static_file_transaction
// `epoll_static_file_transaction` serves pre-cached static files from an
// `epoll_static_file_cache`.
//
// Holds a `shared_ptr` to the cache so the transaction keeps the cache alive
// independently of the factory closure. Only GET and HEAD are supported;
// other methods return 405 with an `Allow` header. Request targets not found
// in the cache return 404.
struct epoll_static_file_transaction: public epoll_http_transaction {
#pragma region Construction
  epoll_static_file_transaction(request_head&& req,
      std::shared_ptr<const epoll_static_file_cache> cache)
      : epoll_http_transaction{std::move(req)}, cache_{std::move(cache)} {}
#pragma endregion
#pragma region Overrides

  [[nodiscard]] stream_claim handle_drain(const send_fn& send_cb) override {
    const auto& req = request_headers;

    // RFC 9110 section 9.1 makes GET and HEAD the mandatory methods, and
    // this transaction supports nothing else. Section 15.5.6 requires a 405
    // to list the supported methods in `Allow`.
    if (req.method != http_method::GET && req.method != http_method::HEAD) {
      close_after = after_response::close;
      (void)send_cb(response_head::make_error_response(close_after,
          req.version, http_status_code::METHOD_NOT_ALLOWED,
          "Method Not Allowed", {"Allow", "GET, HEAD"}));
      return stream_claim::release;
    }

    // Strip query string; only the path portion is used for lookup.
    std::string_view path{req.target};
    path = strings::token_parser::next_delimited('?', path);

    const auto* e = cache_->find(path);
    if (!e) {
      (void)send_cb(response_head::make_error_response(close_after,
          req.version, http_status_code::NOT_FOUND, "Not Found"));
      return stream_claim::release;
    }

    response_headers.version = req.version;
    response_headers.status_code = http_status_code::OK;
    response_headers.reason = "OK";
    response_headers.options.content_length = e->body.size();
    response_headers.options.connection = close_after;
    (void)response_headers.headers.reset_raw("Content-Type", e->content_type);

    (void)send_cb(response_headers.serialize());
    // RFC 9110 section 9.3.2: HEAD is GET without the response content.
    if (req.method != http_method::HEAD) (void)send_cb(std::string{e->body});
    return stream_claim::release;
  }
#pragma endregion
#pragma region Data members
private:
  std::shared_ptr<const epoll_static_file_cache> cache_;
#pragma endregion
};
#pragma endregion

}} // namespace corvid::proto
