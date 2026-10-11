# CLAUDE.md

The `filesys` module provides RAII wrappers around OS handles, inheriting from `os_file`.

`os_enums`, `os_error`, `os_file`, `os_event`, and `os_mmap_file` are cross-platform: each is a public entry point that selects a per-platform implementation under `details/` (`linux_*` or `windows_*`). For `os_error`, `os_file`, and `os_event`, the shared interface and its documentation live in a CRTP base header (`details/os_*_base.h`) that the implementations include directly, so a `details/` header parses standalone; platform extras are documented on the implementation class. `os_enums` and `os_mmap_file` have no base header: their shared contract is documented in the entry header itself, and `os_mmap_file` is the portable read-only facade over the platform mapping wrappers below. Headers that exist for one platform only carry that platform as a filename prefix: `linux_epoll.h` (`epoll`) and `linux_mmap.h` (`memory_map`, plus the `mmap_*` enums) build directly on the Linux implementations, and `windows_mmap.h` (`file_mapping` and `mapped_view`, plus the `page_protect` and `file_map` enums) on the Windows ones.

A `details/` header included directly (without its entry's `CORVID_*_ENTRY` macro) is tolerated in dev builds and under clangd (`-DCORVID_CLANGD` from the generated `.clangd`), and fails with `#error` in NDEBUG builds.
