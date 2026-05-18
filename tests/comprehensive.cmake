# Comprehensive multi-config sweep, driven by CTest scripting.
#
#   ctest -V -S tests/comprehensive.cmake
#
# Configures, builds, and tests every instrumentation mode we care about:
# plain libc++, clang-tidy (build-time analysis only), ASAN+UBSAN, TSAN, and
# MSAN. Each config gets its own build dir under tests/build/comprehensive/.
# The script aggregates pass/fail across configs and exits non-zero if any
# config failed configure, build, or test.

cmake_minimum_required(VERSION 3.18)

set(CTEST_SOURCE_DIRECTORY "${CTEST_SCRIPT_DIRECTORY}")
set(_project_root "${CTEST_SOURCE_DIRECTORY}/..")
set(CTEST_PROJECT_NAME "Corvid")
set(CTEST_CMAKE_GENERATOR "Ninja")
set(CTEST_USE_LAUNCHERS 0)

# Default cap of 50 truncates Build.xml and the "N or more" report counter.
# Raise it so tidy's hundreds of warnings are all captured and counted.
set(CTEST_CUSTOM_MAXIMUM_NUMBER_OF_WARNINGS 100000)
set(CTEST_CUSTOM_MAXIMUM_NUMBER_OF_ERRORS   100000)

cmake_host_system_information(RESULT _ncores QUERY NUMBER_OF_LOGICAL_CORES)
if(NOT _ncores OR _ncores LESS 1)
    set(_ncores 1)
endif()

# All libcxx configs share these compilers. MSAN additionally consumes the
# locally-built MSAN-instrumented libc++ at .local/llvm-msan/, wired up by
# tests/CMakeLists.txt when SANITIZER=msan.
set(ENV{CC}  "/usr/bin/clang-22")
set(ENV{CXX} "/usr/bin/clang++-22")

set(_failures "")

function(_run_config _name)
    set(_extra_opts ${ARGN})
    message("")
    message("===== comprehensive: ${_name} =====")

    set(CTEST_BINARY_DIRECTORY
        "${CTEST_SOURCE_DIRECTORY}/build/comprehensive/${_name}")
    set(CTEST_BUILD_NAME "${_name}")

    file(REMOVE_RECURSE "${CTEST_BINARY_DIRECTORY}")
    file(MAKE_DIRECTORY "${CTEST_BINARY_DIRECTORY}")

    # MSAN-specific: the ignorelist contents affect codegen but aren't part
    # of ccache's default hash (only the path is). CCACHE_EXTRAFILES folds
    # the file contents into the hash so editing the ignorelist invalidates
    # stale .o files. Mirrors cleanbuild.sh.
    if(_name STREQUAL "msan")
        set(ENV{CCACHE_EXTRAFILES}
            "${_project_root}/scripts/msan-libcxx-ignorelist.txt")
    else()
        unset(ENV{CCACHE_EXTRAFILES})
    endif()

    ctest_start("Experimental" TRACK "${_name}")

    ctest_configure(
        BUILD "${CTEST_BINARY_DIRECTORY}"
        SOURCE "${CTEST_SOURCE_DIRECTORY}"
        OPTIONS "${_extra_opts}"
        RETURN_VALUE _cfg_rv
    )
    if(NOT _cfg_rv EQUAL 0)
        message(WARNING "[${_name}] configure failed (rv=${_cfg_rv})")
        set(_failures "${_failures};${_name}:configure" PARENT_SCOPE)
        return()
    endif()

    ctest_build(
        BUILD "${CTEST_BINARY_DIRECTORY}"
        RETURN_VALUE _bld_rv
    )

    # Pull just the warning/error headline lines out of LastBuild_<tag>.log
    # into a flat warnings.txt next to the build dir. Clang/tidy format is
    # "<file>:<line>:<col>: warning: <msg> [<check>]"; gcc/clang errors
    # have the same shape. We skip `note:` lines (macro-expansion context)
    # to keep the punch list scannable; full context stays in the raw log.
    # We use the file line counts (not ctest_build's NUMBER_WARNINGS) so
    # the displayed counts match what the file actually contains -- ctest
    # also matches `note:` lines, which inflates the count ~7x for tidy.
    file(STRINGS "${CTEST_BINARY_DIRECTORY}/Testing/TAG" _tag_lines LIMIT_COUNT 1)
    list(GET _tag_lines 0 _tag)
    set(_raw_log
        "${CTEST_BINARY_DIRECTORY}/Testing/Temporary/LastBuild_${_tag}.log")
    set(_warn_log "${CTEST_BINARY_DIRECTORY}/warnings.txt")
    set(_n_warnings 0)
    set(_n_errors 0)
    if(EXISTS "${_raw_log}")
        execute_process(
            COMMAND grep -E ":[0-9]+:[0-9]+: (warning|error):" "${_raw_log}"
            OUTPUT_FILE "${_warn_log}"
            RESULT_VARIABLE _grep_rv
        )
        # grep exits 1 when there are no matches; treat as empty file.
        if(EXISTS "${_warn_log}")
            execute_process(
                COMMAND grep -cE ":[0-9]+:[0-9]+: warning:" "${_warn_log}"
                OUTPUT_VARIABLE _n_warnings
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            execute_process(
                COMMAND grep -cE ":[0-9]+:[0-9]+: error:" "${_warn_log}"
                OUTPUT_VARIABLE _n_errors
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        endif()
    endif()

    set_property(GLOBAL APPEND PROPERTY _CR_WARN_REPORT
        "${_name}|${_n_warnings}|${_n_errors}|${_warn_log}")

    if(NOT _bld_rv EQUAL 0)
        message(WARNING "[${_name}] build failed (rv=${_bld_rv})")
        set(_failures "${_failures};${_name}:build" PARENT_SCOPE)
        return()
    endif()

    # Tidy is a build-time analyzer; the test binaries themselves are
    # the same as a plain build, so re-running them adds no signal.
    if(_name STREQUAL "tidy")
        return()
    endif()

    ctest_test(
        BUILD "${CTEST_BINARY_DIRECTORY}"
        PARALLEL_LEVEL ${_ncores}
        RETURN_VALUE _test_rv
    )
    if(NOT _test_rv EQUAL 0)
        message(WARNING "[${_name}] tests failed (rv=${_test_rv})")
        set(_failures "${_failures};${_name}:test" PARENT_SCOPE)
        return()
    endif()
endfunction()

_run_config(plain)
_run_config(asan "-DSANITIZER=asan")
_run_config(tsan "-DSANITIZER=tsan")

# MSAN needs the instrumented libc++ at .local/llvm-msan/. Skip with a clear
# diagnostic rather than aborting if it hasn't been built yet.
if(EXISTS "${_project_root}/tests/.local/llvm-msan/lib/libc++.a")
    _run_config(msan "-DSANITIZER=msan")
else()
    message("")
    message("===== comprehensive: msan SKIPPED =====")
    message("MSAN libc++ not found at tests/.local/llvm-msan/. "
            "Run scripts/build_msan_libcxx.sh (one-time, ~10 min) to enable.")
    set(_failures "${_failures};msan:skipped")
endif()

# Tidy is build-time analysis only and the slowest config; run it last so
# pass/fail from the sanitizer configs surfaces sooner during live runs.
_run_config(tidy "-DUSE_CLANG_TIDY=ON")

message("")
message("===== comprehensive: summary =====")

get_property(_reports GLOBAL PROPERTY _CR_WARN_REPORT)
if(_reports)
    message("Build diagnostics per config:")
    foreach(_r IN LISTS _reports)
        # entry: name|warnings|errors|warnings_path
        string(REPLACE "|" ";" _parts "${_r}")
        list(GET _parts 0 _r_name)
        list(GET _parts 1 _r_warn)
        list(GET _parts 2 _r_err)
        list(GET _parts 3 _r_path)
        if(_r_warn GREATER 0 OR _r_err GREATER 0)
            message("  ${_r_name}: ${_r_warn} warnings, ${_r_err} errors")
            message("    -> ${_r_path}")
        else()
            message("  ${_r_name}: clean")
        endif()
    endforeach()
    message("")
endif()

if(_failures STREQUAL "")
    message("All configs passed.")
else()
    foreach(_f IN LISTS _failures)
        if(NOT _f STREQUAL "")
            message("  FAIL ${_f}")
        endif()
    endforeach()
    message(FATAL_ERROR "comprehensive sweep had failures")
endif()
