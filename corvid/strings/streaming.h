// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
//
// Licensed under the Apache License, Version 2.0(the "License");
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
#include "strings_shared.h"
#include "delimiting.h"

namespace corvid::strings { inline namespace streaming {

//
// Streaming
//

// Lightweight streaming wrappers to avoid having to constantly type
// `std::cout <<` and `<< std::endl`, while providing a small amount of
// additional utility.

constexpr auto&
stream_out(OStreamDerived auto& os, const OStreamable auto&... args) {
  return (os << ... << args);
}

constexpr auto& stream_out_with(OStreamDerived auto& os, delim d,
    const OStreamable auto& head, const OStreamable auto&... tail) {
  os << head;
  return ((os << d << (tail)), ...);
}

constexpr auto& print(const OStreamable auto&... parts) {
  return stream_out(std::cout, parts...);
}

constexpr auto& print_with(delim d, const OStreamable auto&... parts) {
  return stream_out_with(std::cout, d, parts...);
}

constexpr auto& println(const OStreamable auto&... parts) {
  return print(parts...) << '\n';
}

constexpr auto& println_with(delim d, const OStreamable auto&... parts) {
  return print_with(d, parts...) << '\n';
}

constexpr auto& report(const OStreamable auto&... parts) {
  return stream_out(std::cerr, parts...) << std::endl;
}

constexpr auto& report_if(bool emit, const OStreamable auto&... parts) {
  if (emit) report(parts...);
  return std::cerr;
}

constexpr auto& report_with(delim d, const OStreamable auto&... parts) {
  return stream_out_with(std::cerr, d, parts...) << std::endl;
}

// Redirect a `std::ostream`, `from`, to a different one, `to`, during its
// lifespan.
class ostream_redirector {
public:
  explicit ostream_redirector(std::ostream& from, std::ostream& to)
      : from_{&from}, rdbuf_{from.rdbuf()} {
    from.rdbuf(to.rdbuf());
  }

  ~ostream_redirector() { from_->rdbuf(rdbuf_); }

private:
  std::ostream* from_;
  std::streambuf* rdbuf_;
};

}} // namespace corvid::strings::streaming
