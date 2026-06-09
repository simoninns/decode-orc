# ClangFormat.cmake - clang-format integration
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Provides two layers of enforcement:
#
#   1. CMAKE_CXX_CLANG_FORMAT -- CMake's built-in hook runs clang-format
#      in check mode (--dry-run --Werror) for every .cpp source file that is
#      recompiled, so any edited file is checked automatically during the build.
#
#   2. clang-format-check target -- checks all .cpp/.h/.hpp files (including
#      headers, which are not compiled directly).  Use this in CI or to get a
#      full-repo report.
#
#   3. clang-format target -- reformats all files in-place.
#
# clang-format is optional; if not found everything is silently skipped so
# that sandboxed (Nix derivation) builds are not affected.

find_program(CLANG_FORMAT_EXECUTABLE
    NAMES
        clang-format
        clang-format-21
        clang-format-20
        clang-format-19
        clang-format-18
    DOC "Path to clang-format executable"
)

if(NOT CLANG_FORMAT_EXECUTABLE)
    message(STATUS "clang-format: not found -- format checks disabled")
    return()
endif()

message(STATUS "clang-format: ${CLANG_FORMAT_EXECUTABLE}")

# ---------------------------------------------------------------------------
# 1. Per-file check wired into the build dependency graph
# ---------------------------------------------------------------------------
# Every .cpp file that is recompiled is also checked.  The build fails if the
# file is not properly formatted.
set(CMAKE_CXX_CLANG_FORMAT
    "${CLANG_FORMAT_EXECUTABLE};--style=file;--dry-run;--Werror"
)

# ---------------------------------------------------------------------------
# 2 & 3. Full-repo targets (covers headers as well as sources)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE _clang_format_sources
    "${CMAKE_SOURCE_DIR}/orc/*.cpp"
    "${CMAKE_SOURCE_DIR}/orc/*.h"
    "${CMAKE_SOURCE_DIR}/orc/*.hpp"
    "${CMAKE_SOURCE_DIR}/orc-tests/*.cpp"
    "${CMAKE_SOURCE_DIR}/orc-tests/*.h"
    "${CMAKE_SOURCE_DIR}/orc-tests/*.hpp"
)

add_custom_target(clang-format-check
    COMMAND
        ${CLANG_FORMAT_EXECUTABLE}
        --style=file
        --dry-run
        --Werror
        ${_clang_format_sources}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "clang-format: checking all source files..."
    VERBATIM
)

add_custom_target(clang-format
    COMMAND
        ${CLANG_FORMAT_EXECUTABLE}
        --style=file
        -i
        ${_clang_format_sources}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "clang-format: reformatting all source files in-place..."
    VERBATIM
)
