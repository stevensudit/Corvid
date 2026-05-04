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
#include <sys/mman.h>
#include <sys/uio.h>

#include "iou_wrap.h"

namespace corvid { inline namespace proto { namespace iouring {

enum class block_type : bool { read, write };

// Fwd.
class iou_buffer;

#pragma region buffer_pool_base

// Abstract backing pool used by `iou_buffer`.
class buffer_pool_base {
public:
  using span_t = iou_sqe::span_t;
  using const_span_t = iou_sqe::const_span_t;
  using buffer = iou_buffer;
  using ptr = std::byte*;
  using cptr = const std::byte*;
  static constexpr size_t hugepage_size = 2ULL * 1024 * 1024;

  virtual ~buffer_pool_base() = default;

private:
  friend class iou_buffer;

  // Return base address of the pool's memory region.
  [[nodiscard]] virtual std::byte* base() const noexcept = 0;

  // Return a buffer to the pool.
  virtual void return_buffer(span_t s, block_type blockrw) noexcept = 0;

  // Track read bytes separately, to selectively throttle.
  virtual void decrement_read_bytes(size_t n) noexcept = 0;
  virtual void increment_read_bytes(size_t n) noexcept = 0;

protected:
  [[nodiscard]] static iou_buffer make_buffer(buffer_pool_base& pool,
      span_t span, size_t buf_index, block_type blockrw) noexcept;

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
  kb001 = 1UL * 1024,
  kb002 = 2UL * 1024, // 2 KB; fits a UDP payload inside a standard MTU
  kb004 = 4UL * 1024,
  kb008 = 8UL * 1024,
  kb016 = 16UL * 1024,
  kb032 = 32UL * 1024,
  kb064 = 64UL * 1024,
  kb128 = 128UL * 1024,
  kb256 = 256UL * 1024,
  kb512 = 512UL * 1024,
  m01 = 1UL * 1024 * 1024,
  m02 = 2UL * 1024 * 1024,
  m04 = 4UL * 1024 * 1024,
  m08 = 8UL * 1024 * 1024,
  m16 = 16UL * 1024 * 1024,
  m32 = 32UL * 1024 * 1024,
  m64 = 64UL * 1024 * 1024,
};

// `PROT_*` wrapper.
enum class mmap_prot {
  none = PROT_NONE,           // 0x00
  read = PROT_READ,           // 0x01
  write = PROT_WRITE,         // 0x02
  exec = PROT_EXEC,           // 0x04
  growsdown = PROT_GROWSDOWN, // 0x01000000
  growsup = PROT_GROWSUP,     // 0x02000000
};

// `MAP_*` wrapper.
enum class mmap_map {
  file = MAP_FILE,                       // 0x00
  shared = MAP_SHARED,                   // 0x01
  map_private = MAP_PRIVATE,             // 0x02
  shared_validate = MAP_SHARED_VALIDATE, // 0x03
  mask_type = 0x0f,                      // Mask for mapping
  fixed = MAP_FIXED,                     // 0x10
  anonymous = MAP_ANONYMOUS,             // 0x20
  map_huge_shuft = MAP_HUGE_SHIFT,       // 26
  map_huge_mask = MAP_HUGE_MASK,         // 0x3f
};

}}} // namespace corvid::proto::iouring

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::iouring::block_size> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::proto::iouring::block_size,
        "kb001, kb002, kb004, kb008, kb016, kb032, kb064, kb128, kb256, "
        "kb512, m01, m02, m04, m08, m16, m32, m64">();

namespace corvid { inline namespace proto { namespace iouring {

#pragma endregion

}}} // namespace corvid::proto::iouring
