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

[[nodiscard]] constexpr bool decodeJsonStringField(json_object_view obj,
    std::string_view key, std::string& out) {
  const auto value = obj.find(key);
  return value && value.decode_string(out);
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
  std::string type;
  if (!msg || !detail::decodeJsonStringField(msg, "type", type)) return kind;
  (void)strings::convert_text_enum(kind, type);
  return kind;
}

[[nodiscard]] constexpr SimClientMessageKind classifySimClientMessage(
    std::string_view msg) {
  return classifySimClientMessage(parseSimClientMessageRoot(msg).value_or({}));
}

[[nodiscard]] constexpr std::optional<UiCanvasInput> parseUiCanvasMessage(
    json_object_view msg) {
  if (!msg) return std::nullopt;

  UiCanvasInput input;
  const auto seq = msg.get_number<uint64_t>("seq");
  const auto buttons = msg.get_number<uint32_t>("buttons");
  const auto x = msg.get_number<float>("x");
  const auto y = msg.get_number<float>("y");
  const auto canvas_x = msg.get_number<float>("canvasX");
  const auto canvas_y = msg.get_number<float>("canvasY");
  const auto shift = msg.get_bool("shift");
  const auto ctrl = msg.get_bool("ctrl");
  const auto alt = msg.get_bool("alt");
  const auto meta = msg.get_bool("meta");

  std::string event;
  std::string button;
  if (!seq || !buttons || !x || !y || !canvas_x || !canvas_y || !shift ||
      !ctrl || !alt || !meta ||
      !detail::decodeJsonStringField(msg, "event", event) ||
      !detail::decodeJsonStringField(msg, "button", button))
    return std::nullopt;

  input.seq = *seq;
  input.buttons = *buttons;
  input.x = *x;
  input.y = *y;
  input.canvasX = *canvas_x;
  input.canvasY = *canvas_y;
  input.shift = *shift;
  input.ctrl = *ctrl;
  input.alt = *alt;
  input.meta = *meta;

  if (event == "click")
    input.event = UiCanvasEvent::click;
  else if (event == "dblclick")
    input.event = UiCanvasEvent::dblclick;
  else if (event == "contextmenu")
    input.event = UiCanvasEvent::contextmenu;
  else if (event == "dragstart")
    input.event = UiCanvasEvent::dragstart;
  else if (event == "dragmove")
    input.event = UiCanvasEvent::dragmove;
  else if (event == "dragend")
    input.event = UiCanvasEvent::dragend;
  else
    return std::nullopt;

  if (button == "left")
    input.button = UiMouseButton::left;
  else if (button == "middle")
    input.button = UiMouseButton::middle;
  else if (button == "right")
    input.button = UiMouseButton::right;
  else if (button == "other")
    input.button = UiMouseButton::other;
  else
    return std::nullopt;

  return input;
}

[[nodiscard]] constexpr std::optional<UiCanvasInput> parse_ui_canvas_message(
    std::string_view msg) {
  const auto root = parseSimClientMessageRoot(msg);
  return root ? parseUiCanvasMessage(*root) : std::nullopt;
}

[[nodiscard]] constexpr std::optional<UiActionInput> parse_ui_action_message(
    json_object_view msg) {
  if (!msg) return std::nullopt;

  UiActionInput input;
  const auto seq = msg.get_number<uint64_t>("seq");
  if (!seq || !detail::decodeJsonStringField(msg, "action", input.action))
    return std::nullopt;

  input.seq = *seq;
  input.fields = detail::parseActionFields(msg);
  return input;
}

[[nodiscard]] constexpr std::optional<UiActionInput> parse_ui_action_message(
    std::string_view msg) {
  const auto root = parseSimClientMessageRoot(msg);
  return root ? parse_ui_action_message(*root) : std::nullopt;
}
}} // namespace corvid::sim
