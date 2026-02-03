# Top-level CMakeLists.txt for decode-orc
# This allows building from a top-level build/ directory

cmake_minimum_required(VERSION 3.20)
project(decode-orc-root)

# If PROJECT_VERSION_OVERRIDE is provided (for release builds), use it globally
if(DEFINED PROJECT_VERSION_OVERRIDE)
    message(STATUS "Using global version override: ${PROJECT_VERSION_OVERRIDE}")
endif()

# Add external dependencies (submodules)
add_subdirectory(external/encode-orc)

# Add the orc subdirectory which contains the main project
add_subdirectory(orc)
