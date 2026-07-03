# ClangTidy.cmake - clang-tidy integration
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Provides two layers of enforcement:
#
#   1. CMAKE_CXX_CLANG_TIDY -- CMake's built-in hook runs clang-tidy on
#      every .cpp source file that is recompiled, passing the exact compiler
#      flags so clang-tidy sees the same includes/defines.  The .clang-tidy
#      config in orc/ is picked up automatically by directory traversal.
#
#   2. clang-tidy-check target -- runs the full source tree via
#      run-clang-tidy using the compile_commands.json database.  Use this
#      in CI or to get a full-repo report.
#
# clang-tidy is optional; if not found everything is silently skipped so
# that sandboxed (Nix derivation) builds are not affected.

# clang-tidy cannot parse MSVC (cl.exe) command lines when invoked through
# CMAKE_CXX_CLANG_TIDY: /EH and Windows SDK defines are misread, producing
# thousands of false "exceptions disabled" / unknown-type errors that fail
# the build. The Visual Studio generator never ran this hook, so skipping
# MSVC preserves long-standing Windows behaviour (analysis runs on Linux CI).
if(MSVC)
    message(STATUS "clang-tidy: skipped for MSVC toolchain")
    return()
endif()

find_program(CLANG_TIDY_EXECUTABLE
    NAMES
        clang-tidy
        clang-tidy-21
        clang-tidy-20
        clang-tidy-19
        clang-tidy-18
    DOC "Path to clang-tidy executable"
)

if(NOT CLANG_TIDY_EXECUTABLE)
    message(STATUS "clang-tidy: not found -- static analysis disabled")
    return()
endif()

message(STATUS "clang-tidy: ${CLANG_TIDY_EXECUTABLE}")

# Export compile_commands.json so clang-tidy-check and IDEs can consume it.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# 1. Per-file analysis wired into the build dependency graph
# ---------------------------------------------------------------------------
# Every .cpp file that is recompiled is also analysed.  The build fails if
# any warning is emitted (.clang-tidy already sets WarningsAsErrors: '*').
set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXECUTABLE}")

# ---------------------------------------------------------------------------
# 2. Full-repo target (uses compile_commands.json via run-clang-tidy)
# ---------------------------------------------------------------------------
find_program(RUN_CLANG_TIDY_EXECUTABLE
    NAMES
        run-clang-tidy
        run-clang-tidy-21
        run-clang-tidy-20
        run-clang-tidy-19
        run-clang-tidy-18
    DOC "Path to run-clang-tidy script"
)

if(RUN_CLANG_TIDY_EXECUTABLE)
    message(STATUS "run-clang-tidy: ${RUN_CLANG_TIDY_EXECUTABLE}")
    add_custom_target(clang-tidy-check
        COMMAND ${RUN_CLANG_TIDY_EXECUTABLE}
            -clang-tidy-binary ${CLANG_TIDY_EXECUTABLE}
            -p ${CMAKE_BINARY_DIR}
            -header-filter ".*/(orc|orc-tests)/.*"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "clang-tidy: analysing all source files..."
        VERBATIM
    )
else()
    message(STATUS "run-clang-tidy: not found -- clang-tidy-check target disabled")
endif()
