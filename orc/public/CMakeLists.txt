# Public API Module - MVP Architecture
# Facade providing clean C interface to core functionality
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(orc-public STATIC
    impl/public_api_impl.cpp
)

# Public API headers are the only interface exposed
target_include_directories(orc-public PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Implementation can see core internals
target_include_directories(orc-public PRIVATE
    ${CMAKE_SOURCE_DIR}/orc/core/include
    ${CMAKE_SOURCE_DIR}/orc/common/include
)

# Link against core (private - not exposed to users)
target_link_libraries(orc-public PRIVATE orc-core)
target_link_libraries(orc-public PUBLIC orc-common)

# Ensure C++17 standard
target_compile_features(orc-public PUBLIC cxx_std_17)

# Install public headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION include/orc/public
    FILES_MATCHING PATTERN "*.h"
    PATTERN "impl" EXCLUDE
)
