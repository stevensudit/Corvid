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

#include <cstdint>
#include <format>
#include <iterator>
#include <string>
#include <string_view>

#include "../corvid/meta/formatting.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

// These tests exercise the formatter bases in `corvid/meta/formatting.h`
// directly, with minimal local wrapper types, rather than relying on the
// library types that happen to derive from them.

#pragma region Local wrapper types

namespace {

// A wrapper convertible to `const int&`, for `forwarding_formatter`.
struct fbox {
  int v = 0;
  operator const int&() const noexcept { return v; }
};

// A pointer-like wrapper for `nullable_formatter`. It is contextually
// convertible to bool and dereferences to the element. The two null modes ride
// along as template parameters so one wrapper covers every combination.
template<typename T, null_formatting Plain = null_formatting::sentinel,
    null_formatting Debug = Plain>
struct nbox {
  const T* ptr = nullptr;
  explicit operator bool() const noexcept { return ptr != nullptr; }
  const T& operator*() const noexcept { return *ptr; }
};

// A nullable wrapper with a custom sentinel, set through the marker
// constructor the docs describe.
struct closing_handle {
  const int* ptr = nullptr;
  explicit operator bool() const noexcept { return ptr != nullptr; }
  const int& operator*() const noexcept { return *ptr; }
};

// A `self_rendering_formatter` target that renders through a `format_to`
// member, with a distinct `debug_format_to` for the `?` spec. The base applies
// width and precision to the rendered text via a buffer.
struct greeter {
  std::string_view who;
  template<typename OutIt>
  OutIt format_to(OutIt out) const {
    return std::format_to(out, "hi {}", who);
  }
  template<typename OutIt>
  OutIt debug_format_to(OutIt out) const {
    return std::format_to(out, "<{}>", who);
  }
};

// A `self_rendering_formatter` target that applies the spec itself through a
// `format_to_spec` member (the path `os_file` uses).
struct dashes {
  template<CharType CharT, typename OutIt>
  OutIt format_to_spec(const parsed_spec<CharT>& spec, OutIt out) const {
    std::string_view content = "----";
    if (const auto prec = spec.precision) content = content.substr(0, *prec);
    return spec.write_padded(out, content, spec.width);
  }
};

} // namespace

template<CharType CharT>
struct std::formatter<fbox, CharT>: corvid::forwarding_formatter<int, CharT> {
};

template<typename T, corvid::null_formatting Plain,
    corvid::null_formatting Debug, corvid::CharType CharT>
struct std::formatter<nbox<T, Plain, Debug>, CharT>
    : corvid::nullable_formatter<T, CharT, Plain, Debug> {};

template<corvid::CharType CharT>
struct std::formatter<closing_handle, CharT>
    : corvid::nullable_formatter<int, CharT> {
  constexpr formatter() : corvid::nullable_formatter<int, CharT>{"closed"} {}
};

template<corvid::CharType CharT>
struct std::formatter<greeter, CharT>
    : corvid::self_rendering_formatter<CharT> {};

template<corvid::CharType CharT>
struct std::formatter<dashes, CharT>: corvid::self_rendering_formatter<CharT> {
};

#pragma endregion
#pragma region Compile-time checks

namespace {

using cps = parsed_spec<char>;
using align = cps::aligned;

// `calc_padding` is constexpr; verify the split for each alignment, the
// odd-pad bias toward the trailing side, and the no-room case.
static_assert(cps::calc_padding(align::left, 3, 10).first == 0);
static_assert(cps::calc_padding(align::left, 3, 10).second == 7);
static_assert(cps::calc_padding(align::right, 3, 10).first == 7);
static_assert(cps::calc_padding(align::right, 3, 10).second == 0);
static_assert(cps::calc_padding(align::center, 3, 10).first == 3);
static_assert(cps::calc_padding(align::center, 3, 10).second == 4);
static_assert(cps::calc_padding(align::center, 4, 10).first == 3);
static_assert(cps::calc_padding(align::center, 4, 10).second == 3);
static_assert(cps::calc_padding(align::right, 5, 3).first == 0);
static_assert(cps::calc_padding(align::right, 5, 3).second == 0);

using sp = spec_parser<char>;
using avt = sp::arg_value_t;

// `make_from_parse` classifies a width/precision argument; it is constexpr.
constexpr avt parse_arg(std::string_view s) {
  std::size_t ndx = 0;
  return avt::make_from_parse(s, ndx);
}
static_assert(parse_arg("10").kind == sp::arg_kind::fixed);
static_assert(parse_arg("10").value == 10);
static_assert(parse_arg("{}").is_automatic());
static_assert(parse_arg("{7}").kind == sp::arg_kind::manual);
static_assert(parse_arg("{7}").value == 7);
static_assert(parse_arg("xyz").kind == sp::arg_kind::none);

// `parse` is constexpr; spot-check a fully loaded spec.
constexpr sp parse_spec(std::string_view s) {
  sp p;
  (void)p.parse(s);
  return p;
}
static_assert(parse_spec("*^10.3Lf}").fill == '*');
static_assert(parse_spec("*^10.3Lf}").width == 10);
static_assert(parse_spec("*^10.3Lf}").precision == 3);
static_assert(parse_spec("*^10.3Lf}").has_locale);
static_assert(parse_spec("*^10.3Lf}").type == 'f');

} // namespace

#pragma endregion
#pragma region parsed_spec

TEST_CASE("WritePrimitives", "[parsed_spec]") {
  std::string s;
  cps::write_repeat(std::back_inserter(s), '*', 3);
  CHECK(s == "***");

  s.clear();
  cps::write_repeat(std::back_inserter(s), '-', 0);
  CHECK(s.empty());

  s.clear();
  cps::write_sv(std::back_inserter(s), "abc"sv);
  CHECK(s == "abc");
}

TEST_CASE("WritePadded", "[parsed_spec]") {
  auto render = [](align a, char fill, std::size_t w, std::string_view c) {
    cps spec;
    spec.alignment = a;
    spec.fill = fill;
    std::string out;
    (void)spec.write_padded(std::back_inserter(out), c, w);
    return out;
  };
  CHECK(render(align::left, ' ', 6, "ab") == "ab    ");
  CHECK(render(align::right, ' ', 6, "ab") == "    ab");
  CHECK(render(align::center, ' ', 6, "ab") == "  ab  ");
  // Odd padding biases toward the trailing side.
  CHECK(render(align::center, '*', 7, "ab") == "**ab***");
  // Width narrower than content adds nothing.
  CHECK(render(align::left, ' ', 1, "abc") == "abc");

  // The one-argument overload uses `spec.width`.
  cps spec;
  spec.alignment = align::right;
  spec.width = 5;
  std::string out;
  (void)spec.write_padded(std::back_inserter(out), "xy");
  CHECK(out == "   xy");
}

TEST_CASE("WidePadded", "[parsed_spec]") {
  // The narrow content is widened to the spec's code unit.
  parsed_spec<wchar_t> spec;
  spec.alignment = parsed_spec<wchar_t>::aligned::right;
  std::wstring out;
  (void)spec.write_padded(std::back_inserter(out), "ab", 5);
  CHECK(out == L"   ab");
}

#pragma endregion
#pragma region spec_parser

TEST_CASE("ArgValue", "[spec_parser]") {
  // The member `parse` advances the index past the consumed text.
  avt a;
  CHECK(a.parse("10x", 0) == 2);
  CHECK(a.kind == sp::arg_kind::fixed);
  CHECK(a.value == 10);
  CHECK(a.get_fixed() == 10);
  CHECK_FALSE(a.is_dynamic());

  avt b;
  CHECK(b.parse("{}", 0) == 2);
  CHECK(b.is_automatic());
  CHECK(b.is_dynamic());
  CHECK(b.get_automatic() == 0);
  CHECK(b.get_fixed() == std::nullopt);

  avt c;
  CHECK(c.parse("{12}", 0) == 4);
  CHECK(c.kind == sp::arg_kind::manual);
  CHECK(c.value == 12);
  CHECK(c.is_dynamic());
  CHECK_FALSE(c.is_automatic());

  avt d;
  CHECK(d.parse("z", 0) == 0);
  CHECK(d.kind == sp::arg_kind::none);
  CHECK_FALSE(d.is_dynamic());
}

TEST_CASE("ParseSpec", "[spec_parser]") {
  {
    sp p;
    CHECK(p.parse("") == 0);
    CHECK(p.width == 0);
    CHECK(p.alignment == sp::aligned::left);
    CHECK(p.fill == ' ');
    CHECK(p.sign == '-');
    CHECK_FALSE(p.alternate);
    CHECK_FALSE(p.zero_pad);
    CHECK_FALSE(p.has_locale);
    CHECK(p.type == '\0');
    CHECK_FALSE(p.debug);
  }
  {
    // `parse` stops at and returns the offset of the closing brace.
    sp p;
    CHECK(p.parse("5}") == 1);
    CHECK(p.width == 5);
  }
  {
    sp p;
    CHECK(p.parse("d}") == 1);
    CHECK(p.type == 'd');
  }
  {
    sp p;
    p.parse(">8}");
    CHECK(p.alignment == sp::aligned::right);
    CHECK(p.width == 8);
  }
  {
    sp p;
    p.parse("*^6}");
    CHECK(p.fill == '*');
    CHECK(p.alignment == sp::aligned::center);
    CHECK(p.width == 6);
  }
  {
    sp p;
    p.parse(" 5}");
    CHECK(p.sign == ' ');
    CHECK(p.width == 5);
  }
  {
    sp p;
    p.parse("#}");
    CHECK(p.alternate);
  }
  {
    sp p;
    p.parse("06}");
    CHECK(p.zero_pad);
    CHECK(p.width == 6);
  }
  {
    sp p;
    p.parse(".3f}");
    CHECK(p.precision == 3);
    CHECK(p.type == 'f');
  }
  {
    sp p;
    p.parse("L}");
    CHECK(p.has_locale);
  }
  {
    sp p;
    p.parse("?}");
    CHECK(p.type == '?');
    CHECK(p.debug);
  }
  {
    sp p;
    CHECK(p.parse("*^+#010.3Lf}") == 11);
    CHECK(p.fill == '*');
    CHECK(p.alignment == sp::aligned::center);
    CHECK(p.sign == '+');
    CHECK(p.alternate);
    CHECK(p.zero_pad);
    CHECK(p.width == 10);
    CHECK(p.precision == 3);
    CHECK(p.has_locale);
    CHECK(p.type == 'f');
  }
}

TEST_CASE("IsDynamic", "[spec_parser]") {
  {
    sp p;
    p.parse("5.2}");
    CHECK_FALSE(p.is_dynamic());
  }
  {
    sp p;
    p.parse("{}}");
    CHECK(p.is_dynamic());
    CHECK(p.width_arg.is_automatic());
  }
  {
    sp p;
    p.parse(".{}}");
    CHECK(p.is_dynamic());
    CHECK(p.precision_arg.is_automatic());
  }
  {
    sp p;
    p.parse("{3}}");
    CHECK(p.is_dynamic());
    CHECK(p.width_arg.kind == sp::arg_kind::manual);
    CHECK(p.width_arg.value == 3);
  }
}

TEST_CASE("RewriteExplicit", "[spec_parser]") {
  // Each automatic `{}` becomes a manual `{n}` with its claimed id, so the
  // base reads the same argument. (The claim is simulated here by setting the
  // value the parse context would have supplied.)
  {
    sp p;
    p.parse("{}.{}}");
    p.width_arg.value = 2;
    p.precision_arg.value = 5;
    CHECK(p.rewrite_spec_as_explicit("{}.{}") == "{2}.{5}");
  }
  {
    sp p;
    p.parse(".{}}");
    p.precision_arg.value = 7;
    CHECK(p.rewrite_spec_as_explicit(".{}") == ".{7}");
  }
}

#pragma endregion
#pragma region forwarding_formatter

TEST_CASE("Forwarding", "[forwarding_formatter]") {
  // The wrapper reuses the full int spec grammar.
  CHECK(std::format("{}", fbox{42}) == "42");
  CHECK(std::format("{:5}", fbox{7}) == "    7");
  CHECK(std::format("{:#x}", fbox{255}) == "0xff");
  CHECK(std::format("{:+}", fbox{5}) == "+5");
  CHECK(std::format("{:{}}", fbox{7}, 4) == "   7");
}

#pragma endregion
#pragma region nullable_formatter

TEST_CASE("Present", "[nullable_formatter]") {
  int n = 42;
  int big = 255;
  CHECK(std::format("{}", nbox<int>{&n}) == "42");
  CHECK(std::format("{:5}", nbox<int>{&n}) == "   42");
  CHECK(std::format("{:#x}", nbox<int>{&big}) == "0xff");
  CHECK(std::format("{:{}}", nbox<int>{&n}, 4) == "  42");

  // String elements forward the debug spec's quoting too.
  std::string_view sv = "hi";
  CHECK(std::format("{}", nbox<std::string_view>{&sv}) == "hi");
  CHECK(std::format("{:>5}", nbox<std::string_view>{&sv}) == "   hi");
  CHECK(std::format("{:?}", nbox<std::string_view>{&sv}) == "\"hi\"");
}

TEST_CASE("Sentinel", "[nullable_formatter]") {
  CHECK(std::format("{}", nbox<int>{}) == "(null)");
  CHECK(std::format("{:8}", nbox<int>{}) == "(null)  ");
  CHECK(std::format("{:>8}", nbox<int>{}) == "  (null)");
  CHECK(std::format("{:^8}", nbox<int>{}) == " (null) ");
  CHECK(std::format("{:*^10}", nbox<int>{}) == "**(null)**");
}

TEST_CASE("DynamicWidthSentinel", "[nullable_formatter]") {
  // Automatic `{}`: the id is claimed and the spec rewritten for the base.
  CHECK(std::format("{:{}}", nbox<int>{}, 8) == "(null)  ");
  // Manual `{n}`: read directly.
  CHECK(std::format("{0:{1}}", nbox<int>{}, 8) == "(null)  ");
  CHECK(std::format("{0:>{1}}", nbox<int>{}, 8) == "  (null)");
  // A present value still resolves dynamic width through the base.
  int n = 42;
  CHECK(std::format("{:{}}", nbox<int>{&n}, 6) == "    42");
}

TEST_CASE("CustomMarker", "[nullable_formatter]") {
  CHECK(std::format("{}", closing_handle{}) == "closed");
  CHECK(std::format("{:8}", closing_handle{}) == "closed  ");
  CHECK(std::format("{:>8}", closing_handle{}) == "  closed");
  int n = 7;
  CHECK(std::format("{}", closing_handle{&n}) == "7");
}

TEST_CASE("Empty", "[nullable_formatter]") {
  using empty_text = nbox<std::string_view, null_formatting::empty>;
  CHECK(std::format("{}", empty_text{}) == "");
  // An empty field still honors width, via the inherited formatter.
  CHECK(std::format("{:4}", empty_text{}) == "    ");
  std::string_view sv = "hi";
  CHECK(std::format("{}", empty_text{&sv}) == "hi");
}

TEST_CASE("DebugVsPlain", "[nullable_formatter]") {
  // Plain spec shows empty; debug spec shows the sentinel.
  using mixed = nbox<std::string_view, null_formatting::empty,
      null_formatting::sentinel>;
  CHECK(std::format("{}", mixed{}) == "");
  CHECK(std::format("{:?}", mixed{}) == "(null)");

  // The reverse selection. Empty mode formats a default-constructed underlying
  // value through the base, so an empty string under the debug spec renders
  // quoted.
  using mixed2 = nbox<std::string_view, null_formatting::sentinel,
      null_formatting::empty>;
  CHECK(std::format("{}", mixed2{}) == "(null)");
  CHECK(std::format("{:?}", mixed2{}) == "\"\"");
}

#pragma endregion
#pragma region self_rendering_formatter

TEST_CASE("SelfRendering", "[self_rendering_formatter]") {
  // No spec: the rendered text passes through directly.
  CHECK(std::format("{}", greeter{"bob"}) == "hi bob");
  // Width and alignment pad the rendered text (via a buffer).
  CHECK(std::format("{:10}", greeter{"bob"}) == "hi bob    ");
  CHECK(std::format("{:>10}", greeter{"bob"}) == "    hi bob");
  CHECK(std::format("{:^10}", greeter{"bob"}) == "  hi bob  ");
  // Precision truncates the rendered text.
  CHECK(std::format("{:.2}", greeter{"bob"}) == "hi");
  // Dynamic width is resolved before padding.
  CHECK(std::format("{:{}}", greeter{"x"}, 5) == "hi x ");
  // The `?` spec routes to debug_format_to.
  CHECK(std::format("{:?}", greeter{"bob"}) == "<bob>");
}

TEST_CASE("FormatToSpec", "[self_rendering_formatter]") {
  CHECK(std::format("{}", dashes{}) == "----");
  CHECK(std::format("{:6}", dashes{}) == "----  ");
  CHECK(std::format("{:>6}", dashes{}) == "  ----");
  CHECK(std::format("{:^8}", dashes{}) == "  ----  ");
  // Precision truncates the self-rendered text.
  CHECK(std::format("{:.2}", dashes{}) == "--");
  CHECK(std::format("{:6.2}", dashes{}) == "--    ");
  // Dynamic width arrives already resolved.
  CHECK(std::format("{:{}}", dashes{}, 6) == "----  ");
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
