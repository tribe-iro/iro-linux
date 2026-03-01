cmake_minimum_required(VERSION 3.20)
project(iro_tool LANGUAGES CXX)

# =============================================================================
# IRO Tool Build Configuration
# =============================================================================
#
# REQUIRED COMPILER: Clang 18+
#
# Why Clang (not GCC):
#   • Superior diagnostics — -Weverything catches bugs GCC misses
#   • First-class sanitizers — UBSan/ASan are essential for binary parsing code
#   • Faster builds — rapid iteration during development
#   • Modern C++23 — full support without experimental flags
#
# The code is portable C++23 with no Clang-specific extensions.
# GCC 14+ will work if you must, but Clang is the tested configuration.
# =============================================================================

# Enforce Clang (warn if using something else)
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING
        "IRO tools are designed for Clang 18+. "
        "You're using ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}. "
        "This may work but is not the tested configuration."
    )
endif()

# Check Clang version
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "18.0")
    message(WARNING
        "Clang 18+ recommended. You have Clang ${CMAKE_CXX_COMPILER_VERSION}. "
        "Older versions may lack full C++23 support."
    )
endif()

# C++23 is required per IRO-TOOL-SPEC-4.2 §0.1
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Shared header location
set(IRO_SHARED_HEADERS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/overlay/scripts/iro)

# -----------------------------------------------------------------------------
# Compile Options — Aggressive Warnings
# -----------------------------------------------------------------------------
# These tools parse binary formats and generate code. Bugs here cause silent
# data corruption or security issues. We enable maximum warnings.

set(IRO_WARNINGS_COMMON
    # Standard warnings
    -Wall
    -Wextra
    -Wpedantic

    # Integer safety (critical for binary parsing)
    -Wconversion
    -Wsign-conversion

    # Modern C++ hygiene
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wnull-dereference
)

set(IRO_WARNINGS_CLANG
    # Clang-only diagnostics
    -Wmost
    -Wthread-safety
    -Wshorten-64-to-32
    -Wunused-exception-parameter
    -Wunreachable-code-aggressive
)

function(iro_apply_warnings target)
    target_compile_options(${target} PRIVATE ${IRO_WARNINGS_COMMON})
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target} PRIVATE ${IRO_WARNINGS_CLANG})
    endif()
endfunction()

# Release builds: optimize for speed (these tools are in the build critical path)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -flto=thin")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
endif()

# Debug builds: sanitizers for catching bugs
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -fno-omit-frame-pointer -fsanitize=address,undefined")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=address,undefined")

# -----------------------------------------------------------------------------
# Targets
# -----------------------------------------------------------------------------

# layout_parse - ELF note parser and header emitter
add_executable(layout_parse
    overlay/scripts/iro/layout_parse/layout_parse.cc
    overlay/scripts/iro/layout_parse/dwarf_reader.cc
)
target_include_directories(layout_parse PRIVATE ${IRO_SHARED_HEADERS_DIR})
iro_apply_warnings(layout_parse)

# gen_probe - Probe C translation unit generator
add_executable(gen_probe
    overlay/scripts/iro/gen_probe/gen_probe.cc
)
target_include_directories(gen_probe PRIVATE ${IRO_SHARED_HEADERS_DIR})
iro_apply_warnings(gen_probe)

# depcheck - Header boundary enforcement tool
add_executable(depcheck
    overlay/scripts/iro/depcheck/depcheck.cc
)
target_include_directories(depcheck PRIVATE ${IRO_SHARED_HEADERS_DIR})
iro_apply_warnings(depcheck)

# -----------------------------------------------------------------------------
# Installation
# -----------------------------------------------------------------------------

install(TARGETS layout_parse gen_probe depcheck
    RUNTIME DESTINATION bin
)

# -----------------------------------------------------------------------------
# Testing
# -----------------------------------------------------------------------------

enable_testing()

# Basic smoke tests — tools run without crashing
add_test(NAME layout_parse_help COMMAND layout_parse --help)
add_test(NAME gen_probe_help COMMAND gen_probe --help)
add_test(NAME depcheck_help COMMAND depcheck --help)
set_tests_properties(layout_parse_help gen_probe_help depcheck_help
    PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
add_test(
    NAME version_consistency
    COMMAND ${CMAKE_COMMAND}
        -DIRO_TOOL_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_versions.cmake
)
add_test(
    NAME integration_emit_determinism
    COMMAND ${CMAKE_COMMAND}
        -DIRO_TOOL_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DIRO_TOOL_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration_emit_determinism.cmake
)
add_test(
    NAME integration_dwarf_fixtures
    COMMAND ${CMAKE_COMMAND}
        -DIRO_TOOL_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DIRO_TOOL_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -DIRO_TOOL_C_COMPILER=${CMAKE_C_COMPILER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration_dwarf_fixtures.cmake
)

# SHA-256 verification is implicit — tools verify on startup before any hash operation
