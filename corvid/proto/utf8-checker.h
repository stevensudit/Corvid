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
#include <string_view>

namespace corvid { inline namespace proto {

// Incremental UTF-8 validator.
//
// Feed one chunk after another via `consume`. The validator preserves only the
// carry state needed to span chunk boundaries. Once `failed`, it remains so
// until `reset` is called.
class utf8_checker {
public:
  // Result of incremental UTF-8 validation.
  enum class validation : uint8_t { complete, incomplete, failed };

  [[nodiscard]] validation validate(std::string_view input) noexcept {
    if (state_ == validation::failed) return state_;

    for (unsigned char byte : input) {
      if (remaining_ == 0 ? !consume_lead_byte(byte)
                          : !consume_continuation(byte))
        return state_;
    }

    state_ = remaining_ == 0 ? validation::complete : validation::incomplete;
    return state_;
  }

  void reset() noexcept {
    state_ = validation::complete;
    remaining_ = 0;
    min_cont_ = 0x80;
    max_cont_ = 0xBF;
  }

  [[nodiscard]] validation state() const noexcept { return state_; }
  [[nodiscard]] bool is_complete() const noexcept {
    return state_ == validation::complete;
  }
  [[nodiscard]] bool is_incomplete() const noexcept {
    return state_ == validation::incomplete;
  }
  [[nodiscard]] bool is_failed() const noexcept {
    return state_ == validation::failed;
  }

private:
  [[nodiscard]] bool consume_lead_byte(uint8_t byte) noexcept {
    if (byte <= 0x7F) return true;
    if (byte >= 0xC2 && byte <= 0xDF) {
      remaining_ = 1;
      return true;
    }
    if (byte == 0xE0) {
      remaining_ = 2;
      min_cont_ = 0xA0;
      return true;
    }
    if (byte >= 0xE1 && byte <= 0xEC) {
      remaining_ = 2;
      return true;
    }
    if (byte == 0xED) {
      remaining_ = 2;
      max_cont_ = 0x9F;
      return true;
    }
    if (byte >= 0xEE && byte <= 0xEF) {
      remaining_ = 2;
      return true;
    }
    if (byte == 0xF0) {
      remaining_ = 3;
      min_cont_ = 0x90;
      return true;
    }
    if (byte >= 0xF1 && byte <= 0xF3) {
      remaining_ = 3;
      return true;
    }
    if (byte == 0xF4) {
      remaining_ = 3;
      max_cont_ = 0x8F;
      return true;
    }
    state_ = validation::failed;
    return false;
  }

  [[nodiscard]] bool consume_continuation(uint8_t byte) noexcept {
    if (byte < min_cont_ || byte > max_cont_) {
      state_ = validation::failed;
      return false;
    }
    --remaining_;
    min_cont_ = 0x80;
    max_cont_ = 0xBF;
    return true;
  }

  validation state_{validation::complete};
  uint8_t remaining_{};
  uint8_t min_cont_{0x80};
  uint8_t max_cont_{0xBF};
};

}} // namespace corvid::proto
