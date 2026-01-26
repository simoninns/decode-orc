# View Types Module - MVP Architecture
# Header-only library providing shared data structures for the presentation layer.
# These types define the contract between core and GUI/CLI, keeping them decoupled.
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(orc-view-types INTERFACE)

# View type headers are exposed to presenters and GUI
target_include_directories(orc-view-types INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link against common types
target_link_libraries(orc-view-types INTERFACE orc-common)

# Install public headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION include/orc/public
    FILES_MATCHING PATTERN "*.h"
    PATTERN "impl" EXCLUDE
)
