# DecodeOrcPluginSDKConfig.cmake
#
# CMake package configuration for the decode-orc Plugin SDK.
#
# After find_package(decode-orc-plugin-sdk REQUIRED), the following are
# available:
#
#   orc::plugin-sdk   — INTERFACE imported target. Link your plugin against
#                       this target to get all required include directories
#                       and the orc-core link dependency.
#
#   orc_add_stage_plugin()  — CMake helper macro (from DecodeOrcPluginSDKHelpers.cmake)
#                             for creating properly configured plugin targets.
#
# Example (external plugin CMakeLists.txt):
#
#   cmake_minimum_required(VERSION 3.20)
#   project(my-orc-plugin CXX)
#
#   find_package(decode-orc-plugin-sdk REQUIRED)
#
#   orc_add_stage_plugin(
#       my-stage-plugin
#       OUTPUT_NAME  my-stage-plugin
#       PLUGIN_VERSION "1.0.0"
#       SOURCES plugin.cpp my_stage.cpp
#   )

cmake_minimum_required(VERSION 3.20)

include(CMakeFindDependencyMacro)

# spdlog is required by orc-common (transitively required by orc-core)
find_dependency(spdlog REQUIRED)

# Import the orc::plugin-sdk target
include("${CMAKE_CURRENT_LIST_DIR}/decode-orc-plugin-sdkTargets.cmake")

# Load the orc_add_stage_plugin() helper macro
include("${CMAKE_CURRENT_LIST_DIR}/DecodeOrcPluginSDKHelpers.cmake")
