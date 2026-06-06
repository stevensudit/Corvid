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
#include <string_view>

#include "targeting.h"
#include "conversion.h"

namespace corvid::strings { inline namespace targeting {

// Append target that writes the C++ "debug" escaped form of its char input
// (per [format.string.escaped], the rules behind the `?` format spec) into an
// output iterator, widening each emitted unit to `DestChar`. It does NOT add
// the surrounding quotes; the caller emits those around it. This lets the
// char-based append machinery stream text straight into a `std::format`
// context with no intermediate string, while staying consistent with how std
// quotes the strings beside it in the same range or map.
//
// The escaping matches the standard over the 7-bit ASCII range that inputs
// occupy: `\t`, `\n`, `\r`, `\"`, `\\`, and `\u{hex}` for other control units
// and DEL. There is no multibyte decoding; bytes at or above 0x80 are emitted
// as `\u{hex}` of the byte value.
template<typename It, CharType DestChar>
struct debug_escaping_appendable {
  using append_char_type = char;
  It out;
};

// `debug_escaping_appendable` specialization: char in, escaped `DestChar` out.
template<typename It, CharType DestChar>
class appender<debug_escaping_appendable<It, DestChar>> final
    : public appender_base<debug_escaping_appendable<It, DestChar>, char> {
  using target_t = debug_escaping_appendable<It, DestChar>;
  using base = appender_base<target_t, char>;
  using self_t = appender<target_t>;
  using base::target_;

#pragma region Construction
public:
  using base::base;

#pragma endregion
#pragma region Appending
private:
  friend base;

  // Widen one already-escaped ASCII unit to DestChar and write it.
  self_t& emit(char c) {
    *target_.out++ = static_cast<DestChar>(static_cast<unsigned char>(c));
    return *this;
  }
  self_t& operator()(char c) { return emit(c); }

  self_t& put_escaped(char c) {
    switch (c) {
    case '"': return emit('\\')('"');
    case '\\': return emit('\\')('\\');
    case '\t': return emit('\\')('t');
    case '\n': return emit('\\')('n');
    case '\r': return emit('\\')('r');
    default: break;
    }
    const auto byte = static_cast<unsigned char>(c);
    if (byte >= 0x20 && byte < 0x7f) return emit(c);
    emit('\\')('u')('{');
    if (byte >= 0x10) emit(as_hex_lc_digit<DestChar>(byte >> 4));
    return emit(as_hex_lc_digit<DestChar>(byte))('}');
  }

  auto& append_sv(std::string_view sv) {
    for (const char c : sv) put_escaped(c);
    return *this;
  }
  auto& append_ch(size_t len, char c) {
    while (len--) put_escaped(c);
    return *this;
  }

#pragma endregion
};

}} // namespace corvid::strings::targeting
