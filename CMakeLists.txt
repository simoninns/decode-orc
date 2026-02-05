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

# Add external dependencies (submodules)
option(BUILD_ENCODE_ORC "Build encode-orc submodule" OFF)
if(BUILD_ENCODE_ORC)
    add_subdirectory(external/encode-orc)
endif()

# Add the orc subdirectory which contains the main project
add_subdirectory(orc)

# MVP architecture validation (run via: ctest -R MVPArchitectureCheck)
add_test(
    NAME MVPArchitectureCheck
    COMMAND ${BASH_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check_mvp_architecture.sh
)
