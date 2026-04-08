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

#include <sstream>
#include <string>

#include "minitest.h"

using namespace corvid;
// NOLINTBEGIN(readability-function-cognitive-complexity)
void JsonParser_ParseScalarsAndKinds() {
  json_value_view value;

  ASSERT_TRUE(parse_json("null", value));
  EXPECT_TRUE(value.is_null());

  ASSERT_TRUE(parse_json(" true ", value));
  ASSERT_TRUE(value.is_bool());
  const auto bool_value = value.as_bool();
  ASSERT_TRUE(bool_value.has_value());
  EXPECT_TRUE(*bool_value);

  ASSERT_TRUE(parse_json("-12.5e2", value));
  ASSERT_TRUE(value.is_number());
  const auto number_value = value.as_number<double>();
  ASSERT_TRUE(number_value.has_value());
  EXPECT_NEAR(*number_value, -1250.0, 1e-6);

  ASSERT_TRUE(parse_json(R"("plain")", value));
  ASSERT_TRUE(value.is_string());
  const auto plain_value = value.string_view_if_plain();
  ASSERT_TRUE(plain_value.has_value());
  EXPECT_EQ(*plain_value, std::string_view{"plain"});
}

void JsonParser_ParseNestedViewsAndLookup() {
  json_value_view root;
  ASSERT_TRUE(parse_json(
      R"({"a\/b":[1,{"nested":false}],"plain":"value","count":2})", root));

  const auto obj = root.as_object();
  ASSERT_TRUE(obj);

  const auto plain = obj.get_string_view_if_plain("plain");
  ASSERT_TRUE(plain.has_value());
  EXPECT_EQ(*plain, std::string_view{"value"});
  const auto count_value = obj.get_number<int>("count");
  ASSERT_TRUE(count_value.has_value());
  EXPECT_EQ(*count_value, 2);

  const auto arr = obj.get_array("a/b");
  ASSERT_TRUE(arr);

  size_t count = 0;
  for (const auto item : arr) {
    if (count == 0) {
      const auto item_value = item.as_number<int>();
      ASSERT_TRUE(item_value.has_value());
      EXPECT_EQ(*item_value, 1);
    } else {
      const auto nested = item.as_object();
      ASSERT_TRUE(nested);
      const auto nested_value = nested.get_bool("nested");
      ASSERT_TRUE(nested_value.has_value());
      EXPECT_FALSE(*nested_value);
    }
    ++count;
  }
  EXPECT_EQ(count, 2U);
}

void JsonParser_DecodesEscapesAndUnicode() {
  json_value_view value;
  ASSERT_TRUE(parse_json(R"("line\nA\uD83D\uDE00")", value));

  EXPECT_FALSE(value.string_view_if_plain().has_value());

  std::string decoded;
  ASSERT_TRUE(value.decode_string(decoded));
  std::string expected = "line\nA";
  expected += "\xF0\x9F\x98\x80";
  EXPECT_EQ(decoded, expected);
}

void JsonParser_RejectsInvalidJson() {
  json_value_view value;
  json_error err;

  EXPECT_FALSE(parse_json("01", value, &err));
  EXPECT_EQ(err.code, json_errc::invalid_number);

  EXPECT_FALSE(parse_json("1.", value, &err));
  EXPECT_EQ(err.code, json_errc::invalid_number);

  EXPECT_FALSE(parse_json(R"({"a":1,})", value, &err));
  EXPECT_EQ(err.code, json_errc::expected_key);

  EXPECT_FALSE(parse_json("true false", value, &err));
  EXPECT_EQ(err.code, json_errc::trailing_data);

  EXPECT_FALSE(parse_json("NaN", value, &err));
  EXPECT_EQ(err.code, json_errc::invalid_token);
}

void JsonParser_RespectsDepthLimit() {
  json_value_view value;
  json_error err;

  EXPECT_FALSE(parse_json("[[[0]]]", value, &err, {.max_depth = 2}));
  EXPECT_EQ(err.code, json_errc::depth_exceeded);
}

void JsonWriter_EscapesAndTrustedStrings() {
  std::string out;
  json_writer writer{out};

  {
    auto root = writer.object();
    root->member("escaped", "a/b")
        .member(json_trusted{"trusted"}, json_trusted{"a/b"});
  }

  EXPECT_EQ(out, R"({"escaped":"a\/b","trusted":"a/b"})");
}

void JsonWriter_FormatsFloatsAndRoundTrips() {
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

  EXPECT_EQ(out, R"({"x":20.0,"scale":2.000,"items":[1,"two",{"ok":true}]})");

  json_value_view root;
  ASSERT_TRUE(parse_json(out, root));
  const auto obj = root.as_object();
  ASSERT_TRUE(obj);
  const auto x = obj.get_number<float>("x");
  const auto scale = obj.get_number<float>("scale");
  ASSERT_TRUE(x.has_value());
  ASSERT_TRUE(scale.has_value());
  EXPECT_NEAR(*x, 20.0, 1e-6);
  EXPECT_NEAR(*scale, 2.0, 1e-6);

  const auto items = obj.get_array("items");
  ASSERT_TRUE(items);
  size_t count = 0;
  for (const auto item : items) {
    if (count == 2) {
      const auto nested = item.as_object();
      ASSERT_TRUE(nested);
      const auto ok = nested.get_bool("ok");
      ASSERT_TRUE(ok.has_value());
      EXPECT_TRUE(*ok);
    }
    ++count;
  }
  EXPECT_EQ(count, 3U);
}

void JsonWriter_WritesToOstreamTargets() {
  std::ostringstream out;
  json_writer writer{out};

  {
    auto root = writer.object();
    root->member("ok", true);

    auto items = root->member_array("items");
    items->value(nullptr).value(json_trusted{"raw"});
  }

  EXPECT_EQ(out.str(), R"({"ok":true,"items":[null,"raw"]})");
}

void JsonWriter_ScopedContainersAutoClose() {
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

  EXPECT_EQ(out,
      R"({"kind":"demo","meta":{"ok":true},"items":[1,{"name":"two"}]})");
}

MAKE_TEST_LIST(JsonParser_ParseScalarsAndKinds,
    JsonParser_ParseNestedViewsAndLookup, JsonParser_DecodesEscapesAndUnicode,
    JsonParser_RejectsInvalidJson, JsonParser_RespectsDepthLimit,
    JsonWriter_EscapesAndTrustedStrings, JsonWriter_FormatsFloatsAndRoundTrips,
    JsonWriter_WritesToOstreamTargets, JsonWriter_ScopedContainersAutoClose);
// NOLINTEND(readability-function-cognitive-complexity)
