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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../proto/json_parser.h"
#include "../strings/enum_conversion.h"
#include "sim_game.h"

namespace corvid { inline namespace sim {
enum class SimClientMessageKind : uint8_t {
  unknown,
  hello,
  ui_canvas,
  ui_action,
};

}} // namespace corvid::sim

template<>
constexpr auto
    corvid::enums::registry::enum_spec_v<corvid::sim::SimClientMessageKind> =
        corvid::enums::sequence::make_sequence_enum_spec<
            corvid::sim::SimClientMessageKind,
            "unknown, hello, ui_canvas, ui_action">();

namespace corvid { inline namespace sim {
namespace detail {

[[nodiscard]] constexpr bool
decodeString(json_object_view obj, std::string_view key, std::string& out) {
  const auto value = obj.find(key);
  return value && value.decode_string(out);
}

[[nodiscard]] constexpr std::string
decodeString(json_object_view obj, std::string_view key) {
  std::string str;
  (void)decodeString(obj, key, str);
  return str;
}

[[nodiscard]] constexpr std::vector<UiActionField> parseActionFields(
    json_object_view obj) {
  std::vector<UiActionField> fields;
  const auto fields_obj = obj.get_object("fields");
  if (!fields_obj) return fields;

  std::string key;
  std::string value;
  for (const auto field : fields_obj) {
    if (!field.key.decode_string(key) || !field.value.decode_string(value)) {
      fields.clear();
      return fields;
    }
    fields.push_back({key, value});
  }
  return fields;
}

} // namespace detail

[[nodiscard]] constexpr std::optional<json_object_view>
parseSimClientMessageRoot(std::string_view msg) {
  json_value_view root;
  if (!parse_json(msg, root)) return std::nullopt;
  const auto obj = root.as_object();
  if (!obj) return std::nullopt;
  return obj;
}

[[nodiscard]] constexpr SimClientMessageKind classifySimClientMessage(
    json_object_view msg) {
  SimClientMessageKind kind{};
  (void)strings::convert_text_enum(kind, detail::decodeString(msg, "type"));
  return kind;
}

[[nodiscard]] constexpr SimClientMessageKind classifySimClientMessage(
    std::string_view msg) {
  return classifySimClientMessage(
      parseSimClientMessageRoot(msg).value_or(json_object_view{}));
}

[[nodiscard]] constexpr std::optional<UiCanvasInput> parseUiCanvasMessage(
    json_object_view msg) {
  if (!msg) return std::nullopt;

  UiCanvasInput input;
  if (!msg.parse_number<uint64_t>("seq", input.seq) ||
      !msg.parse_number<uint32_t>("buttons", input.buttons) ||
      !msg.parse_number<float>("x", input.x) ||
      !msg.parse_number<float>("y", input.y) ||
      !msg.parse_number<float>("canvasX", input.canvasX) ||
      !msg.parse_number<float>("canvasY", input.canvasY) ||
      !msg.parse_bool("shift", input.shift) ||
      !msg.parse_bool("ctrl", input.ctrl) ||
      !msg.parse_bool("alt", input.alt) || !msg.parse_bool("meta", input.meta))
    return std::nullopt;

  if (!strings::convert_text_enum(input.event,
          detail::decodeString(msg, "event")))
    return std::nullopt;

  if (!strings::convert_text_enum(input.button,
          detail::decodeString(msg, "button")))
    return std::nullopt;

  return input;
}

[[nodiscard]] constexpr std::optional<UiCanvasInput> parseUiCanvasMessage(
    std::string_view msg) {
  const auto root = parseSimClientMessageRoot(msg);
  return root ? parseUiCanvasMessage(*root) : std::nullopt;
}

[[nodiscard]] constexpr std::optional<UiActionInput> parseUiActionMessage(
    json_object_view msg) {
  if (!msg) return std::nullopt;

  UiActionInput input;
  const auto seq = msg.get_number<uint64_t>("seq");
  if (!seq || !detail::decodeString(msg, "action", input.action))
    return std::nullopt;

  input.seq = *seq;
  input.fields = detail::parseActionFields(msg);
  return input;
}

[[nodiscard]] constexpr std::optional<UiActionInput> parseUiActionMessage(
    std::string_view msg) {
  const auto root = parseSimClientMessageRoot(msg);
  return root ? parseUiActionMessage(*root) : std::nullopt;
}
}} // namespace corvid::sim
