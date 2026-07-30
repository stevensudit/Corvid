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
#include <ostream>
#include <streambuf>

#include "exception_firewalls.h"

namespace corvid { inline namespace infra {

// Stream redirection
//
// `ostream_redirector` aims one `std::ostream` at another one's buffer for the
// duration of a scope, then restores the original. The usual reason is to
// capture what would have gone to `std::cout` or `std::cerr`, either to assert
// on it in a test or to divert it while something noisy runs.
//
//    std::stringstream ss;
//    if (true) {
//      ostream_redirector capture(std::cout, ss);
//      std::cout << "captured";
//    }
//    // `std::cout` is itself again, and `ss.str()` is "captured".
//
// The redirection is strictly scoped: the object is neither copyable nor
// movable, so the restore happens exactly once, in the scope that established
// it.

#pragma region ostream_redirector

// Redirect a `std::ostream`, `from`, to a different one, `to`, during its
// lifespan.
class ostream_redirector final {
public:
#pragma region Construction

  explicit ostream_redirector(std::ostream& from, std::ostream& to)
      : from_{&from}, rdbuf_{from.rdbuf()} {
    from.rdbuf(to.rdbuf());
  }

  ostream_redirector(const ostream_redirector&) = delete;
  ostream_redirector& operator=(const ostream_redirector&) = delete;
  ostream_redirector(ostream_redirector&&) = delete;
  ostream_redirector& operator=(ostream_redirector&&) = delete;

  ~ostream_redirector() noexcept {
    try_or_terminate([&] { from_->rdbuf(rdbuf_); });
  }

#pragma endregion
#pragma region Data members

private:
  std::ostream* from_{};
  std::streambuf* rdbuf_{};

#pragma endregion
};

#pragma endregion

}} // namespace corvid::infra
