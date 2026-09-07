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

// Windows-only: a direct wrapper over file mapping objects and their views,
// with no identical Linux counterpart.
#ifndef _WIN32
#error "\"windows_mmap.h\" is Windows-only."
#endif
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "../enums/bitmask_enum.h"
#include "../strings/cstring_view.h"
// <windows.h> comes via "os_enums.h".
#include "os_enums.h"
#include "os_file.h"

namespace corvid { inline namespace filesys {

#pragma region page_protect

// `PAGE_*` and `SEC_*` wrapper for `CreateFileMapping`.
//
// One page protection, optionally combined with section attributes.
enum class page_protect : uint32_t {
  readonly = PAGE_READONLY,                   // 0x00000002
  readwrite = PAGE_READWRITE,                 // 0x00000004
  writecopy = PAGE_WRITECOPY,                 // 0x00000008
  execute_read = PAGE_EXECUTE_READ,           // 0x00000020
  execute_readwrite = PAGE_EXECUTE_READWRITE, // 0x00000040
  execute_writecopy = PAGE_EXECUTE_WRITECOPY, // 0x00000080
  image = SEC_IMAGE,                          // 0x01000000
  reserve = SEC_RESERVE,                      // 0x04000000
  commit = SEC_COMMIT,                        // 0x08000000
  nocache = SEC_NOCACHE,                      // 0x10000000
  image_no_execute = SEC_IMAGE_NO_EXECUTE,    // 0x11000000
  writecombine = SEC_WRITECOMBINE,            // 0x40000000
  large_pages = SEC_LARGE_PAGES,              // 0x80000000
};

consteval auto corvid_enum_spec(page_protect*) {
  return corvid::enums::bitmask::make_bitmask_enum_spec<page_protect,
      "large_pages,writecombine,,nocache,commit,reserve,,image,,,,,,,,,,,,,,,,"
      ",execute_writecopy,execute_readwrite,execute_read,,writecopy,readwrite,"
      "readonly,">();
}

#pragma endregion
#pragma region file_map

// `FILE_MAP_*` wrapper for `MapViewOfFile`.
//
// `FILE_MAP_ALL_ACCESS` is omitted: for mapping a view it is equivalent to
// `write`.
enum class file_map : uint32_t {
  copy = FILE_MAP_COPY,                       // 0x00000001
  write = FILE_MAP_WRITE,                     // 0x00000002
  read = FILE_MAP_READ,                       // 0x00000004
  execute = FILE_MAP_EXECUTE,                 // 0x00000020
  large_pages = FILE_MAP_LARGE_PAGES,         // 0x20000000
  targets_invalid = FILE_MAP_TARGETS_INVALID, // 0x40000000
  reserve = FILE_MAP_RESERVE,                 // 0x80000000
};

consteval auto corvid_enum_spec(file_map*) {
  return corvid::enums::bitmask::make_bitmask_enum_spec<file_map,
      "reserve,targets_invalid,large_pages,,,,,,,,,,,,,,,,,,,,,,,,execute,,,"
      "read,write,copy">();
}

#pragma endregion
#pragma region file_mapping

// RAII wrapper around a Windows file mapping object.
//
// `file_mapping` owns the handle `CreateFileMappingW` returns and closes it
// when destroyed. Views come from `mapped_view`; each holds its own reference
// to the object, so the handle may be closed as soon as the views exist.
//
// The object alone does not hold the file open: a file opened with
// `FILE_FLAG_DELETE_ON_CLOSE` is deleted when its last handle closes even
// while the object exists, and a view mapped afterward no longer sees the
// contents. Only a mapped view holds the file (see `mapped_view`), so map the
// view before closing the `os_file`.
//
// Every call passes its arguments through, so the kernel is the sole
// arbiter: failure is signaled through the return value, with the reason
// left in `GetLastError` for `os_error::last()`.
class [[nodiscard]] file_mapping {
public:
#pragma region Construction

  file_mapping() noexcept = default;

  // Adopt a file mapping object held in an `os_file`.
  explicit file_mapping(os_file&& handle) noexcept
      : handle_{std::move(handle)} {}

  // Create a file mapping object over `file` with `protect`, `max_size`
  // bytes long.
  //
  // A `max_size` of 0 sizes the object to the file, which must then not be
  // empty (`ERROR_FILE_INVALID`). A `max_size` past the end of a file mapped
  // with a writable `protect` extends the file. `file` must have been opened
  // with the access rights `protect` needs. On failure, returns a closed
  // `file_mapping`.
  [[nodiscard]] static file_mapping create(const os_file& file,
      page_protect protect = page_protect::readonly,
      uint64_t max_size = 0) noexcept {
    return file_mapping{os_file{::CreateFileMappingW(file.handle(), nullptr,
        static_cast<DWORD>(*protect), static_cast<DWORD>(max_size >> 32),
        static_cast<DWORD>(max_size), nullptr)}};
  }

  // Create a file mapping object of `size` bytes backed by the paging file
  // rather than a file, read-write by default.
  //
  // Its pages start out zeroed. On failure, returns a closed `file_mapping`.
  [[nodiscard]] static file_mapping create_anonymous(uint64_t size,
      page_protect protect = page_protect::readwrite) noexcept {
    return file_mapping{os_file{::CreateFileMappingW(INVALID_HANDLE_VALUE,
        nullptr, static_cast<DWORD>(*protect), static_cast<DWORD>(size >> 32),
        static_cast<DWORD>(size), nullptr)}};
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] bool is_open() const noexcept { return handle_.is_open(); }
  [[nodiscard]] explicit operator bool() const noexcept { return is_open(); }

  // Return the raw file mapping object handle.
  [[nodiscard]] HANDLE handle() const noexcept { return handle_.handle(); }

#pragma endregion
#pragma region Data members
private:
  os_file handle_;

#pragma endregion
};

#pragma endregion
#pragma region mapped_view

// RAII wrapper around a Windows mapped view of a file mapping object.
//
// `mapped_view` owns one view created by `MapViewOfFileEx` and unmaps it when
// destroyed. The view holds its own reference to the file mapping object,
// which in turn holds the file, so both the `file_mapping` and the `os_file`
// behind a view may be closed as soon as it exists; the file stays open until
// the view is unmapped. The factories cover the raw call and the file-backed
// and paging-file-backed cases; `flush` wraps `FlushViewOfFile`.
//
// Every call passes its arguments through, so the kernel is the sole
// arbiter: failure is signaled through the return value, with the reason
// left in `GetLastError` for `os_error::last()`.
class [[nodiscard]] mapped_view {
public:
#pragma region Construction

  mapped_view() noexcept = default;

  // Adopt an existing view of `length` bytes at `base`.
  explicit mapped_view(void* base, size_t length) noexcept
      : base_{base}, length_{length} {}

  mapped_view(const mapped_view&) = delete;
  mapped_view& operator=(const mapped_view&) = delete;

  mapped_view(mapped_view&& other) noexcept
      : base_{std::exchange(other.base_, nullptr)},
        length_{std::exchange(other.length_, 0)} {}

  mapped_view& operator=(mapped_view&& other) noexcept {
    if (this != &other) {
      unmap();
      base_ = std::exchange(other.base_, nullptr);
      length_ = std::exchange(other.length_, 0);
    }
    return *this;
  }

  ~mapped_view() { unmap(); }

  // Map `length` bytes of `mapping` from `offset` with `access`.
  //
  // `access` must be compatible with the protection `mapping` was created
  // with. `offset`, and `hint` when not null, must be multiples of
  // `allocation_granularity()`, and the bytes must lie within the mapping
  // object. `length` must be nonzero: a zero length would map to the end of
  // the object, at a size this view could not report. On failure, returns an
  // unmapped `mapped_view`.
  [[nodiscard]] static mapped_view create(const file_mapping& mapping,
      file_map access, size_t length, uint64_t offset = 0,
      void* hint = nullptr) noexcept {
    assert(length > 0);
    auto* base = ::MapViewOfFileEx(mapping.handle(),
        static_cast<DWORD>(*access), static_cast<DWORD>(offset >> 32),
        static_cast<DWORD>(offset), length, hint);
    if (!base) return {};
    return mapped_view{base, length};
  }

  // Map `length` bytes of `file` from `offset`, read-only by default.
  //
  // The file mapping object is sized to the file and closed before
  // returning; the view keeps it alive. A closed `file` fails with
  // `ERROR_INVALID_PARAMETER`: its null handle is neither a file nor the
  // paging-file sentinel.
  [[nodiscard]] static mapped_view map(const os_file& file, size_t length,
      uint64_t offset = 0, page_protect protect = page_protect::readonly,
      file_map access = file_map::read) noexcept {
    const auto mapping = file_mapping::create(file, protect);
    if (!mapping) return {};
    return create(mapping, access, length, offset);
  }

  // Map the whole of `file`, read-only by default.
  //
  // The length comes from `GetFileSizeEx`. An empty file has nothing to map,
  // and fails with `ERROR_FILE_INVALID`.
  [[nodiscard]] static mapped_view map_all(const os_file& file,
      page_protect protect = page_protect::readonly,
      file_map access = file_map::read) noexcept {
    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(file.handle(), &size)) return {};
    return map(file, static_cast<size_t>(size.QuadPart), 0, protect, access);
  }

  // Map `length` bytes of zeroed memory backed by the paging file, read-write
  // by default.
  //
  // The file mapping object is closed before returning; the view keeps it
  // alive.
  [[nodiscard]] static mapped_view create_anonymous(size_t length,
      page_protect protect = page_protect::readwrite,
      file_map access = file_map::write) noexcept {
    const auto mapping = file_mapping::create_anonymous(length, protect);
    if (!mapping) return {};
    return create(mapping, access, length);
  }

  // Open `path` read-only and map the whole file.
  //
  // The open shares reads, writes, and deletes, placing no more restriction
  // on other openers than a POSIX open would. The file handle is closed
  // before returning; the view holds the file open until it is unmapped.
  [[nodiscard]] static mapped_view map_file(wcstring_view path) noexcept {
    const os_file file{::CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file) return {};
    return map_all(file);
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] bool is_mapped() const noexcept { return base_; }
  [[nodiscard]] explicit operator bool() const noexcept { return is_mapped(); }

  [[nodiscard]] void* data() const noexcept { return base_; }
  [[nodiscard]] size_t size() const noexcept { return length_; }

  // The view as bytes.
  [[nodiscard]] std::span<std::byte> bytes() const noexcept {
    // Parens, not braces: C++26 gives span an initializer_list constructor.
    // NOLINTNEXTLINE(modernize-return-braced-init-list)
    return std::span<std::byte>(static_cast<std::byte*>(base_), length_);
  }

  // The system page size, the granularity of page protection and commitment.
  [[nodiscard]] static size_t page_size() noexcept {
    SYSTEM_INFO info{};
    ::GetSystemInfo(&info);
    return info.dwPageSize;
  }

  // The system allocation granularity, which view offsets and address hints
  // align to.
  [[nodiscard]] static size_t allocation_granularity() noexcept {
    SYSTEM_INFO info{};
    ::GetSystemInfo(&info);
    return info.dwAllocationGranularity;
  }

#pragma endregion
#pragma region Unmap and release

  // Unmap the view.
  //
  // Idempotent. Returns true when a view was held and is now unmapped, false
  // otherwise. On failure, the object is left unmapped rather than holding a
  // stale view.
  bool unmap() noexcept {
    if (!is_mapped()) return false;
    auto* base = std::exchange(base_, nullptr);
    length_ = 0;
    return ::UnmapViewOfFile(base);
  }

  // Release ownership and return the base address without unmapping.
  [[nodiscard]] void* release() noexcept {
    length_ = 0;
    return std::exchange(base_, nullptr);
  }

#pragma endregion
#pragma region Flush

  // Start writing the whole view's dirty pages back via `FlushViewOfFile`.
  [[nodiscard]] bool flush() const noexcept { return flush(0, length_); }

  // Start writing the dirty pages among `length` bytes from `offset` back
  // via `FlushViewOfFile`.
  //
  // Returns once the write-back is initiated, not once it reaches the disk;
  // `FlushFileBuffers` on the file is the durable follow-up.
  [[nodiscard]] bool flush(size_t offset, size_t length) const noexcept {
    return ::FlushViewOfFile(static_cast<std::byte*>(base_) + offset, length);
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
