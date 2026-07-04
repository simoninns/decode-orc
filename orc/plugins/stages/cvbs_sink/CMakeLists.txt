# CVBS Sink Stage Plugin
#
# Writes CVBS file-format family output (.composite or .y/.c payload, .meta
# sidecar, and dropout/audio/EFM/AC3 extension sidecars).
#
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 decode-orc contributors

file(GLOB_RECURSE STAGE_PLUGIN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

orc_add_stage_plugin(
    orc-stage-plugin-cvbs-sink
    OUTPUT_NAME orc-stage-plugin-cvbs-sink
    PLUGIN_VERSION "${ORC_VERSION}"
    SOURCES ${STAGE_PLUGIN_SOURCES}
    # SQLite is used by cvbs_sink_stage_deps.cpp for the .meta and extension
    # sidecars; declared here because the SDK does not propagate host
    # third-party dependencies to plugins.
    LINK_LIBRARIES SQLite::SQLite3
)
