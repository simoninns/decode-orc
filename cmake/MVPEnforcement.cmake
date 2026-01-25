# MVPEnforcement.cmake
# Custom CMake functions to enforce MVP architecture at build time
# SPDX-License-Identifier: GPL-3.0-or-later

# Register a custom build step that validates MVP architecture
function(add_mvp_validation TARGET_NAME)
    add_custom_command(
        TARGET ${TARGET_NAME}
        PRE_BUILD
        COMMAND ${CMAKE_SOURCE_DIR}/check_mvp_interface_leakage.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Validating MVP architecture boundaries..."
        VERBATIM
    )
endfunction()

# Verify that a target does NOT link against core directly
function(verify_no_core_dependency TARGET_NAME)
    get_target_property(LINK_LIBS ${TARGET_NAME} LINK_LIBRARIES)
    if(LINK_LIBS)
        foreach(lib ${LINK_LIBS})
            if(lib STREQUAL "orc-core")
                message(FATAL_ERROR 
                    "MVP Violation: Target '${TARGET_NAME}' directly links orc-core. "
                    "GUI/CLI targets must only link orc-presenters, not orc-core.")
            endif()
        endforeach()
    endif()
endfunction()

# Add compile-time check that a target has the appropriate build flag
function(verify_build_flag TARGET_NAME FLAG_NAME)
    get_target_property(COMPILE_DEFS ${TARGET_NAME} COMPILE_DEFINITIONS)
    if(NOT COMPILE_DEFS)
        message(WARNING 
            "Target '${TARGET_NAME}' missing compile definition '${FLAG_NAME}'. "
            "MVP enforcement may not work correctly.")
    else()
        list(FIND COMPILE_DEFS ${FLAG_NAME} flag_index)
        if(flag_index EQUAL -1)
            message(WARNING 
                "Target '${TARGET_NAME}' missing compile definition '${FLAG_NAME}'. "
                "MVP enforcement may not work correctly.")
        endif()
    endif()
endfunction()
