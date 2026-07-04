file(GLOB_RECURSE STAGE_PLUGIN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

# SoXR is required for the free-running 44100 Hz resample path
# (free_running_resampler.cpp). Same detection order as tbc_source.
set(_SOXR_LINK_TARGET "")
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SOXR IMPORTED_TARGET soxr)
    if(SOXR_FOUND)
        set(_SOXR_LINK_TARGET PkgConfig::SOXR)
    endif()
endif()
if(NOT _SOXR_LINK_TARGET)
    find_package(soxr CONFIG QUIET)
    if(soxr_FOUND OR TARGET soxr)
        set(_SOXR_LINK_TARGET soxr)
    endif()
endif()
if(NOT _SOXR_LINK_TARGET)
    # Fallback: try SoXR with capitalised name (some distro packages).
    # Target name is suffixed because tbc_source declares its own fallback.
    find_library(SOXR_LIBRARY NAMES soxr)
    find_path(SOXR_INCLUDE_DIR NAMES soxr.h)
    if(SOXR_LIBRARY AND SOXR_INCLUDE_DIR)
        add_library(soxr_fallback_audio_sink INTERFACE)
        target_link_libraries(soxr_fallback_audio_sink INTERFACE "${SOXR_LIBRARY}")
        target_include_directories(soxr_fallback_audio_sink INTERFACE "${SOXR_INCLUDE_DIR}")
        set(_SOXR_LINK_TARGET soxr_fallback_audio_sink)
    endif()
endif()
if(NOT _SOXR_LINK_TARGET)
    message(FATAL_ERROR
        "orc-stage-plugin-audio-sink: SoXR library not found. "
        "Install libsoxr-dev (Debian/Ubuntu), soxr (Homebrew/vcpkg), "
        "or provide SOXR_LIBRARY/SOXR_INCLUDE_DIR manually.")
endif()

orc_add_stage_plugin(
    orc-stage-plugin-audio-sink
    OUTPUT_NAME orc-stage-plugin-audio-sink
    PLUGIN_VERSION "${ORC_VERSION}"
    SOURCES ${STAGE_PLUGIN_SOURCES}
    LINK_LIBRARIES ${_SOXR_LINK_TARGET}
)
