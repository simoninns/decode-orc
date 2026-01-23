# orc-common: Shared type definitions accessible to all layers
#
# This library contains shared types and utility functions that are
# used across core, public API, presenters, GUI, and CLI.

add_library(orc-common
    parameter_util.cpp
)

# PUBLIC interface - these directories are visible to all consumers
target_include_directories(orc-common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/orc/common>
)

# Install headers
install(DIRECTORY include/
    DESTINATION include/orc/common
    FILES_MATCHING PATTERN "*.h"
)
