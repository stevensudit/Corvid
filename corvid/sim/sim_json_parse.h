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
#include "sim_game.h"

namespace corvid { inline namespace sim {
enum class SimClientMessageKind : uint8_t {
  unknown,
  hello,
  ui_canvas,
  ui_action,
};

namespace detail {

[[nodiscard]] constexpr bool decode_json_string_field(json_object_view obj,
    std::string_view key, std::string& out) {
  const auto value = obj.find(key);
  return value && value.decode_string(out);
}

template<typename T>
[[nodiscard]] constexpr std::optional<T>
get_json_number(json_object_view obj, std::string_view key) {
  return obj.get_number<T>(key);
}

[[nodiscard]] constexpr std::vector<UiActionField> parse_json_fields(
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
parse_sim_client_message_root(std::string_view msg) {
  json_value_view root;
  if (!parse_json(msg, root)) return std::nullopt;
  const auto obj = root.as_object();
  if (!obj) return std::nullopt;
  return obj;
}

[[nodiscard]] constexpr SimClientMessageKind classify_sim_client_message(
    json_object_view msg) {
  if (!msg) return SimClientMessageKind::unknown;

  std::string type;
  if (!detail::decode_json_string_field(msg, "type", type))
    return SimClientMessageKind::unknown;

  if (type == "hello") return SimClientMessageKind::hello;
  if (type == "ui_canvas") return SimClientMessageKind::ui_canvas;
  if (type == "ui_action") return SimClientMessageKind::ui_action;
  return SimClientMessageKind::unknown;
}

[[nodiscard]] constexpr SimClientMessageKind classify_sim_client_message(
    std::string_view msg) {
  const auto root = parse_sim_client_message_root(msg);
  return root ? classify_sim_client_message(*root)
              : SimClientMessageKind::unknown;
}

[[nodiscard]] constexpr std::optional<UiCanvasInput> parse_ui_canvas_message(
    json_object_view msg) {
  if (!msg) return std::nullopt;

  UiCanvasInput input;
  const auto seq = detail::get_json_number<uint64_t>(msg, "seq");
  const auto buttons = detail::get_json_number<uint32_t>(msg, "buttons");
  const auto x = detail::get_json_number<float>(msg, "x");
  const auto y = detail::get_json_number<float>(msg, "y");
  const auto canvas_x = detail::get_json_number<float>(msg, "canvasX");
  const auto canvas_y = detail::get_json_number<float>(msg, "canvasY");
  const auto shift = msg.get_bool("shift");
  const auto ctrl = msg.get_bool("ctrl");
  const auto alt = msg.get_bool("alt");
  const auto meta = msg.get_bool("meta");

  std::string event;
  std::string button;
  if (!seq || !buttons || !x || !y || !canvas_x || !canvas_y || !shift ||
      !ctrl || !alt || !meta ||
      !detail::decode_json_string_field(msg, "event", event) ||
      !detail::decode_json_string_field(msg, "button", button))
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
  const auto root = parse_sim_client_message_root(msg);
  return root ? parse_ui_canvas_message(*root) : std::nullopt;
}

[[nodiscard]] constexpr std::optional<UiActionInput> parse_ui_action_message(
    json_object_view msg) {
  if (!msg) return std::nullopt;

  UiActionInput input;
  const auto seq = detail::get_json_number<uint64_t>(msg, "seq");
  if (!seq || !detail::decode_json_string_field(msg, "action", input.action))
    return std::nullopt;

  input.seq = *seq;
  input.fields = detail::parse_json_fields(msg);
  return input;
}

[[nodiscard]] constexpr std::optional<UiActionInput> parse_ui_action_message(
    std::string_view msg) {
  const auto root = parse_sim_client_message_root(msg);
  return root ? parse_ui_action_message(*root) : std::nullopt;
}
}} // namespace corvid::sim
