# CLAUDE.md

The `filesys` module provides RAII wrappers around OS handles, inheriting from `os_file`.

`os_enums`, `os_error`, `os_file`, and `os_event` are cross-platform: each is a public entry point that selects a per-platform implementation under `details/` (`linux_*` or `windows_*`). For `os_error`, `os_file`, and `os_event`, the shared interface and its documentation live in a CRTP base header (`os_*_base.h`) that the implementations include directly, so a `details/` header parses standalone; platform extras are documented on the implementation class. `os_enums` has no base header: its shared contract is documented in the entry header itself. `epoll` and `net_socket` are Linux-only and build directly on the Linux implementations.

A `details/` header included directly (without its entry's `CORVID_*_ENTRY` macro) is tolerated in dev builds and under clangd (`-DCORVID_CLANGD` from the generated `.clangd`), and fails with `#error` in NDEBUG builds.
