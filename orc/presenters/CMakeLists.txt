# Presenters Module - MVP Architecture Layer
# Provides clean interface between GUI/CLI and Core
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(orc-presenters STATIC
    src/project_presenter.cpp
    src/render_presenter.cpp
    src/analysis_presenter.cpp
    src/hints_presenter.cpp
    src/dropout_presenter.cpp
    src/metrics_presenter.cpp
    src/vbi_presenter.cpp
)

# Presenters can see both public API and core internals
# This is the bridge layer
target_include_directories(orc-presenters
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/orc/presenters>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/orc/core/include
        ${CMAKE_SOURCE_DIR}/orc/public
)

# Link against both public API and core
target_link_libraries(orc-presenters
    PUBLIC
        orc-common  # Public types available to GUI
    PRIVATE
        orc-core    # Core implementation (private)
)

# Ensure C++17 standard
target_compile_features(orc-presenters PUBLIC cxx_std_17)

# Install presenter headers for GUI/CLI use
install(DIRECTORY include/
    DESTINATION include/orc/presenters
    FILES_MATCHING PATTERN "*.h"
)
