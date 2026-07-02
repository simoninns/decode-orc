# DecodeOrcPluginSDKHelpers.cmake
#
# Helper macros for building decode-orc stage plugins, both inside this
# repository and in external (third-party) plugin projects.
#
# Provides:
#   orc_add_stage_plugin()   — create a properly configured MODULE target

include_guard(GLOBAL)
include(CMakeParseArguments)

# Platform-specific plugin installation directory.
# These paths are used both by the in-tree orc_add_stage_plugin() (via
# StagePlugins.cmake) and by the installed helper for external plugin authors.
if(NOT DEFINED ORC_STAGE_PLUGIN_INSTALL_DIR)
    if(APPLE)
        set(ORC_STAGE_PLUGIN_INSTALL_DIR "orc-gui.app/Contents/PlugIns/orc-stage-plugins")
    elseif(WIN32)
        set(ORC_STAGE_PLUGIN_INSTALL_DIR "bin/orc-stage-plugins")
    else()
        set(ORC_STAGE_PLUGIN_INSTALL_DIR "lib/orc-stage-plugins")
    endif()
endif()

# ---------------------------------------------------------------------------
# orc_add_stage_plugin(<target>
#     OUTPUT_NAME    <name>           # Plugin shared library output name
#     PLUGIN_VERSION <version>        # Semantic version string ("1.0.0")
#     SOURCES        <file> ...       # Source files for this plugin
#     [LINK_LIBRARIES <lib> ...]      # Additional link dependencies
#     [RUNTIME_DEPENDENCIES <path>...])
# ---------------------------------------------------------------------------
#
# Creates a SHARED plugin library target configured for decode-orc stage
# plugin conventions (SHARED rather than MODULE so in-tree test executables
# can link stage classes directly; the host loads plugins via dlopen):
#
#   - Links to orc::plugin-sdk (or orc-plugin-sdk for in-tree builds) which
#     provides all SDK include paths and orc-core.
#   - Sets ORC_STAGE_PLUGIN_VERSION compile definition.
#   - Places the output in the standard plugin directory.
#   - Installs to the platform-appropriate plugin location.
#   - Registers the target as a dependency of the orc-stage-plugins meta-target.
#
# USAGE (external plugin project, after find_package(decode-orc-plugin-sdk)):
#
#   orc_add_stage_plugin(
#       my-orc-stage-my-filter
#       OUTPUT_NAME    my-orc-stage-my-filter
#       PLUGIN_VERSION "1.2.3"
#       SOURCES        plugin.cpp my_filter_stage.cpp
#   )
#
function(orc_add_stage_plugin target)
    set(options)
    set(oneValueArgs OUTPUT_NAME PLUGIN_VERSION)
    set(multiValueArgs SOURCES LINK_LIBRARIES RUNTIME_DEPENDENCIES)
    cmake_parse_arguments(ORCSP "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ORCSP_SOURCES)
        message(FATAL_ERROR "orc_add_stage_plugin(${target}) requires SOURCES")
    endif()

    # SHARED (not MODULE) is deliberate: plugins are dlopen'd by the host at
    # runtime, but the in-tree unit/functional test executables also link
    # stage plugin targets directly to exercise stage classes in-process
    # (see orc-tests/core/unit/CMakeLists.txt). MODULE targets cannot be
    # linked, so SHARED is required for that dual use.
    add_library(${target} SHARED ${ORCSP_SOURCES})

    if(ORCSP_OUTPUT_NAME)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${ORCSP_OUTPUT_NAME}")
    endif()

    # Prefer orc::plugin-sdk (installed package target namespace) if available;
    # fall back to orc-plugin-sdk (in-tree target name).
    if(TARGET orc::plugin-sdk)
        set(_orc_sdk_target orc::plugin-sdk)
    elseif(TARGET orc-plugin-sdk)
        set(_orc_sdk_target orc-plugin-sdk)
    else()
        message(FATAL_ERROR
            "orc_add_stage_plugin: Neither orc::plugin-sdk nor orc-plugin-sdk is defined. "
            "Did you call find_package(decode-orc-plugin-sdk REQUIRED) or "
            "add_subdirectory(orc/sdk)?")
    endif()

    target_link_libraries(${target} PRIVATE ${_orc_sdk_target} ${ORCSP_LINK_LIBRARIES})

    # ORC_STAGE_INSTRUCTIONS_MD uses dladdr() on Linux to locate the plugin .so.
    if(UNIX AND NOT APPLE)
        target_link_libraries(${target} PRIVATE dl)
    endif()

    # Always expose the plugin directory itself for local stage headers.
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    if(ORCSP_PLUGIN_VERSION)
        target_compile_definitions(${target} PRIVATE
            ORC_STAGE_PLUGIN_VERSION="${ORCSP_PLUGIN_VERSION}")
    endif()

    # Output directory during builds (so the host can discover plugins at runtime)
    if(NOT DEFINED ORC_STAGE_PLUGIN_BUILD_DIR)
        set(ORC_STAGE_PLUGIN_BUILD_DIR "${CMAKE_BINARY_DIR}/lib/orc-stage-plugins")
    endif()

    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY  "${ORC_STAGE_PLUGIN_BUILD_DIR}"
        RUNTIME_OUTPUT_DIRECTORY  "${ORC_STAGE_PLUGIN_BUILD_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/lib"
    )

    # RPATH for bundled deployment
    if(APPLE)
        set_target_properties(${target} PROPERTIES
            INSTALL_RPATH "@loader_path/../../Frameworks")
    elseif(UNIX AND NOT WIN32)
        set_target_properties(${target} PROPERTIES
            INSTALL_RPATH "\$ORIGIN/..")
    endif()

    install(TARGETS ${target}
        LIBRARY DESTINATION "${ORC_STAGE_PLUGIN_INSTALL_DIR}" COMPONENT Runtime
        RUNTIME DESTINATION "${ORC_STAGE_PLUGIN_INSTALL_DIR}" COMPONENT Runtime
        BUNDLE  DESTINATION "${ORC_STAGE_PLUGIN_INSTALL_DIR}" COMPONENT Runtime
    )

    # If instructions.md exists, copy it alongside the plugin .so at build time
    # and install it so ORC_STAGE_INSTRUCTIONS_MD can find it at runtime.
    # Uses add_custom_command(OUTPUT/DEPENDS) so a change to instructions.md
    # triggers the copy even when the stage .so does not need to be rebuilt.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/instructions.md")
        if(ORCSP_OUTPUT_NAME)
            set(_orc_md_base "${ORCSP_OUTPUT_NAME}")
        else()
            set(_orc_md_base "${target}")
        endif()
        if(WIN32)
            set(_orc_md_filename "${_orc_md_base}.md")
        else()
            set(_orc_md_filename "lib${_orc_md_base}.md")
        endif()

        set(_orc_md_src "${CMAKE_CURRENT_SOURCE_DIR}/instructions.md")
        set(_orc_md_dst "${ORC_STAGE_PLUGIN_BUILD_DIR}/${_orc_md_filename}")

        add_custom_command(
            OUTPUT "${_orc_md_dst}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_orc_md_src}" "${_orc_md_dst}"
            DEPENDS "${_orc_md_src}"
            COMMENT "Copying instructions.md for ${target}"
            VERBATIM
        )
        add_custom_target(${target}-instructions ALL DEPENDS "${_orc_md_dst}")

        install(FILES "${_orc_md_src}"
            RENAME "${_orc_md_filename}"
            DESTINATION "${ORC_STAGE_PLUGIN_INSTALL_DIR}"
            COMPONENT Runtime
        )
    endif()

    if(ORCSP_RUNTIME_DEPENDENCIES)
        foreach(_dep IN LISTS ORCSP_RUNTIME_DEPENDENCIES)
            if(EXISTS "${_dep}")
                install(FILES "${_dep}"
                    DESTINATION "${ORC_STAGE_PLUGIN_INSTALL_DIR}"
                    COMPONENT Runtime
                )
            else()
                message(WARNING "Stage plugin runtime dependency not found: ${_dep}")
            endif()
        endforeach()
    endif()

    # Register this plugin with the orc-stage-plugins meta-target so that
    # building orc-gui or orc-cli builds all plugins automatically.
    if(NOT TARGET orc-stage-plugins)
        add_custom_target(orc-stage-plugins)
    endif()
    add_dependencies(orc-stage-plugins ${target})
endfunction()
