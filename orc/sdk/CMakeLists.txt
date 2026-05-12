# orc-plugin-sdk: Public stable plugin SDK for decode-orc stage plugins.
#
# This INTERFACE library provides:
#   - The public SDK include directory (orc/plugin/orc_plugin_sdk.h umbrella).
#   - Access to the orc-core and orc-common include paths required to compile
#     against the stable stage API (ParameterizedStage, TriggerableStage, etc.).
#   - The orc-core link target so plugins can link the host runtime library.
#
# In-tree usage (plugins built inside this repository):
#   target_link_libraries(my-plugin PRIVATE orc-plugin-sdk)
#
# Out-of-tree usage (third-party plugins):
#   find_package(decode-orc-plugin-sdk REQUIRED)
#   target_link_libraries(my-plugin PRIVATE orc::plugin-sdk)
#   orc_add_stage_plugin(...)   # helper macro from DecodeOrcPluginSDKHelpers.cmake

add_library(orc-plugin-sdk INTERFACE)

target_include_directories(orc-plugin-sdk INTERFACE
    # SDK headers: <orc/plugin/orc_plugin_sdk.h>, etc.
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/decode-orc-plugin-sdk>
)

# Expose orc/core/stages/ so that <triggerable_stage.h> (used by orc_stage_api.h)
# is resolvable for plugin builds without requiring knowledge of the internal tree.
target_include_directories(orc-plugin-sdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/orc/core/stages>
)

# Plugins must link against orc-core to resolve DAGStage, ArtifactPtr, etc.
# This also transitively exposes orc-common include paths (parameter_types.h,
# node_type.h, common_types.h) and orc-core/include (stage_parameter.h, etc.).
target_link_libraries(orc-plugin-sdk INTERFACE orc-core)

# =============================================================================
# Install rules
# =============================================================================

# SDK public headers
install(DIRECTORY include/
    DESTINATION include/decode-orc-plugin-sdk
    FILES_MATCHING PATTERN "*.h"
)

# CMake package config files (see cmake/decode-orc-plugin-sdk/)
# Note: full CMake package export (find_package support) requires orc-core to
# also be installed and exported. The Config.cmake and Helpers.cmake are
# installed here; the Targets.cmake is generated when orc-core is packaged.
# Full out-of-tree package support is completed in Phase 7.
install(FILES
    ${CMAKE_SOURCE_DIR}/cmake/decode-orc-plugin-sdk/DecodeOrcPluginSDKConfig.cmake
    ${CMAKE_SOURCE_DIR}/cmake/decode-orc-plugin-sdk/DecodeOrcPluginSDKHelpers.cmake
    DESTINATION lib/cmake/decode-orc-plugin-sdk
)

# SDK enforcement gate scripts — installed alongside the CMake package so that
# third-party plugin authors can run the same private-include and private-link
# scans locally without needing access to the host source tree.
install(FILES
    ${CMAKE_SOURCE_DIR}/cmake/check_plugin_private_includes.sh
    ${CMAKE_SOURCE_DIR}/cmake/check_plugin_private_links.sh
    DESTINATION lib/cmake/decode-orc-plugin-sdk
    PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)
