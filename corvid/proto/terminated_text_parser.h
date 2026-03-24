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
#include <cstddef>
#include <optional>
#include <string_view>

namespace corvid { inline namespace proto {

// Parser for sentinel-terminated text frames, designed for line-oriented
// protocols such as HTTP, SMTP, and POP3.
//
// The parser is a lightweight, non-owning view over a `state` object that
// holds all mutable and configuration fields. The `state` is intended to live
// in the connection so that it persists across successive `on_data` calls,
// while the parser itself may be constructed on the stack per call.
//
// `parse` returns `std::optional<bool>`: `true` = complete,
// `false` = too long, `std::nullopt` = incomplete.
//
// Typical integration pattern with `recv_buffer_view` in `on_data`:
//
//   // In the connection (constructed once):
//   terminated_text_parser::state parser_state{"\r\n", 8192};
//
//   // In on_data (constructed per call):
//   terminated_text_parser parser{parser_state};
//   while (true) {
//     auto sv = view.active_view();
//     std::string_view text;
//     if (const auto r = parser.parse(sv, text); r == true) {
//       handle(text, conn);            // process before updating view
//       view.update_active_view(sv);   // sv already advanced by `parse`
//       parser.reset();
//     } else if (r == false) {
//       conn.close();
//       return true;
//     } else {
//       // incomplete (nullopt)
//       if (sv.size() == view.buffer_capacity())
//         view.expand_to(sv.size() * 2);
//       return true;
//     }
//   }
//
// `bytes_scanned` in `state` tracks how far the current view was examined,
// enabling efficient resume across successive `on_data` calls without
// re-scanning from the start. The sentinel is stored as a `std::string_view`;
// the pointed-to bytes must outlive the `state`.
class terminated_text_parser {
public:
  // Persistent state for `terminated_text_parser`. Intended to live in the
  // connection so that parse progress is preserved across `on_data` calls.
  //
  // `sentinel`: byte sequence marking the end of each frame (e.g., "\r\n",
  //   ":", "\r\n\r\n"). The pointed-to bytes must outlive the `state`.
  // `max_length`: maximum bytes that may be scanned before `false` is
  //   returned. 0 means no limit is enforced here; the caller is responsible
  //   for the full-buffer case (compare `bytes_scanned()` against
  //   `view.buffer_capacity()`).
  struct state {
    friend class terminated_text_parser;

    explicit state(std::string_view sentinel = "\r\n",
        size_t max_length = 8192) noexcept
        : sentinel_{sentinel}, max_length_{max_length} {}

    [[nodiscard]] const auto& sentinel() const noexcept { return sentinel_; }

    [[nodiscard]] operator bool() const noexcept { return !sentinel_.empty(); }
    [[nodiscard]] bool operator!() const noexcept { return sentinel_.empty(); }

  private:
    std::string_view sentinel_;
    size_t max_length_{};
    size_t bytes_scanned_{};
  };

  // Construct a parser over `s`. The `state` must outlive the parser.
  explicit terminated_text_parser(state& s) noexcept : state_{s} {}

  // Using the associated state, try to parse one sentinel-terminated frame
  // from `input`.
  //
  // Complete:
  //    - Returns `true`, and sets `text_out` to the frame text (a view into
  //      the original `input`, without the sentinel), while advancing `input`
  //      past the full frame and its sentinel. Note that `text_out` could be
  //      empty if there's nothing before the sentinel.
  //    - The `text_out` view will remain valid until the `recv_buffer_view`
  //      destructs, so the caller should act on it in place and return, or
  //      move the `recv_buffer_view` into a worker thread, or possibly copy
  //      it.
  //    - The caller should pass `input` to `view.update_active_view(input)` so
  //      that it can be consumed, then call `reset()` before the next frame.
  //
  // Incomplete:
  //   - Returns `std::nullopt`, and doesn't modify `text_out` or `input`.
  //     Updates `bytes_scanned` so the next call can resume efficiently.
  //     The caller should wait for more data to arrive and call `parse` again
  //     with the same `state` and the new `input` (which will necessarily
  //     contain the same unconsumed bytes as before, plus the new ones).
  //   - If `input.size() == view.buffer_capacity()`, the buffer is full
  //     with no terminator found; call `view.expand_to(...)` or close.
  //
  // Invalid:
  //   - Returns `false`, and doesn't modify `text_out` or `input`. When
  //     the sentinel isn't empty and max_length isn't 0, this indicates that
  //     the input exceeded max_length without finding a full frame. When the
  //     sentinel is empty, this indicates that the state was never properly
  //     initialized. In either case, the parser cannot continue and the
  //     caller should close the connection.
  [[nodiscard]] std::optional<bool>
  parse(std::string_view& input, std::string_view& text_out) {
    // An empty sentinel is invalid.
    const size_t slen{state_.sentinel_.size()};
    if (slen == 0) return false;

    // How many bytes to back up from the current scan position to catch a
    // split sentinel.
    const size_t backup{slen - 1};
    // Adjusted scan position for this call.
    const size_t resume{
        state_.bytes_scanned_ > backup ? state_.bytes_scanned_ - backup : 0};

    // Search for sentinel.
    const auto pos = input.find(state_.sentinel_, resume);
    if (pos == std::string_view::npos) {
      // Track how far we got.
      state_.bytes_scanned_ = input.size();

      // Fail if too long.
      if (state_.max_length_ > 0 && state_.bytes_scanned_ > state_.max_length_)
        return false;

      return std::nullopt;
    }
    // Extract frame.
    text_out = input.substr(0, pos);
    input.remove_prefix(pos + slen);
    return true;
  }

  // Reset state for the next frame. Call after `parse` returns `true`
  // and the frame has been processed.
  void reset() noexcept { state_.bytes_scanned_ = 0; }

  // Bytes from the start of the current frame view already examined.
  [[nodiscard]] size_t bytes_scanned() const noexcept {
    return state_.bytes_scanned_;
  }

private:
  state& state_;
};

}} // namespace corvid::proto
