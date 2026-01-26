# orc-common: Shared type definitions accessible to all layers
#
# This library contains shared types and utility functions that are
# used across core, public API, presenters, GUI, and CLI.

add_library(orc-common
    parameter_util.cpp
    node_id.cpp
    logging.cpp
    crash_handler.cpp
)

# PUBLIC interface - these directories are visible to all consumers
target_include_directories(orc-common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/orc/common>
)

# Find and link spdlog (which includes fmt)
# Note: spdlog bundles fmt, so we get both libraries
find_package(spdlog REQUIRED)
target_link_libraries(orc-common PUBLIC spdlog::spdlog)

# Install headers
install(DIRECTORY include/
    DESTINATION include/orc/common
    FILES_MATCHING PATTERN "*.h"
)
