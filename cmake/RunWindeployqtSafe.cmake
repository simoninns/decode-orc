if(NOT DEFINED WINDEPLOYQT_EXECUTABLE OR NOT DEFINED WINDEPLOYQT_CONFIG OR NOT DEFINED WINDEPLOYQT_TARGET)
    message(FATAL_ERROR "RunWindeployqtSafe.cmake requires WINDEPLOYQT_EXECUTABLE, WINDEPLOYQT_CONFIG, and WINDEPLOYQT_TARGET")
endif()

get_filename_component(_qt_tools_bin "${WINDEPLOYQT_EXECUTABLE}" DIRECTORY)
get_filename_component(_qt_triplet_root "${_qt_tools_bin}/../../.." ABSOLUTE)
set(_qt_release_bin "${_qt_triplet_root}/bin")
set(_qt_debug_bin "${_qt_triplet_root}/debug/bin")

if(EXISTS "${_qt_debug_bin}" OR EXISTS "${_qt_release_bin}")
    set(ENV{PATH} "${_qt_debug_bin};${_qt_release_bin};$ENV{PATH}")
endif()

if(WINDEPLOYQT_CONFIG STREQUAL "debug")
    if(EXISTS "${_qt_debug_bin}" AND EXISTS "${_qt_release_bin}")
        file(GLOB _qt_debug_dlls "${_qt_debug_bin}/*d.dll")
        foreach(_dll IN LISTS _qt_debug_dlls)
            get_filename_component(_dll_name "${_dll}" NAME)
            if(NOT EXISTS "${_qt_release_bin}/${_dll_name}")
                file(COPY "${_dll}" DESTINATION "${_qt_release_bin}")
            endif()
        endforeach()
    endif()
endif()

execute_process(
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
        "--${WINDEPLOYQT_CONFIG}"
        --no-translations
        --no-system-d3d-compiler
        --no-opengl-sw
        "${WINDEPLOYQT_TARGET}"
    RESULT_VARIABLE _windeployqt_rc
    OUTPUT_VARIABLE _windeployqt_stdout
    ERROR_VARIABLE _windeployqt_stderr
)

set(_manual_plugin_fallback_ok FALSE)
if(NOT _windeployqt_rc EQUAL 0)
    string(FIND "${_windeployqt_stderr}" "Unable to find the platform plugin." _platform_plugin_err)

    if(NOT _platform_plugin_err EQUAL -1)
        get_filename_component(_target_dir "${WINDEPLOYQT_TARGET}" DIRECTORY)
        set(_plugin_root "${_qt_release_bin}/../Qt6/plugins")
        if(WINDEPLOYQT_CONFIG STREQUAL "debug" AND EXISTS "${_qt_debug_bin}/../Qt6/plugins")
            set(_plugin_root "${_qt_debug_bin}/../Qt6/plugins")
        endif()

        set(_plugin_types platforms styles imageformats generic)
        foreach(_ptype IN LISTS _plugin_types)
            if(EXISTS "${_plugin_root}/${_ptype}")
                file(MAKE_DIRECTORY "${_target_dir}/${_ptype}")
                file(GLOB _plugin_dlls "${_plugin_root}/${_ptype}/*.dll")
                foreach(_plugin_dll IN LISTS _plugin_dlls)
                    file(COPY "${_plugin_dll}" DESTINATION "${_target_dir}/${_ptype}")
                endforeach()
            endif()
        endforeach()

        file(GLOB _platform_plugins "${_target_dir}/platforms/*qwindows*.dll")
        if(_platform_plugins)
            set(_manual_plugin_fallback_ok TRUE)
        endif()
    endif()

    if(_manual_plugin_fallback_ok)
        message(STATUS
            "windeployqt reported missing platform plugin; applied manual plugin deployment fallback from vcpkg plugin directories."
        )
    else()
        message(WARNING
            "windeployqt failed with exit code ${_windeployqt_rc}. "
            "Continuing build without blocking. Runtime deployment may be incomplete for this configuration.\n"
            "windeployqt stdout:\n${_windeployqt_stdout}\n"
            "windeployqt stderr:\n${_windeployqt_stderr}"
        )
    endif()
endif()
