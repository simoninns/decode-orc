# orc-common: Shared type definitions accessible to all layers
#
# This is an INTERFACE library containing header-only types that are
# used across core, public API, presenters, GUI, and CLI.
#
# INTERFACE libraries don't compile into a binary - they just provide
# include paths and usage requirements to targets that link against them.

add_library(orc-common INTERFACE)

# PUBLIC interface - these directories are visible to all consumers
target_include_directories(orc-common INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/orc/common>
)

# Install headers
install(DIRECTORY include/
    DESTINATION include/orc/common
    FILES_MATCHING PATTERN "*.h"
)
