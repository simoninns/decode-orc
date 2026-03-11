# Top-level CMakeLists.txt for decode-orc
# This allows building from a top-level build/ directory

cmake_minimum_required(VERSION 3.20)
project(decode-orc-root)

# Use a discoverable bash for script execution (Nix-friendly)
find_program(BASH_EXECUTABLE bash REQUIRED)

# If PROJECT_VERSION_OVERRIDE is provided (for release builds), use it globally
if(DEFINED PROJECT_VERSION_OVERRIDE)
    message(STATUS "Using global version override: ${PROJECT_VERSION_OVERRIDE}")
endif()

# Enable testing so we can register architecture checks with CTest
enable_testing()

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

# Add the orc subdirectory which contains the main project
add_subdirectory(orc)

if(BUILD_UNIT_TESTS)
    # Add the subdirectory which contains the unit test project(s)
    add_subdirectory(orc-unit-tests)
endif()

# MVP architecture validation (run via: ctest -R MVPArchitectureCheck)
add_test(
    NAME MVPArchitectureCheck
    COMMAND ${BASH_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/check_mvp_architecture.sh
)
