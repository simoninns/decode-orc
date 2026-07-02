# Top-level CMakeLists.txt for decode-orc
# This allows building from a top-level build/ directory

cmake_minimum_required(VERSION 3.20)
project(decode-orc-root)

# Use a discoverable bash for script execution (Nix-friendly)
find_program(BASH_EXECUTABLE bash REQUIRED)

# Prefer the mold linker on Linux when available (the Nix dev shell provides
# it); links of the plugin/test shared objects are several times faster than
# with BFD ld. Silently falls back to the default linker when mold is absent
# (Nix package builds, macOS, Windows) or CMake predates CMAKE_LINKER_TYPE.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux"
   AND NOT DEFINED CMAKE_LINKER_TYPE
   AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
    find_program(MOLD_EXECUTABLE mold)
    if(MOLD_EXECUTABLE)
        set(CMAKE_LINKER_TYPE MOLD)
        message(STATUS "Linker: mold (${MOLD_EXECUTABLE})")
    endif()
endif()

# If PROJECT_VERSION_OVERRIDE is provided (for release builds), use it globally
if(DEFINED PROJECT_VERSION_OVERRIDE)
    message(STATUS "Using global version override: ${PROJECT_VERSION_OVERRIDE}")
endif()

# Enable testing so we can register architecture checks with CTest
enable_testing()

# Build options
option(BUILD_UNIT_TESTS "Build unit tests" ON)
option(BUILD_GUI_TESTS "Build GUI unit tests" OFF)
option(BUILD_INTEGRATION_TESTS "Build integration tests" OFF)
option(BUILD_FUNCTIONAL_TESTS "Build functional tests (require built plugins)" OFF)

# ---------------------------------------------------------------------------
# EFM decoder dependencies
# spdlog and fmt must be found before add_subdirectory(orc) so that the
# IMPORTED targets exist when orc/core/stages/efm_sink/CMakeLists.txt calls
# target_link_libraries on them.
# EZPWD_INCLUDE_DIR is supplied by:
#   - Nix builds:        -DEZPWD_INCLUDE_DIR=<store-path> via cmakeFlags
#   - nix develop shell: ENV{EZPWD_INCLUDE_DIR} exported by shellHook
# ---------------------------------------------------------------------------
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

# Resolve the ezpwd include directory (header-only Reed-Solomon library).
# Priority: CMake cache variable > EZPWD_INCLUDE_DIR env > EZPWD_DIR env.
if(NOT EZPWD_INCLUDE_DIR)
    if(DEFINED ENV{EZPWD_INCLUDE_DIR})
        set(EZPWD_INCLUDE_DIR "$ENV{EZPWD_INCLUDE_DIR}")
    elseif(DEFINED ENV{EZPWD_DIR})
        set(EZPWD_INCLUDE_DIR "$ENV{EZPWD_DIR}")
    else()
        message(FATAL_ERROR "EZPWD_INCLUDE_DIR is not set. "
            "Point it at the ezpwd-reed-solomon headers directory. "
            "In the Nix dev shell this is exported automatically by the shellHook.")
    endif()
endif()

# clang-format check (optional; skipped silently when clang-format is absent)
include(cmake/ClangFormat.cmake)

# clang-tidy analysis (optional; skipped silently when clang-tidy is absent)
include(cmake/ClangTidy.cmake)

# Add the orc subdirectory which contains the main project
add_subdirectory(orc)

if(BUILD_UNIT_TESTS OR BUILD_GUI_TESTS OR BUILD_INTEGRATION_TESTS OR BUILD_FUNCTIONAL_TESTS)
    # Add the unified test tree rooted at orc-tests/
    add_subdirectory(orc-tests)
endif()

# MVP architecture validation
# Run via: ctest -R MVPArchitectureCheck  OR  ctest -L mvp
add_test(
    NAME MVPArchitectureCheck
    COMMAND ${BASH_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/check_mvp_architecture.sh
)
set_tests_properties(MVPArchitectureCheck PROPERTIES LABELS "mvp")

# SDK-only enforcement gates: plugin code and build files must not use private host headers or link targets.
# These gates are required and will FAIL the build if violations are detected.
add_test(
    NAME PluginPrivateIncludeScan
    COMMAND ${BASH_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/check_plugin_private_includes.sh ${CMAKE_SOURCE_DIR}
)
set_tests_properties(PluginPrivateIncludeScan PROPERTIES LABELS "sdk")

add_test(
    NAME PluginPrivateLinkScan
    COMMAND ${BASH_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/check_plugin_private_links.sh ${CMAKE_SOURCE_DIR}
)
set_tests_properties(PluginPrivateLinkScan PROPERTIES LABELS "sdk")
