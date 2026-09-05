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

// Linux-only: memory mapping has no shared vocabulary with Windows yet.
#ifdef _WIN32
#error "\"mmap.h\" is Linux-only."
#endif
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../enums/bitmask_enum.h"
#include "../enums/sequence_enum.h"
#include "../strings/cstring_view.h"
#include "os_enums.h"
#include "os_file.h"

namespace corvid { inline namespace filesys {

#pragma region mmap_prot

// `PROT_*` wrapper.
enum class mmap_prot : uint32_t {
  none = PROT_NONE,           // 0x00
  read = PROT_READ,           // 0x01
  write = PROT_WRITE,         // 0x02
  exec = PROT_EXEC,           // 0x04
  growsdown = PROT_GROWSDOWN, // 0x01000000
  growsup = PROT_GROWSUP,     // 0x02000000
};

consteval auto corvid_enum_spec(mmap_prot*) {
  return corvid::enums::bitmask::make_bitmask_enum_spec<mmap_prot,
      "growsup,growsdown,,,,,,,,,,,,,,,,,,,,,,exec,write,read">();
}

#pragma endregion
#pragma region mmap_mask

// `MAP_*` wrapper.
enum class mmap_mask : uint32_t {
  file = MAP_FILE,                       // 0x00
  shared = MAP_SHARED,                   // 0x01
  map_private = MAP_PRIVATE,             // 0x02
  shared_validate = MAP_SHARED_VALIDATE, // 0x03
  mask_type = MAP_TYPE,                  // 0x0f
  map_huge_mask = MAP_HUGE_MASK,         // 0x3f
  fixed = MAP_FIXED,                     // 0x00010
  anonymous = MAP_ANONYMOUS,             // 0x00020
  growsdown = MAP_GROWSDOWN,             // 0x00100
  denywrite = MAP_DENYWRITE,             // 0x00800
  executable = MAP_EXECUTABLE,           // 0x01000
  locked = MAP_LOCKED,                   // 0x02000
  noreserve = MAP_NORESERVE,             // 0x04000
  populate = MAP_POPULATE,               // 0x08000
  nonblock = MAP_NONBLOCK,               // 0x10000
  stack = MAP_STACK,                     // 0x20000
  hugetlb = MAP_HUGETLB,                 // 0x40000
  sync = MAP_SYNC,                       // 0x80000
  fixed_noreplace = MAP_FIXED_NOREPLACE, // 0x100000
};

consteval auto corvid_enum_spec(mmap_mask*) {
  return corvid::enums::bitmask::make_bitmask_enum_spec<mmap_mask,
      "fixed_noreplace,sync,hugetlb,stack,nonblock,populate,noreserve,locked,"
      "executable,denywrite,,,growsdown,,,anonymous,fixed,,,private,shared">();
}

#pragma endregion
#pragma region mmap_advice

// `MADV_*` wrapper.
// NOLINTNEXTLINE(performance-enum-size)
enum class mmap_advice : int32_t {
  normal = MADV_NORMAL,                   // 0
  random = MADV_RANDOM,                   // 1
  sequential = MADV_SEQUENTIAL,           // 2
  willneed = MADV_WILLNEED,               // 3
  dontneed = MADV_DONTNEED,               // 4
  free = MADV_FREE,                       // 8
  remove = MADV_REMOVE,                   // 9
  dontfork = MADV_DONTFORK,               // 10
  dofork = MADV_DOFORK,                   // 11
  mergeable = MADV_MERGEABLE,             // 12
  unmergeable = MADV_UNMERGEABLE,         // 13
  hugepage = MADV_HUGEPAGE,               // 14
  nohugepage = MADV_NOHUGEPAGE,           // 15
  dontdump = MADV_DONTDUMP,               // 16
  dodump = MADV_DODUMP,                   // 17
  wipeonfork = MADV_WIPEONFORK,           // 18
  keeponfork = MADV_KEEPONFORK,           // 19
  cold = MADV_COLD,                       // 20
  pageout = MADV_PAGEOUT,                 // 21
  populate_read = MADV_POPULATE_READ,     // 22
  populate_write = MADV_POPULATE_WRITE,   // 23
  dontneed_locked = MADV_DONTNEED_LOCKED, // 24
  collapse = MADV_COLLAPSE,               // 25
  hwpoison = MADV_HWPOISON,               // 100
};
consteval auto corvid_enum_spec(mmap_advice*) {
  return corvid::enums::sequence::make_sequence_enum_spec<mmap_advice,
      "0,normal,random,sequential,willneed,dontneed,,,,free,remove,dontfork,"
      "dofork,mergeable,unmergeable,hugepage,nohugepage,dontdump,dodump,"
      "wipeonfork,keeponfork,cold,pageout,populate_read,populate_write,"
      "dontneed_locked,collapse|100,hwpoison">();
}

#pragma endregion
#pragma region memory_map

// RAII wrapper around a Linux memory mapping.
//
// `memory_map` owns one region created by `::mmap` and unmaps it when
// destroyed. The factories cover the raw call and the file-backed cases, and
// `advise` wraps `::madvise`. Every call passes its arguments through, so
// the kernel is the sole arbiter: failure is signaled through the return
// value, with the reason left in `errno` for `os_error::last()`.
class [[nodiscard]] memory_map {
public:
#pragma region Construction

  memory_map() noexcept = default;

  // Adopt an existing mapping of `length` bytes at `base`.
  explicit memory_map(void* base, size_t length) noexcept
      : base_{base}, length_{length} {}

  memory_map(const memory_map&) = delete;
  memory_map& operator=(const memory_map&) = delete;

  memory_map(memory_map&& other) noexcept
      : base_{std::exchange(other.base_, nullptr)},
        length_{std::exchange(other.length_, 0)} {}

  memory_map& operator=(memory_map&& other) noexcept {
    if (this != &other) {
      unmap();
      base_ = std::exchange(other.base_, nullptr);
      length_ = std::exchange(other.length_, 0);
    }
    return *this;
  }

  ~memory_map() { unmap(); }

  // Map `length` bytes with `prot` and `flags`.
  //
  // With `mmap_mask::anonymous`, `fd` is ignored; otherwise the mapping is
  // backed by `fd` from `offset`, which must be a multiple of `page_size()`.
  // `hint` is the address hint. On failure, returns an unmapped `memory_map`.
  [[nodiscard]] static memory_map create(size_t length, mmap_prot prot,
      mmap_mask flags, int fd = -1, off_t offset = 0,
      void* hint = nullptr) noexcept {
    // The analyzer sees a test pass a zero length, which the kernel rejects
    // with EINVAL; that is the documented failure path, not a defect.
    // NOLINTNEXTLINE(clang-analyzer-unix.StdCLibraryFunctions)
    auto* base = ::mmap(hint, length, static_cast<int>(prot),
        static_cast<int>(flags), fd, offset);
    if (base == MAP_FAILED) return {};
    return memory_map{base, length};
  }

  // Map `length` bytes of `file` from `offset`, private and read-only by
  // default.
  //
  // A closed `file` fails with `EBADF`: its -1 descriptor is only accepted
  // alongside `mmap_mask::anonymous`.
  [[nodiscard]] static memory_map map(const os_file& file, size_t length,
      off_t offset = 0, mmap_prot prot = mmap_prot::read,
      mmap_mask flags = mmap_mask::map_private) noexcept {
    return create(length, prot, flags, file.handle(), offset);
  }

  // Map the whole of `file`, private and read-only by default.
  //
  // The length comes from `fstat`. An empty file has nothing to map, so it
  // fails like any other zero-length request.
  [[nodiscard]] static memory_map map_all(const os_file& file,
      mmap_prot prot = mmap_prot::read,
      mmap_mask flags = mmap_mask::map_private) noexcept {
    struct stat st{};
    // The analyzer sees a test pass a closed file, which `fstat` rejects
    // with EBADF; that is the documented failure path, not a defect.
    // NOLINTNEXTLINE(clang-analyzer-unix.StdCLibraryFunctions)
    if (::fstat(file.handle(), &st) != 0) return {};
    return map(file, static_cast<size_t>(st.st_size), 0, prot, flags);
  }

  // Open `path` read-only and map the whole file privately.
  //
  // The descriptor is closed before returning; the mapping outlives it.
  [[nodiscard]] static memory_map map_file(cstring_view path) noexcept {
    const os_file file{
        ::open(path.c_str(), *(o_flags::rdonly | o_flags::cloexec))};
    if (!file) return {};
    return map_all(file);
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] bool is_mapped() const noexcept { return base_ != nullptr; }
  [[nodiscard]] explicit operator bool() const noexcept { return is_mapped(); }

  [[nodiscard]] void* data() const noexcept { return base_; }
  [[nodiscard]] size_t size() const noexcept { return length_; }

  // The region as bytes.
  [[nodiscard]] std::span<std::byte> bytes() const noexcept {
    // Parens, not braces: C++26 gives span an initializer_list constructor.
    // NOLINTNEXTLINE(modernize-return-braced-init-list)
    return std::span<std::byte>(static_cast<std::byte*>(base_), length_);
  }

  // The system page size, which file offsets and advice ranges align to.
  [[nodiscard]] static size_t page_size() noexcept {
    return static_cast<size_t>(::sysconf(_SC_PAGESIZE));
  }

#pragma endregion
#pragma region Unmap and release

  // Unmap the region.
  //
  // Idempotent. Returns true when a region was held and is now unmapped,
  // false otherwise. On failure, the object is left unmapped rather than
  // holding a stale region.
  bool unmap() noexcept {
    if (!is_mapped()) return false;
    auto* base = std::exchange(base_, nullptr);
    const auto length = std::exchange(length_, 0);
    return ::munmap(base, length) == 0;
  }

  // Release ownership and return the base address without unmapping.
  [[nodiscard]] void* release() noexcept {
    length_ = 0;
    return std::exchange(base_, nullptr);
  }

#pragma endregion
#pragma region Advice

  // Apply `advice` to the whole region via `madvise`.
  [[nodiscard]] bool advise(mmap_advice advice) const noexcept {
    return advise(0, length_, advice);
  }

  // Apply `advice` to `length` bytes from `offset`, which must be a multiple
  // of `page_size()`.
  [[nodiscard]] bool
  advise(size_t offset, size_t length, mmap_advice advice) const noexcept {
    return ::madvise(static_cast<std::byte*>(base_) + offset, length,
               static_cast<int>(*advice)) == 0;
  }

#pragma endregion
#pragma region Data members
private:
  void* base_{};
  size_t length_{};

#pragma endregion
};

#pragma endregion
}} // namespace corvid::filesys
