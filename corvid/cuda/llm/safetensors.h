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

// Linux-only: rests on "linux_mmap.h".
#ifdef _WIN32
#error "\"safetensors.h\" is Linux-only."
#endif
#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../containers/core/opt_find.h"
#include "../../containers/core/transparent.h"
#include "../../enums/sequence_enum.h"
#include "../../filesys/linux_mmap.h"
#include "../../math/endian.h"
#include "../../proto/misc/json_parser.h"
#include "../../strings/cstring_view.h"

// A reader for the safetensors weight file format.
//
// A safetensors file is a little-endian 64-bit header length, a JSON header
// of that length naming each tensor's dtype, shape, and byte range, then one
// buffer holding every tensor's data, row-major and little-endian. The format
// has no code path, so reading it safely is a matter of checking the header's
// claims against the file, which `safetensors_file` does before exposing any
// tensor.
//
// Loading a file:
//   safetensors_file weights;
//   if (!weights.load("model.safetensors")) ...
//   if (const auto* wte = weights.find("wte.weight"); wte && wte->is<float>())
//     std::span<const float> values = wte->as<float>();
namespace corvid::llm {

#pragma region tensor_dtype

// Element type of a tensor, with the names the safetensors header uses.
enum class tensor_dtype : uint8_t {
  f64,
  f32,
  f16,
  bf16,
  i64,
  i32,
  i16,
  i8,
  u8,
  boolean
};
consteval auto corvid_enum_spec(tensor_dtype*) {
  return corvid::enums::sequence::make_sequence_enum_spec<tensor_dtype,
      "F64,F32,F16,BF16,I64,I32,I16,I8,U8,BOOL">();
}

// The size in bytes of one element of `dtype`.
[[nodiscard]] constexpr size_t dtype_size(tensor_dtype dtype) noexcept {
  using enum tensor_dtype;
  switch (dtype) {
  case f64:
  case i64: return 8;
  case f32:
  case i32: return 4;
  case f16:
  case bf16:
  case i16: return 2;
  case i8:
  case u8:
  case boolean: return 1;
  }
  // Unreachable: every enumerator is listed above. A future dtype without a
  // case lands here, and a zero size makes every count of it fail loudly.
  return 0;
}

// An element type with a `tensor_dtype` twin.
template<typename T>
concept TensorElement =
    std::same_as<T, double> || std::same_as<T, float> ||
    std::same_as<T, int64_t> || std::same_as<T, int32_t> ||
    std::same_as<T, int16_t> || std::same_as<T, int8_t> ||
    std::same_as<T, uint8_t> || std::same_as<T, bool>;

// The dtype whose elements are `T`.
template<TensorElement T>
[[nodiscard]] consteval tensor_dtype dtype_of() noexcept {
  using enum tensor_dtype;
  if constexpr (std::same_as<T, double>) return f64;
  if constexpr (std::same_as<T, float>) return f32;
  if constexpr (std::same_as<T, int64_t>) return i64;
  if constexpr (std::same_as<T, int32_t>) return i32;
  if constexpr (std::same_as<T, int16_t>) return i16;
  if constexpr (std::same_as<T, int8_t>) return i8;
  if constexpr (std::same_as<T, uint8_t>) return u8;
  if constexpr (std::same_as<T, bool>) return boolean;
}

#pragma endregion
#pragma region safetensors_file

// The tensors of one safetensors file, found by name and viewed in place.
//
// `parse` reads a file image the caller keeps alive, and `load` maps a file
// and keeps the mapping. Either validates the whole header before exposing
// anything: the header length is capped, every byte range lies within the
// buffer, the ranges tile the buffer exactly with no gaps or overlaps (as the
// reference implementation requires), every range's size matches its shape
// and dtype, and names are unique. On failure, both leave the object as it
// was.
class safetensors_file {
public:
#pragma region tensor

  // One tensor's header fields and a view of its bytes.
  struct tensor {
    std::string name;
    tensor_dtype dtype{};
    std::vector<size_t> shape;
    std::span<const std::byte> bytes;

    // The element count, which is the product of the shape.
    [[nodiscard]] size_t count() const noexcept {
      return bytes.size() / dtype_size(dtype);
    }

    // Whether the elements are `T` and aligned for it, which `as` requires.
    template<TensorElement T>
    [[nodiscard]] bool is() const noexcept {
      return dtype == dtype_of<T>() &&
             std::bit_cast<uintptr_t>(bytes.data()) % alignof(T) == 0;
    }

    // The elements as `T`, which `is` must confirm first (asserted).
    template<TensorElement T>
    [[nodiscard]] std::span<const T> as() const {
      assert(is<T>());
      // Parens, not braces: C++26 gives span an initializer_list constructor.
      // NOLINTNEXTLINE(modernize-return-braced-init-list)
      return std::span<const T>(reinterpret_cast<const T*>(bytes.data()),
          count());
    }
  };

#pragma endregion
#pragma region Loading

  // Map the file at `path` and parse it, keeping the mapping.
  //
  // On failure (the file cannot be mapped, or `parse` rejects it), returns
  // false, leaving the object as it was.
  [[nodiscard]] bool load(cstring_view path) {
    auto mapping = memory_map::map_file(path);
    if (!mapping) return false;
    if (!parse(mapping.bytes())) return false;
    mapping_ = std::move(mapping);
    return true;
  }

  // Parse the file image `file`, which must outlive the tensors' views.
  //
  // Releases any mapping a previous `load` kept. On failure, returns false,
  // leaving the object as it was.
  [[nodiscard]] bool parse(std::span<const std::byte> file) {
    // The header length, then the header, then the buffer.
    uint64_t header_size{};
    if (file.size() < sizeof(header_size)) return false;
    std::memcpy(&header_size, file.data(), sizeof(header_size));
    header_size = swap_not_little(header_size);
    if (header_size > max_header_size ||
        header_size > file.size() - sizeof(header_size))
      return false;
    const auto header = file.subspan(sizeof(header_size), header_size);
    const auto buffer = file.subspan(sizeof(header_size) + header_size);

    json_value_view root;
    if (!parse_json(strings::as_string_view(header), root) ||
        !root.is_object())
      return false;

    std::vector<tensor> tensors;
    string_unordered_map<size_t> index;
    string_map<std::string> metadata;
    std::string key;
    for (const auto [key_view, value] : root.as_object()) {
      if (!key_view.decode_string(key)) return false;
      if (key == "__metadata__") {
        if (!parse_metadata(metadata, value)) return false;
        continue;
      }
      tensor entry;
      entry.name = std::move(key);
      if (!parse_tensor(entry, value, buffer)) return false;
      if (!index.emplace(entry.name, tensors.size()).second) return false;
      tensors.push_back(std::move(entry));
    }
    if (!tiles_buffer(tensors, buffer)) return false;

    tensors_ = std::move(tensors);
    index_ = std::move(index);
    metadata_ = std::move(metadata);
    mapping_ = {};
    return true;
  }

#pragma endregion
#pragma region Tensors

  // Find the tensor named `name`, or null.
  [[nodiscard]] const tensor* find(std::string_view name) const noexcept {
    if (const auto ndx = find_opt(index_, name)) return &tensors_[*ndx];
    return nullptr;
  }

  // Every tensor, in header order.
  [[nodiscard]] std::span<const tensor> tensors() const noexcept {
    return tensors_;
  }

  [[nodiscard]] size_t size() const noexcept { return tensors_.size(); }

  // The header's `__metadata__` strings, if any.
  [[nodiscard]] const auto& metadata() const noexcept { return metadata_; }

#pragma endregion
#pragma region Helpers
private:
  // The reference implementation's cap on the header length.
  static constexpr size_t max_header_size = 100'000'000;

  // Parse the `__metadata__` object `value` into `metadata`.
  [[nodiscard]] static bool
  parse_metadata(string_map<std::string>& metadata, json_value_view value) {
    const auto fields = value.as_object();
    if (!fields) return false;
    std::string key, text;
    for (const auto [key_view, field] : fields) {
      if (!key_view.decode_string(key) || !field.decode_string(text))
        return false;
      metadata.insert_or_assign(std::move(key), std::move(text));
    }
    return true;
  }

  // Parse the tensor object `value` into `entry`, whose name is already set,
  // taking its bytes from `buffer`.
  [[nodiscard]] static bool parse_tensor(tensor& entry, json_value_view value,
      std::span<const std::byte> buffer) {
    const auto fields = value.as_object();
    if (!fields) return false;

    std::string dtype_name;
    if (!fields.get_string("dtype", dtype_name)) return false;
    const auto dtype = sequence::enum_find_by_name<tensor_dtype>(dtype_name);
    if (!dtype) return false;
    entry.dtype = *dtype;

    const auto shape = fields.get_array("shape");
    if (!shape) return false;
    size_t count = 1;
    for (const auto dim : shape) {
      const auto extent = dim.as_number<size_t>();
      if (!extent) return false;
      if (*extent && count > std::numeric_limits<size_t>::max() / *extent)
        return false;
      entry.shape.push_back(*extent);
      count *= *extent;
    }

    const auto offsets = fields.get_array("data_offsets");
    if (!offsets) return false;
    std::array<size_t, 2> range{};
    size_t ndx = 0;
    for (const auto item : offsets) {
      const auto offset = item.as_number<size_t>();
      if (ndx == range.size() || !offset) return false;
      range[ndx++] = *offset;
    }
    if (ndx != range.size()) return false;
    const auto [begin, end] = range;
    if (begin > end || end > buffer.size() ||
        end - begin != count * dtype_size(entry.dtype))
      return false;
    entry.bytes = buffer.subspan(begin, end - begin);
    return true;
  }

  // Whether the tensors' byte ranges tile `buffer` exactly, in some order,
  // with no gaps or overlaps.
  [[nodiscard]] static bool tiles_buffer(std::span<const tensor> tensors,
      std::span<const std::byte> buffer) {
    std::vector<std::pair<size_t, size_t>> byte_ranges;
    byte_ranges.reserve(tensors.size());
    for (const auto& entry : tensors) {
      const auto begin =
          static_cast<size_t>(entry.bytes.data() - buffer.data());
      byte_ranges.emplace_back(begin, begin + entry.bytes.size());
    }
    if (byte_ranges.empty()) return buffer.empty();
    std::ranges::sort(byte_ranges);
    const auto seam = std::ranges::adjacent_find(byte_ranges,
        [](const auto& prev, const auto& next) {
          return prev.second != next.first;
        });
    return (seam == byte_ranges.end()) && (byte_ranges.front().first == 0) &&
           (byte_ranges.back().second == buffer.size());
  }

#pragma endregion
#pragma region Data members

  memory_map mapping_;
  std::vector<tensor> tensors_;
  string_unordered_map<size_t> index_;
  string_map<std::string> metadata_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::llm
