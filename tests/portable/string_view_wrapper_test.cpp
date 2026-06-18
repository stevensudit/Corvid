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

#include <format>
#include <map>
#include <vector>

#include "corvid/strings/cstring_view.h"
#include "corvid/strings/opt_string_view.h"
#include "catch2_main.h"

using namespace corvid;
using namespace corvid::literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("Wrapper formats like its string view", "[StringViewWrapperTest]") {
  if (true) {
    CHECK(std::format("{}", "hi"_czsv) == "hi");
    CHECK(std::format("{}", "hi"_optsv) == "hi");
    CHECK(std::format("{}", ""_optsv).empty());
  }
}

TEST_CASE("Wrapper honors the debug spec", "[StringViewWrapperTest]") {
  if (true) {
    CHECK(std::format("{:?}", "hi"_czsv) == R"("hi")");
    CHECK(std::format("{:?}", "a\tb"_optsv) == R"("a\tb")");
    CHECK(std::format("{:?}", ""_optsv) == R"("")");
  }
}

TEST_CASE("Wrapper honors fill, align, and width", "[StringViewWrapperTest]") {
  if (true) {
    CHECK(std::format("{:>5}", "hi"_czsv) == "   hi");
    CHECK(std::format("{:*^6}", "hi"_czsv) == "**hi**");
    // Width composes with the debug spec for non-null content.
    CHECK(std::format("{:<8?}", "hi"_czsv) == R"("hi"    )");
  }
}

TEST_CASE("Null is transparent under {} and marked under {:?}",
    "[StringViewWrapperTest]") {
  if (true) {
    CHECK(std::format("{}", 0_optsv).empty());
    CHECK(std::format("{:?}", 0_optsv) == "(null)");
    // Width applies to a transparent null, padding the empty rendering.
    CHECK(std::format("{:>3}", 0_optsv) == "   ");
  }
}

TEST_CASE("Null marker honors fill, align, and width",
    "[StringViewWrapperTest]") {
  if (true) {
    CHECK(std::format("{:>10?}", 0_optsv) == "    (null)");
    CHECK(std::format("{:10?}", 0_optsv) == "(null)    ");
    CHECK(std::format("{:*^10?}", 0_optsv) == "**(null)**");
  }
}

TEST_CASE("Dynamic width works with and without the debug spec",
    "[StringViewWrapperTest]") {
  if (true) {
    // Without debug, the base handles dynamic width for both content and the
    // transparent-null padding.
    CHECK(std::format("{:{}}", "hi"_optsv, 5) == "hi   ");
    CHECK(std::format("{:>{}}", 0_optsv, 5) == "     ");

    // With debug, the null marker resolves its own dynamic width and the
    // present value goes through the base. Reached via a runtime format string
    // (vformat) to exercise the runtime parse path on its own.
    auto nul = 0_optsv;
    auto present = "hi"_optsv;
    int w = 8;
    CHECK(std::vformat("{:{}?}", std::make_format_args(nul, w)) == "(null)  ");
    CHECK(std::vformat("{:{}?}", std::make_format_args(present, w)) ==
          R"("hi"    )");
  }
}

TEST_CASE("Wrapper composes inside std range and map",
    "[StringViewWrapperTest]") {
  if (true) {
    // format_kind is disabled for the children, so they format as strings
    // rather than lists of chars, and the range formatter quotes them.
    std::vector<cstring_view> v{"a"_czsv, "b"_czsv};
    CHECK(std::format("{}", v) == R"(["a", "b"])");

    // A null element inside a range still shows the marker, driven by the
    // range formatter's set_debug_format rather than a spec `?`.
    std::vector<opt_string_view> nv{opt_string_view{"a"}, 0_optsv};
    CHECK(std::format("{}", nv) == R"(["a", (null)])");

    // An empty element spec suppresses auto-debug, so the elements are not
    // quoted and a null element is transparent (empty) rather than the marker.
    CHECK(std::format("{::}", nv) == "[a, ]");

    std::map<int, cstring_view> m{{1, "x"_czsv}};
    CHECK(std::format("{}", m) == R"({1: "x"})");
  }
}

TEST_CASE("Wide formatting widens the content", "[StringViewWrapperTest]") {
  if (true) {
    CHECK(std::format(L"{}", L"hi"_wczsv) == L"hi");
    CHECK(std::format(L"{:?}", L"hi"_wczsv) == LR"("hi")");
    opt_wstring_view wnull{};
    CHECK(std::format(L"{:?}", wnull) == L"(null)");
    CHECK(std::format(L"{:>10?}", wnull) == L"    (null)");
  }
}

// The blocks below frame the subject between other fields that consume their
// own (sometimes dynamic) width/precision args, so a miscounted arg id on the
// subject would shift the trailing field and show up in the output. Each
// present-value check is run for both `opt_string_view` and
// `std::string_view`, which must agree; only the null cases are wrapper-only.
// Surrounding fields use strings and ints, not floats, to keep expected output
// exact.

TEST_CASE("Static width and precision, framed, match std::string_view",
    "[StringViewWrapperTest]") {
  if (true) {
    const auto opt = "hello"_optsv;
    const auto sv = "hello"sv;

    // No spec; width as a minimum (a too-small width is a no-op); alignment.
    CHECK(std::format("{:2}[{}]{:<2}", "X", opt, "Y") == "X [hello]Y ");
    CHECK(std::format("{:2}[{}]{:<2}", "X", sv, "Y") == "X [hello]Y ");
    CHECK(std::format("{:2}[{:2}]{:<2}", "X", opt, "Y") == "X [hello]Y ");
    CHECK(std::format("{:2}[{:2}]{:<2}", "X", sv, "Y") == "X [hello]Y ");
    CHECK(std::format("{:2}[{:8}]{:<2}", "X", opt, "Y") == "X [hello   ]Y ");
    CHECK(std::format("{:2}[{:8}]{:<2}", "X", sv, "Y") == "X [hello   ]Y ");
    CHECK(std::format("{:2}[{:>8}]{:<2}", "X", opt, "Y") == "X [   hello]Y ");
    CHECK(std::format("{:2}[{:>8}]{:<2}", "X", sv, "Y") == "X [   hello]Y ");

    // Precision truncates; precision 0; precision past the end; with width.
    CHECK(std::format("{:2}[{:.3}]{:<2}", "X", opt, "Y") == "X [hel]Y ");
    CHECK(std::format("{:2}[{:.3}]{:<2}", "X", sv, "Y") == "X [hel]Y ");
    CHECK(std::format("{:2}[{:.0}]{:<2}", "X", opt, "Y") == "X []Y ");
    CHECK(std::format("{:2}[{:.0}]{:<2}", "X", sv, "Y") == "X []Y ");
    CHECK(std::format("{:2}[{:.9}]{:<2}", "X", opt, "Y") == "X [hello]Y ");
    CHECK(std::format("{:2}[{:.9}]{:<2}", "X", sv, "Y") == "X [hello]Y ");
    CHECK(std::format("{:2}[{:8.3}]{:<2}", "X", opt, "Y") == "X [hel     ]Y ");
    CHECK(std::format("{:2}[{:8.3}]{:<2}", "X", sv, "Y") == "X [hel     ]Y ");
    CHECK(
        std::format("{:2}[{:>8.3}]{:<2}", "X", opt, "Y") == "X [     hel]Y ");
    CHECK(std::format("{:2}[{:>8.3}]{:<2}", "X", sv, "Y") == "X [     hel]Y ");
  }
}

TEST_CASE("Dynamic width and precision, framed, match std::string_view",
    "[StringViewWrapperTest]") {
  if (true) {
    const auto opt = "hello"_optsv;
    const auto sv = "hello"sv;

    // Dynamic width on the subject, framed by a dynamic width before and a
    // dynamic precision after.
    CHECK(std::format("{:{}}[{:{}}]{:.{}}", "X", 2, opt, 8, "Yo", 1) ==
          "X [hello   ]Y");
    CHECK(std::format("{:{}}[{:{}}]{:.{}}", "X", 2, sv, 8, "Yo", 1) ==
          "X [hello   ]Y");

    // Dynamic precision on the subject.
    CHECK(std::format("{:{}}[{:.{}}]{:.{}}", "X", 2, opt, 3, "Yo", 1) ==
          "X [hel]Y");
    CHECK(std::format("{:{}}[{:.{}}]{:.{}}", "X", 2, sv, 3, "Yo", 1) ==
          "X [hel]Y");

    // Dynamic width and precision on the subject.
    CHECK(std::format("{:{}}[{:{}.{}}]{:.{}}", "X", 2, opt, 8, 3, "Yo", 1) ==
          "X [hel     ]Y");
    CHECK(std::format("{:{}}[{:{}.{}}]{:.{}}", "X", 2, sv, 8, 3, "Yo", 1) ==
          "X [hel     ]Y");

    // Mixed: static width with dynamic precision, then the reverse.
    CHECK(std::format("{:{}}[{:8.{}}]{:.{}}", "X", 2, opt, 3, "Yo", 1) ==
          "X [hel     ]Y");
    CHECK(std::format("{:{}}[{:8.{}}]{:.{}}", "X", 2, sv, 3, "Yo", 1) ==
          "X [hel     ]Y");
    CHECK(std::format("{:{}}[{:{}.3}]{:.{}}", "X", 2, opt, 8, "Yo", 1) ==
          "X [hel     ]Y");
    CHECK(std::format("{:{}}[{:{}.3}]{:.{}}", "X", 2, sv, 8, "Yo", 1) ==
          "X [hel     ]Y");

    // Manual indexing, subject in the middle.
    CHECK(std::format("{0:{1}}[{2:{3}.{4}}]{5:.{6}}", "X", 2, opt, 8, 3, "Yo",
              1) == "X [hel     ]Y");
    CHECK(std::format("{0:{1}}[{2:{3}.{4}}]{5:.{6}}", "X", 2, sv, 8, 3, "Yo",
              1) == "X [hel     ]Y");
  }
}

TEST_CASE("Null under a plain spec renders empty like an empty view, framed",
    "[StringViewWrapperTest]") {
  if (true) {
    const auto nul = 0_optsv;
    const std::string_view empty{};

    CHECK(std::format("[{}]", nul) == std::format("[{}]", empty));
    CHECK(std::format("[{}]", nul) == "[]");
    CHECK(std::format("[{:5}]", nul) == std::format("[{:5}]", empty));
    CHECK(std::format("[{:5}]", nul) == "[     ]");
    CHECK(std::format("[{:>5}]", nul) == "[     ]");
    CHECK(std::format("[{:.3}]", nul) == "[]");
    CHECK(std::format("[{:5.3}]", nul) == "[     ]");

    // A transparent null routes through the base, so even a dynamic width
    // works today; framed to confirm the trailing field is unshifted.
    CHECK(std::format("{:{}}[{:{}}]{:.{}}", "X", 2, nul, 5, "Yo", 1) ==
          "X [     ]Y");
    CHECK(std::format("{:{}}[{:{}}]{:.{}}", "X", 2, empty, 5, "Yo", 1) ==
          "X [     ]Y");
  }
}

TEST_CASE("Null marker honors fill, align, and static width, framed",
    "[StringViewWrapperTest]") {
  if (true) {
    const auto nul = 0_optsv;

    CHECK(std::format("{:2}[{:?}]{:<2}", "X", nul, "Y") == "X [(null)]Y ");
    CHECK(
        std::format("{:2}[{:10?}]{:<2}", "X", nul, "Y") == "X [(null)    ]Y ");
    CHECK(std::format("{:2}[{:>10?}]{:<2}", "X", nul, "Y") ==
          "X [    (null)]Y ");
    CHECK(std::format("{:2}[{:*^10?}]{:<2}", "X", nul, "Y") ==
          "X [**(null)**]Y ");
  }
}

TEST_CASE("Empty-present differs from null under the debug spec",
    "[StringViewWrapperTest]") {
  if (true) {
    const auto empty = ""_optsv; // present, zero-length
    const auto nul = 0_optsv;    // absent

    // Plain: both render empty.
    CHECK(std::format("[{}]", empty) == "[]");
    CHECK(std::format("[{}]", nul) == "[]");

    // Debug: a present empty quotes to "", a null shows the marker.
    CHECK(std::format("[{:?}]", empty) == R"([""])");
    CHECK(std::format("[{:?}]", nul) == "[(null)]");

    // Static width reaches both, padding different content.
    CHECK(std::format("[{:6?}]", empty) == R"([""    ])");
    CHECK(std::format("[{:6?}]", nul) == "[(null)]");
  }
}

// Dynamic width on the null marker, resolved via the synthetic parse-context
// trick (plan_dynamic_format_spec). Auto `{}` claims the arg id from the parse
// context; manual `{n}` reads it from the spec. The framed cases prove the
// claim does not shift a trailing field's arg.
TEST_CASE("Null marker honors DYNAMIC width under the debug spec",
    "[StringViewWrapperTest]") {
  if (true) {
    const auto nul = 0_optsv;

    // Auto-indexed dynamic width, alone, with fill and alignment.
    CHECK(std::format("{:{}?}", nul, 10) == "(null)    ");
    CHECK(std::format("{:>{}?}", nul, 10) == "    (null)");
    CHECK(std::format("{:*^{}?}", nul, 10) == "**(null)**");

    // Manual-indexed dynamic width.
    CHECK(std::format("{0:{1}?}", nul, 10) == "(null)    ");

    // Framed: the trailing field must still read its own arg, proving the
    // marker's arg-id claim did not shift the count. Auto, then manual.
    CHECK(std::format("{:{}}[{:{}?}]{:.{}}", "X", 2, nul, 10, "Yo", 1) ==
          "X [(null)    ]Y");
    CHECK(std::format("{0:{1}}[{2:{3}?}]{4:.{5}}", "X", 2, nul, 10, "Yo", 1) ==
          "X [(null)    ]Y");

    // Code-unit generic.
    const opt_wstring_view wnul{};
    CHECK(std::format(L"{:{}?}", wnul, 10) == L"(null)    ");
  }
}

// NOLINTEND(readability-function-cognitive-complexity)
