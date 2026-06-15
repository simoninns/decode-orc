# ClangFormat.cmake - clang-format integration
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Provides three layers of enforcement:
#
#   1. CMAKE_CXX_CLANG_FORMAT -- CMake's built-in hook runs clang-format
#      in check mode (--dry-run --Werror) for every .cpp source file that is
#      recompiled, so any edited file is checked automatically during the build.
#
#   2. Per-header stamp files (ALL) -- headers are not direct compilation units
#      so CMAKE_CXX_CLANG_FORMAT never sees them.  Each .h/.hpp file gets a
#      stamp file under build/clang-format-stamps/; the stamp is regenerated
#      only when the header changes, giving incremental checks on every build.
#
#   3. clang-format-check target -- checks all .cpp/.h/.hpp files (including
#      headers, which are not compiled directly).  Use this in CI or to get a
#      full-repo report.
#
#   4. clang-format target -- reformats all files in-place.
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
# 1. Per-file check wired into the build dependency graph (.cpp files)
# ---------------------------------------------------------------------------
# Every .cpp file that is recompiled is also checked.  The build fails if the
# file is not properly formatted.
set(CMAKE_CXX_CLANG_FORMAT
    "${CLANG_FORMAT_EXECUTABLE};--style=file;--dry-run;--Werror"
)

# ---------------------------------------------------------------------------
# 2. Incremental header checks wired into the default build (ALL)
# ---------------------------------------------------------------------------
# CMAKE_CXX_CLANG_FORMAT only fires on compiled .cpp units.  Headers need
# stamp files so they are checked incrementally on every build invocation.

file(GLOB_RECURSE _clang_format_headers
    "${CMAKE_SOURCE_DIR}/orc/*.h"
    "${CMAKE_SOURCE_DIR}/orc/*.hpp"
    "${CMAKE_SOURCE_DIR}/orc-tests/*.h"
    "${CMAKE_SOURCE_DIR}/orc-tests/*.hpp"
)

set(_stamp_dir "${CMAKE_BINARY_DIR}/clang-format-stamps")
file(MAKE_DIRECTORY "${_stamp_dir}")

set(_header_stamps)
foreach(_hdr IN LISTS _clang_format_headers)
    file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${_hdr}")
    string(REPLACE "/" "__" _stamp_name "${_rel}")
    set(_stamp "${_stamp_dir}/${_stamp_name}.stamp")
    add_custom_command(
        OUTPUT  "${_stamp}"
        COMMAND ${CLANG_FORMAT_EXECUTABLE} --style=file --dry-run --Werror "${_hdr}"
        COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
        DEPENDS "${_hdr}"
        COMMENT "clang-format: ${_rel}"
        VERBATIM
    )
    list(APPEND _header_stamps "${_stamp}")
endforeach()

add_custom_target(clang-format-headers ALL
    DEPENDS ${_header_stamps}
)

# ---------------------------------------------------------------------------
# 3 & 4. Full-repo targets (covers headers as well as sources)
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

# ---------------------------------------------------------------------------
# 5. CTest registration so the CI gate catches format violations
# ---------------------------------------------------------------------------
add_test(
    NAME ClangFormatCheck
    COMMAND
        ${CLANG_FORMAT_EXECUTABLE}
        --style=file
        --dry-run
        --Werror
        ${_clang_format_sources}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(ClangFormatCheck PROPERTIES LABELS "format")

# ---------------------------------------------------------------------------
# 6. Install the pre-commit hook once at configure time (local builds only)
# ---------------------------------------------------------------------------
# Sets git's hooksPath to .githooks/ so the checked-in hook is used by all
# contributors without any manual setup step.  This only touches .git/config
# and has no effect on sandboxed (Nix derivation) builds.
find_program(GIT_EXECUTABLE git DOC "Path to git executable")
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} config core.hooksPath .githooks
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE _git_hooks_result
        OUTPUT_QUIET ERROR_QUIET
    )
    if(_git_hooks_result EQUAL 0)
        message(STATUS "clang-format: pre-commit hook installed (core.hooksPath = .githooks)")
    endif()
endif()
