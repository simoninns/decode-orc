include_guard(GLOBAL)

# Load the SDK helpers (provides orc_add_stage_plugin() and ORC_STAGE_PLUGIN_INSTALL_DIR).
# For in-tree builds this is the canonical helper; the installed package ships
# the same file as DecodeOrcPluginSDKHelpers.cmake.
include(${CMAKE_SOURCE_DIR}/cmake/decode-orc-plugin-sdk/DecodeOrcPluginSDKHelpers.cmake)

set(ORC_STAGE_PLUGIN_BUILD_DIR "${CMAKE_BINARY_DIR}/lib/orc-stage-plugins")
