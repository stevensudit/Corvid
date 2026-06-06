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

#include "../corvid/proto.h"

#include <string>

#include "catch2_main.h"

using namespace corvid;
// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region Parser_ParseScalarsAndKinds

TEST_CASE("ParseScalarsAndKinds", "[JsonParser]") {
  json_value_view value;

  REQUIRE(parse_json("null", value));
  CHECK(value.is_null());

  REQUIRE(parse_json(" true ", value));
  REQUIRE(value.is_bool());
  const auto bool_value = value.as_bool();
  REQUIRE(bool_value.has_value());
  CHECK(bool_value.value());

  REQUIRE(parse_json("-12.5e2", value));
  REQUIRE(value.is_number());
  const auto number_value = value.as_number<double>();
  REQUIRE(number_value.has_value());
  CHECK(std::abs((number_value.value()) - (-1250.0)) <= 1e-6);

  REQUIRE(parse_json(R"("plain")", value));
  REQUIRE(value.is_string());
  const auto plain_value = value.string_view_if_plain();
  REQUIRE(plain_value.has_value());
  CHECK(plain_value.value() == std::string_view{"plain"});
}

#pragma endregion
#pragma region Parser_ParseNestedViewsAndLookup

TEST_CASE("ParseNestedViewsAndLookup", "[JsonParser]") {
  json_value_view root;
  REQUIRE(parse_json(
      R"({"a\/b":[1,{"nested":false}],"plain":"value","count":2})", root));

  const auto obj = root.as_object();
  REQUIRE(obj);

  const auto plain = obj.get_string_view_if_plain("plain");
  REQUIRE(plain.has_value());
  CHECK(plain.value() == std::string_view{"value"});
  const auto count_value = obj.get_number<int>("count");
  REQUIRE(count_value.has_value());
  CHECK(count_value.value() == 2);

  const auto arr = obj.get_array("a/b");
  REQUIRE(arr);

  size_t count = 0;
  for (const auto item : arr) {
    if (count == 0) {
      const auto item_value = item.as_number<int>();
      REQUIRE(item_value.has_value());
      CHECK(item_value.value() == 1);
    } else {
      const auto nested = item.as_object();
      REQUIRE(nested);
      const auto nested_value = nested.get_bool("nested");
      REQUIRE(nested_value.has_value());
      CHECK_FALSE(*nested_value);
    }
    ++count;
  }
  CHECK(count == 2U);
}

#pragma endregion
#pragma region Parser_DecodesEscapesAndUnicode

TEST_CASE("DecodesEscapesAndUnicode", "[JsonParser]") {
  json_value_view value;
  REQUIRE(parse_json(R"("line\nA\uD83D\uDE00")", value));

  CHECK_FALSE(value.string_view_if_plain().has_value());

  std::string decoded;
  REQUIRE(value.decode_string(decoded));
  std::string expected = "line\nA";
  expected += "\xF0\x9F\x98\x80";
  CHECK(decoded == expected);
}

#pragma endregion
#pragma region Parser_RejectsInvalidJson

TEST_CASE("RejectsInvalidJson", "[JsonParser]") {
  json_value_view value;
  json_error err;

  CHECK_FALSE(parse_json("01", value, &err));
  CHECK(err.code == json_errc::invalid_number);

  CHECK_FALSE(parse_json("1.", value, &err));
  CHECK(err.code == json_errc::invalid_number);

  CHECK_FALSE(parse_json(R"({"a":1,})", value, &err));
  CHECK(err.code == json_errc::expected_key);

  CHECK_FALSE(parse_json("true false", value, &err));
  CHECK(err.code == json_errc::trailing_data);

  CHECK_FALSE(parse_json("NaN", value, &err));
  CHECK(err.code == json_errc::invalid_token);
}

#pragma endregion
#pragma region Parser_RespectsDepthLimit

TEST_CASE("RespectsDepthLimit", "[JsonParser]") {
  json_value_view value;
  json_error err;

  CHECK_FALSE(parse_json("[[[0]]]", value, &err, {.max_depth = 2}));
  CHECK(err.code == json_errc::depth_exceeded);
}

#pragma endregion
#pragma region Writer_EscapesAndTrustedStrings

TEST_CASE("EscapesAndTrustedStrings", "[JsonWriter]") {
  std::string out;
  json_writer writer{out};

  {
    auto root = writer.object();
    root->member("escaped", "a/b")
        .member(json_trusted{"trusted"}, json_trusted{"a/b"});
  }

  CHECK(out == R"({"escaped":"a\/b","trusted":"a/b"})");
}

#pragma endregion
#pragma region Writer_EscapesControlAndNamedChars

TEST_CASE("EscapesControlAndNamedChars", "[JsonWriter]") {
  std::string out;
  json_writer writer{out};

  {
    auto root = writer.object();
    // Quote, backslash, newline (named escapes) and a control char (\uXXXX).
    root->member("s", "a\"b\\c\nd\x01z");
  }

  CHECK(out == "{\"s\":\"a\\\"b\\\\c\\nd\\u0001z\"}");
}

#pragma endregion
#pragma region Writer_FormatsFloatsAndRoundTrips

TEST_CASE("FormatsFloatsAndRoundTrips", "[JsonWriter]") {
  std::string out;
  json_writer writer{out};

  {
    auto root = writer.object();
    root->member("x", 20.0F, std::chars_format::fixed, 1)
        .member("scale", 2.0F, std::chars_format::fixed, 3);
    {
      auto items = root->member_array("items");
      items->value(1).value("two");
      {
        auto item = items->object();
        item->member("ok", true);
      }
    }
  }

  CHECK(out == R"({"x":20.0,"scale":2.000,"items":[1,"two",{"ok":true}]})");

  json_value_view root;
  REQUIRE(parse_json(out, root));
  const auto obj = root.as_object();
  REQUIRE(obj);
  const auto x = obj.get_number<float>("x");
  const auto scale = obj.get_number<float>("scale");
  REQUIRE(x.has_value());
  REQUIRE(scale.has_value());
  CHECK(std::abs((x.value()) - (20.0)) <= 1e-6);
  CHECK(std::abs((scale.value()) - (2.0)) <= 1e-6);

  const auto items = obj.get_array("items");
  REQUIRE(items);
  size_t count = 0;
  for (const auto item : items) {
    if (count == 2) {
      const auto nested = item.as_object();
      REQUIRE(nested);
      const auto ok = nested.get_bool("ok");
      REQUIRE(ok.has_value());
      CHECK(ok.value());
    }
    ++count;
  }
  CHECK(count == 3U);
}

#pragma endregion
#pragma region Writer_WritesScalarsAndTrusted

TEST_CASE("WritesScalarsAndTrusted", "[JsonWriter]") {
  std::string out;
  json_writer writer{out};

  {
    auto root = writer.object();
    root->member("ok", true);

    auto items = root->member_array("items");
    items->value(nullptr).value(json_trusted{"raw"});
  }

  CHECK(out == R"({"ok":true,"items":[null,"raw"]})");
}

#pragma endregion
#pragma region Writer_ScopedContainersAutoClose

TEST_CASE("ScopedContainersAutoClose", "[JsonWriter]") {
  std::string out;
  json_writer writer{out};

  {
    auto root = writer.object();
    root->member("kind", "demo");

    {
      auto meta = root->member_object("meta");
      meta->member("ok", true);
    }

    {
      auto items = root->member_array("items");
      items->value(1);

      {
        auto item = items->object();
        item->member("name", "two");
      }
    }
  }

  CHECK((out) ==
        (R"({"kind":"demo","meta":{"ok":true},"items":[1,{"name":"two"}]})"));
}

#pragma endregion
// NOLINTEND(readability-function-cognitive-complexity)
