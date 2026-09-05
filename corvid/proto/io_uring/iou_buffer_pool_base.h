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
#include <cstddef>
#include <memory>
#include <sys/uio.h>

#include "../../filesys/mmap.h"
#include "iou_wrap.h"

namespace corvid { inline namespace proto { namespace iouring {

enum class block_type : bool { read, write };

// Fwd.
class iou_buffer;

#pragma region buffer_pool_base

// Abstract backing pool used by `iou_buffer`. Pools must be instantiated via
// `std::make_shared` (their concrete factories enforce this), because each
// `iou_buffer` holds a `std::shared_ptr<buffer_pool_base>` to its pool. That
// keeps the pool alive as long as any buffer it produced is still in scope,
// so a buffer can always return its allocation on destruction.
class buffer_pool_base: public std::enable_shared_from_this<buffer_pool_base> {
public:
  using span_t = iou_sqe::span_t;
  using const_span_t = iou_sqe::const_span_t;
  using buffer = iou_buffer;
  using ptr = std::byte*;
  using cptr = const std::byte*;

  virtual ~buffer_pool_base() = default;

private:
  friend class iou_buffer;

  // Base address of the pool's memory region.
  [[nodiscard]] virtual std::byte* base() const noexcept = 0;

  // Return a buffer to the pool.
  [[nodiscard]] virtual bool return_buffer(span_t s, block_type blockrw) = 0;

  // Track read bytes separately, to selectively throttle.
  [[nodiscard]] virtual bool decrement_read_bytes(size_t) { return false; }
  [[nodiscard]] virtual bool increment_read_bytes(size_t) { return false; }

protected:
  [[nodiscard]] static iou_buffer
  make_buffer(const std::shared_ptr<buffer_pool_base>& pool, span_t span,
      size_t buf_index, block_type blockrw) noexcept;

  // TODO: We'll need a way to make Provided Buffers programmatically
  // detectable. What we really want is for the regular flow, where the user
  // tries to append to the buffer, to fail as though the buffer were full.
  // Perhaps it should just return an empty `active_buffer` in that case.
};

#pragma endregion

#pragma region block_size

// Standard block sizes. Must be a power of two, but you can cast arbitrary
// values to this type if you need larger or smaller ones.
//
// NOLINTNEXTLINE(performance-enum-size)
enum class block_size : size_t {
  kb001 = 1 * 1024UZ,
  kb002 = 2 * 1024UZ, // 2 KB; fits a UDP payload inside a standard MTU
  kb004 = 4 * 1024UZ,
  kb008 = 8 * 1024UZ,
  kb016 = 16 * 1024UZ,
  kb032 = 32 * 1024UZ,
  kb064 = 64 * 1024UZ,
  kb128 = 128 * 1024UZ,
  kb256 = 256 * 1024UZ,
  kb512 = 512 * 1024UZ,
  m01 = 1 * 1024UZ * 1024UZ,
  m02 = 2 * 1024UZ * 1024UZ,
  m04 = 4 * 1024UZ * 1024UZ,
  m08 = 8 * 1024UZ * 1024UZ,
  m16 = 16 * 1024UZ * 1024UZ,
  m32 = 32 * 1024UZ * 1024UZ,
  m64 = 64 * 1024UZ * 1024UZ,
};
consteval auto corvid_enum_spec(block_size*) {
  return corvid::enums::sequence::make_sequence_enum_spec<block_size, "">();
}

#pragma endregion

}}} // namespace corvid::proto::iouring
