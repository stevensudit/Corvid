## Build And Test Workflow

- Treat `cleanbuild.sh` as the canonical full test entrypoint for this repo.
- The supported out-of-source test build directory is `tests/build`.
- Do not assume `build-tests/` is current or authoritative. It may be a stale local build tree and can produce misleading results.
- Before reporting test failures, prefer one of these workflows:
  - Run `./cleanbuild.sh libstdcpp` from the repo root when you want the same clean rebuild path used in Codex.
  - For fast iteration on a single test or file, use a direct CMake build in `tests/build` and rebuild only the relevant target instead of running `cleanbuild.sh`.
  - If you need a direct CMake build, configure from `tests/` into `tests/build` and run binaries from `tests/build/release_bin/`.
- If you see a mismatch between ad hoc command-line results and the user's `F5` or `cleanbuild.sh` workflow, stop and align with the project workflow before concluding the code is broken.

## Notes

- In this repo, `cleanbuild.sh` chooses `libstdcpp` automatically in Codex environments and runs the built test executables from `tests/build/release_bin/`.
