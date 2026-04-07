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

#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sim_game.h"

namespace corvid { inline namespace sim {

enum class SimClientMessageKind : uint8_t {
  unknown,
  hello,
  ui_canvas,
  ui_action,
};

namespace detail {

[[nodiscard]] inline std::size_t
skip_json_ws(std::string_view msg, std::size_t pos) {
  while (pos < msg.size() &&
         std::isspace(static_cast<unsigned char>(msg[pos])))
    ++pos;
  return pos;
}

[[nodiscard]] inline std::optional<std::size_t>
find_json_value_pos(std::string_view msg, std::string_view key) {
  std::string needle;
  needle.reserve(key.size() + 2);
  needle.push_back('"');
  needle.append(key);
  needle.push_back('"');

  auto pos = msg.find(needle);
  if (pos == std::string_view::npos) return std::nullopt;
  pos = msg.find(':', pos + needle.size());
  if (pos == std::string_view::npos) return std::nullopt;
  return skip_json_ws(msg, pos + 1);
}

[[nodiscard]] inline std::optional<std::string>
parse_json_string_token(std::string_view msg, std::size_t pos) {
  if (pos >= msg.size() || msg[pos] != '"') return std::nullopt;
  ++pos;

  std::string out;
  while (pos < msg.size()) {
    const auto ch = msg[pos++];
    if (ch == '"') return out;
    if (ch == '\\') {
      if (pos >= msg.size()) return std::nullopt;
      out.push_back(msg[pos++]);
      continue;
    }
    out.push_back(ch);
  }
  return std::nullopt;
}

[[nodiscard]] inline std::optional<std::string>
parse_json_string_field(std::string_view msg, std::string_view key) {
  const auto pos = find_json_value_pos(msg, key);
  if (!pos) return std::nullopt;
  return parse_json_string_token(msg, *pos);
}

template<typename UInt>
[[nodiscard]] inline std::optional<UInt>
parse_json_unsigned(std::string_view msg, std::string_view key) {
  const auto pos = find_json_value_pos(msg, key);
  if (!pos) return std::nullopt;
  UInt val{};
  auto [ptr, ec] =
      std::from_chars(msg.data() + *pos, msg.data() + msg.size(), val);
  if (ec != std::errc{}) return std::nullopt;
  return val;
}

[[nodiscard]] inline std::optional<float>
parse_json_float(std::string_view msg, std::string_view key) {
  const auto pos = find_json_value_pos(msg, key);
  if (!pos) return std::nullopt;
  float val{};
  auto [ptr, ec] =
      std::from_chars(msg.data() + *pos, msg.data() + msg.size(), val);
  if (ec != std::errc{}) return std::nullopt;
  return val;
}

[[nodiscard]] inline std::optional<bool>
parse_json_bool(std::string_view msg, std::string_view key) {
  const auto pos = find_json_value_pos(msg, key);
  if (!pos) return std::nullopt;
  if (msg.substr(*pos, 4) == "true") return true;
  if (msg.substr(*pos, 5) == "false") return false;
  return std::nullopt;
}

[[nodiscard]] inline std::vector<UiActionField>
parse_json_fields(std::string_view msg) {
  const auto start = find_json_value_pos(msg, "fields");
  if (!start || *start >= msg.size() || msg[*start] != '{') return {};

  std::vector<UiActionField> fields;
  auto pos = *start + 1;
  while (pos < msg.size()) {
    pos = skip_json_ws(msg, pos);
    if (pos >= msg.size() || msg[pos] == '}') break;

    const auto key = parse_json_string_token(msg, pos);
    if (!key) return {};
    pos = msg.find(':', pos);
    if (pos == std::string_view::npos) return {};
    pos = skip_json_ws(msg, pos + 1);

    const auto value = parse_json_string_token(msg, pos);
    if (!value) return {};
    fields.push_back({std::move(*key), std::move(*value)});

    pos = msg.find_first_of(",}", pos);
    if (pos == std::string_view::npos || msg[pos] == '}') break;
    ++pos;
  }
  return fields;
}

} // namespace detail

[[nodiscard]] inline SimClientMessageKind
classify_sim_client_message(std::string_view msg) {
  if (msg.contains(R"("type":"hello")") ||
      msg.contains(R"("type": "hello")"))
    return SimClientMessageKind::hello;
  if (msg.contains(R"("type":"ui_canvas")") ||
      msg.contains(R"("type": "ui_canvas")"))
    return SimClientMessageKind::ui_canvas;
  if (msg.contains(R"("type":"ui_action")") ||
      msg.contains(R"("type": "ui_action")"))
    return SimClientMessageKind::ui_action;
  return SimClientMessageKind::unknown;
}

[[nodiscard]] inline std::optional<UiCanvasInput>
parse_ui_canvas_message(std::string_view msg) {
  UiCanvasInput input;
  const auto seq = detail::parse_json_unsigned<uint64_t>(msg, "seq");
  const auto event = detail::parse_json_string_field(msg, "event");
  const auto button = detail::parse_json_string_field(msg, "button");
  const auto buttons = detail::parse_json_unsigned<uint32_t>(msg, "buttons");
  const auto x = detail::parse_json_float(msg, "x");
  const auto y = detail::parse_json_float(msg, "y");
  const auto canvas_x = detail::parse_json_float(msg, "canvasX");
  const auto canvas_y = detail::parse_json_float(msg, "canvasY");
  const auto shift = detail::parse_json_bool(msg, "shift");
  const auto ctrl = detail::parse_json_bool(msg, "ctrl");
  const auto alt = detail::parse_json_bool(msg, "alt");
  const auto meta = detail::parse_json_bool(msg, "meta");
  if (!seq || !event || !button || !buttons || !x || !y || !canvas_x ||
      !canvas_y || !shift || !ctrl || !alt || !meta)
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

  if (*event == "click")
    input.event = UiCanvasEvent::click;
  else if (*event == "dblclick")
    input.event = UiCanvasEvent::dblclick;
  else if (*event == "contextmenu")
    input.event = UiCanvasEvent::contextmenu;
  else if (*event == "dragstart")
    input.event = UiCanvasEvent::dragstart;
  else if (*event == "dragmove")
    input.event = UiCanvasEvent::dragmove;
  else if (*event == "dragend")
    input.event = UiCanvasEvent::dragend;
  else
    return std::nullopt;

  if (*button == "left")
    input.button = UiMouseButton::left;
  else if (*button == "middle")
    input.button = UiMouseButton::middle;
  else if (*button == "right")
    input.button = UiMouseButton::right;
  else if (*button == "other")
    input.button = UiMouseButton::other;
  else
    return std::nullopt;

  return input;
}

[[nodiscard]] inline std::optional<UiActionInput>
parse_ui_action_message(std::string_view msg) {
  UiActionInput input;
  const auto seq = detail::parse_json_unsigned<uint64_t>(msg, "seq");
  const auto action = detail::parse_json_string_field(msg, "action");
  if (!seq || !action) return std::nullopt;

  input.seq = *seq;
  input.action = std::move(*action);
  input.fields = detail::parse_json_fields(msg);
  return input;
}

}} // namespace corvid::sim
