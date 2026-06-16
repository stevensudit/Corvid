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
#include <type_traits>

#include "strings_shared.h"

namespace corvid::strings { inline namespace targeting {

//
// Appender target
//

// An appender target is a thin wrapper over a target stream or string. As its
// name suggests, it's used in the various append functions to support either
// type of target seamlessly.
//
// Note: Under clang and gcc, this is optimized away entirely. Under MSVC, not
// quite. But this is consistent with MSVC's overall pattern of underwhelming
// optimization.

#pragma region appender

// Base template forward declaration. Only specialized for supported targets.
template<typename T>
class appender;

// Append target that forwards output to an output iterator, converting each
// `SrcChar` input unit to the destination unit `DestChar` (which defaults to
// `SrcChar`, a plain passthrough). This lets the append machinery (enum names,
// numbers, delimiters, quoted strings) write straight into a `std::format`
// context's output iterator, with no intermediate string. The conversion is
// per code unit: an identity copy when the units match, and a value-preserving
// widen when `SrcChar` is narrower (e.g. char names into a wide context). It
// does not decode multibyte encodings; real transcoding (such as UTF-8 to
// UTF-16) is out of scope. The `out` iterator is live and is advanced in place
// as appends occur.
template<typename It, CharType SrcChar, CharType DestChar = SrcChar>
struct output_iterator_appendable {
  using append_char_type = SrcChar;
  It out;
};

// Base class with shared functionality, using C++23 deducing this for static
// polymorphism in place of the CRTP idiom. The `this auto&& self` parameter
// deduces the actual derived type, so the base dispatches to derived hooks
// (`append_sv`/`append_ch`) and returns the correct type without a recurring
// template parameter or `static_cast`.
template<typename T, typename C>
class appender_base {
#pragma region Types
public:
  using char_t = C;
  using view_t = std::basic_string_view<char_t>;

#pragma endregion
#pragma region Construction
public:
  constexpr explicit appender_base(T& target) : target_{target} {}

#pragma endregion
#pragma region Appending

  // Deducing this: `self` deduces to the actual derived type (appender<T>).
  // All append overloads forward to append_sv or append_ch in the derived.
  constexpr auto& append(this auto&& self, view_t sv) {
    return self.append_sv(sv);
  }
  constexpr auto& append(this auto&& self, const char_t* ps, size_t len) {
    return self.append(view_t{ps, len});
  }
  constexpr auto& append(this auto&& self, char_t ch) {
    return self.append_ch(1, ch);
  }
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  constexpr auto& append(this auto&& self, size_t len, char_t ch) {
    return self.append_ch(len, ch);
  }

  // Default reserve is no-op; string specialization overrides.
  // Uses deducing this for return type consistency, not polymorphic dispatch.
  constexpr auto& reserve(this auto&& self, size_t) { return self; }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] constexpr T& operator*() { return target_; }
  [[nodiscard]] constexpr T* operator->() { return &target_; }

#pragma endregion
#pragma region Data members
protected:
  T& target_;

#pragma endregion
};

// `std::basic_ostream` specialization.
template<AnyOStreamDerived T>
class appender<T> final: public appender_base<T, typename T::char_type> {
  using char_t = T::char_type;
  using base = appender_base<T, char_t>;
  using base::target_;

#pragma region Construction
public:
  using base::base;

#pragma endregion
#pragma region Appending
private:
  friend base;
  // Not constexpr: `std::basic_ostream` write/put are never constant
  // evaluable, unlike the string and output-iterator specializations.
  auto& append_sv(std::basic_string_view<char_t> sv) {
    target_.write(sv.data(), sv.size());
    return *this;
  }
  auto& append_ch(size_t len, char_t ch) {
    while (len--) target_.put(ch);
    return *this;
  }

#pragma endregion
};

// `std::basic_string` specialization.
template<AnyStdString T>
class appender<T> final: public appender_base<T, typename T::value_type> {
  using char_t = T::value_type;
  using base = appender_base<T, char_t>;
  using base::target_;

#pragma region Construction
public:
  using base::base;

#pragma endregion
#pragma region Appending

  constexpr auto& reserve(size_t len) {
    target_.reserve(target_.size() + len);
    return *this;
  }

private:
  friend base;
  constexpr auto& append_sv(std::basic_string_view<char_t> sv) {
    target_.append(sv);
    return *this;
  }
  constexpr auto& append_ch(size_t len, char_t ch) {
    if (len == 1)
      target_.push_back(ch);
    else
      target_.append(len, ch);
    return *this;
  }

#pragma endregion
};

// `output_iterator_appendable` specialization: `SrcChar` in, `DestChar` out.
template<typename It, CharType SrcChar, CharType DestChar>
class appender<output_iterator_appendable<It, SrcChar, DestChar>> final
    : public appender_base<output_iterator_appendable<It, SrcChar, DestChar>,
          SrcChar> {
  using target_t = output_iterator_appendable<It, SrcChar, DestChar>;
  using base = appender_base<target_t, SrcChar>;
  using base::target_;

#pragma region Construction
public:
  using base::base;

#pragma endregion
#pragma region Appending
private:
  friend base;
  // Per-unit conversion: identity when the units match, otherwise a widen
  // through the unsigned value, so a high byte maps to its code point rather
  // than sign-extending.
  static constexpr DestChar to_dest(SrcChar unit) {
    return static_cast<DestChar>(
        static_cast<std::make_unsigned_t<SrcChar>>(unit));
  }
  constexpr auto& append_sv(std::basic_string_view<SrcChar> sv) {
    for (const SrcChar unit : sv) *target_.out++ = to_dest(unit);
    return *this;
  }
  constexpr auto& append_ch(size_t len, SrcChar unit) {
    const DestChar dest = to_dest(unit);
    while (len--) *target_.out++ = dest;
    return *this;
  }

#pragma endregion
};

// Deduction guide.
template<AnyAppendTarget T>
appender(T) -> appender<T>;

#pragma endregion

}} // namespace corvid::strings::targeting
