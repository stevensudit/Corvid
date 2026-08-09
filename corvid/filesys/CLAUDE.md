# CLAUDE.md

The `filesys` module provides RAII wrappers around OS handles, inheriting from `os_file`.

`os_enums`, `os_error`, `os_file`, and `os_event` are cross-platform: each is a public entry point that selects a per-platform implementation under `imp/` (`linux_*` or `windows_*`). The entry header carries the portable contract; platform extras are documented on the implementation class. `epoll` and `net_socket` are Linux-only and build directly on the Linux implementations.

An `imp/` header included directly (without its entry's `CORVID_*_ENTRY` macro) reroutes through the entry point in dev builds and under clangd (`-DCORVID_CLANGD` from the generated `.clangd`), and fails with `#error` in NDEBUG builds.
