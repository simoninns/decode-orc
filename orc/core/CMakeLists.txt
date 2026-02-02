add_library(orc-core STATIC
    # Phase 1: Foundations
    field_id.cpp
    video_field_representation.cpp
    artifact.cpp
    dag_executor.cpp
    dag_field_renderer.cpp
    preview_renderer.cpp
    preview_helpers.cpp
    
    # Observation system
    observation_context.cpp
    observer_config.cpp
    pipeline_validator.cpp
    
    # Logging
    logging.cpp
    
    # Crash handler
    crash_handler.cpp
    
    # TBC file I/O
    tbc_reader.cpp
    tbc_metadata.cpp
    tbc_source_internal/tbc_audio_efm_handler.cpp
    tbc_video_field_representation.cpp
    tbc_yc_video_field_representation.cpp
    
    # VBI utilities
    vbi_utilities.cpp
    vbi_decoder.cpp
    
    # EIA-608 decoder for closed captions
    eia608_decoder.cpp
    
    # Observers (organized in subdirectory)
    observers/observer.cpp
    observers/biphase_observer.cpp
    observers/closed_caption_observer.cpp
    observers/fm_code_observer.cpp
    observers/white_flag_observer.cpp
    observers/burst_level_observer.cpp
    observers/white_snr_observer.cpp
    observers/black_psnr_observer.cpp
    observers/field_quality_observer.cpp
    
    # Analysis framework
    analysis/analysis_registry.cpp
    analysis/analysis_init.cpp
    
    # Vectorscope analysis
    analysis/vectorscope/vectorscope_analysis.cpp
    
    # Field mapping analysis
    # Re-enable range-based field mapping tools (observer-independent)
    analysis/disc_mapper/disc_mapper_analysis.cpp
    analysis/disc_mapper/disc_mapper_analyzer.cpp
    
    # Field corruption analysis (testing tool)
    analysis/field_corruption/field_corruption_analyzer.cpp
    analysis/field_corruption/field_corruption_analysis.cpp
    
    # Dropout editor tool
    analysis/dropout/dropout_editor_tool.cpp
    
    # Source alignment analysis
    analysis/source_alignment/source_alignment_analysis.cpp
    
    # Mask line configuration
    analysis/mask_line/mask_line_analysis.cpp
    
    # FFmpeg preset configuration
    analysis/ffmpeg_preset/ffmpeg_preset_analysis.cpp
    
    # Stages (organized in subdirectories)
    stages/pal_comp_source/pal_comp_source_stage.cpp
    stages/ntsc_comp_source/ntsc_comp_source_stage.cpp
    stages/pal_yc_source/pal_yc_source_stage.cpp
    stages/ntsc_yc_source/ntsc_yc_source_stage.cpp
    stages/ld_sink/ld_sink_stage.cpp
    stages/ld_sink/tbc_metadata_writer.cpp
    stages/hackdac_sink/hackdac_sink_stage.cpp
    stages/dropout_correct/dropout_correct_stage.cpp
    stages/dropout_map/dropout_map_stage.cpp
    stages/field_invert/field_invert_stage.cpp
    stages/field_map/field_map_stage.cpp
    stages/mask_line/mask_line_stage.cpp
    stages/source_align/source_align_stage.cpp
    stages/stacker/stacker_stage.cpp
    stages/chroma_sink/chroma_sink_stage.cpp
    stages/chroma_sink/raw_video_sink_stage.cpp
    stages/chroma_sink/ffmpeg_video_sink_stage.cpp
    stages/chroma_sink/output_backend.cpp
    stages/chroma_sink/raw_output_backend.cpp
    stages/chroma_sink/ffmpeg_output_backend.cpp
    stages/audio_sink/audio_sink_stage.cpp
    stages/efm_sink/efm_sink_stage.cpp
    stages/cc_sink/cc_sink_stage.cpp
    stages/video_params/video_params_stage.cpp
    stages/dropout_analysis_sink/dropout_analysis_sink_stage.cpp
    stages/snr_analysis_sink/snr_analysis_sink_stage.cpp
    stages/burst_level_analysis_sink/burst_level_analysis_sink_stage.cpp
    
    # Dropout decision utilities
    dropout_decision.cpp
    
    # Observation cache (shared by all analysis decoders)
    observation_cache.cpp
    
    # Node type system
    node_type.cpp
    
    # Stage parameters
    stage_parameter.cpp
    
    # Stage registration force-link
    stage_init.cpp
    
    # DAG serialization
    dag_serialization.cpp
    
    # Project file I/O
    project.cpp
    
    # Project to DAG conversion
    stage_registry.cpp
    project_to_dag.cpp
)

target_include_directories(orc-core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/orc/view-types  # Access to view types (MVP architecture allows core -> view dependency)
    ${CMAKE_CURRENT_SOURCE_DIR}/observers
    ${CMAKE_CURRENT_SOURCE_DIR}/analysis
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/pal_comp_source
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/ntsc_comp_source
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/pal_yc_source
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/ntsc_yc_source
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/ld_sink
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/hackdac_sink
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/dropout_correct
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/dropout_map
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/field_invert
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/field_map
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/source_align
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/stacker
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/chroma_sink
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/audio_sink
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/efm_sink
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/cc_sink
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/video_params
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/dropout_analysis_sink
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/snr_analysis_sink
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/burst_level_analysis_sink
)

# =============================================================================
# MVP Architecture - Core Headers are PRIVATE
# =============================================================================
# Core headers are marked PRIVATE to prevent transitive inclusion.
# Only orc-presenters should directly access core headers.
# GUI/CLI code accesses core functionality through presenters only.
# =============================================================================
# Note: All include directories above are PRIVATE by default
# =============================================================================

# Link against common types
target_link_libraries(orc-core PUBLIC orc-common)

# Compiler-specific warning flags
if(MSVC)
    target_compile_options(orc-core PRIVATE /W4)
else()
    target_compile_options(orc-core PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:Release>:-O3>
    )
endif()

# Enable debug/trace logging at compile time
target_compile_definitions(orc-core PRIVATE 
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
    $<$<PLATFORM_ID:Windows>:_USE_MATH_DEFINES>
)

# Find and link SQLite3
find_package(SQLite3 REQUIRED)
target_link_libraries(orc-core PUBLIC SQLite::SQLite3)

# Find and link yaml-cpp
find_package(yaml-cpp REQUIRED)
# Create an alias if yaml-cpp::yaml-cpp doesn't exist (for compatibility)
if(TARGET yaml-cpp AND NOT TARGET yaml-cpp::yaml-cpp)
    add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
endif()
target_link_libraries(orc-core PUBLIC yaml-cpp::yaml-cpp)

# Find and link spdlog
find_package(spdlog REQUIRED)
target_link_libraries(orc-core PUBLIC spdlog::spdlog)

# Find and link libpng
find_package(PNG REQUIRED)
target_link_libraries(orc-core PUBLIC PNG::PNG)

# Add chroma decoder library subdirectory (Pure C++17, no Qt6 dependencies)
add_subdirectory(stages/chroma_sink/decoders)
target_link_libraries(orc-core PRIVATE orc_chroma_decoders)

# Find and link FFmpeg libraries (for MP4 output)
if(WIN32)
    # On Windows, try to find FFmpeg manually first, fall back to pkg-config
    # This handles Chocolatey FFmpeg installations better
    if(DEFINED ENV{FFMPEG_ROOT})
        set(FFMPEG_ROOT $ENV{FFMPEG_ROOT})
    endif()
    
    # Try to find FFmpeg headers and libraries manually
    find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h
        HINTS ${FFMPEG_ROOT}/include ${CMAKE_PREFIX_PATH}/include
        PATH_SUFFIXES include
    )
    
    find_library(AVCODEC_LIBRARY
        NAMES avcodec libavcodec
        HINTS ${FFMPEG_ROOT}/lib ${CMAKE_PREFIX_PATH}/lib ${FFMPEG_ROOT}/bin
        PATH_SUFFIXES lib bin
    )
    
    find_library(AVFORMAT_LIBRARY
        NAMES avformat libavformat
        HINTS ${FFMPEG_ROOT}/lib ${CMAKE_PREFIX_PATH}/lib ${FFMPEG_ROOT}/bin
        PATH_SUFFIXES lib bin
    )
    
    find_library(AVUTIL_LIBRARY
        NAMES avutil libavutil
        HINTS ${FFMPEG_ROOT}/lib ${CMAKE_PREFIX_PATH}/lib ${FFMPEG_ROOT}/bin
        PATH_SUFFIXES lib bin
    )
    
    find_library(SWSCALE_LIBRARY
        NAMES swscale libswscale
        HINTS ${FFMPEG_ROOT}/lib ${CMAKE_PREFIX_PATH}/lib ${FFMPEG_ROOT}/bin
        PATH_SUFFIXES lib bin
    )
    
    if(AVCODEC_INCLUDE_DIR AND AVCODEC_LIBRARY AND AVFORMAT_LIBRARY AND AVUTIL_LIBRARY AND SWSCALE_LIBRARY)
        # Create imported targets manually
        add_library(FFMPEG::avcodec UNKNOWN IMPORTED)
        set_target_properties(FFMPEG::avcodec PROPERTIES
            IMPORTED_LOCATION "${AVCODEC_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${AVCODEC_INCLUDE_DIR}"
        )
        
        add_library(FFMPEG::avformat UNKNOWN IMPORTED)
        set_target_properties(FFMPEG::avformat PROPERTIES
            IMPORTED_LOCATION "${AVFORMAT_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${AVCODEC_INCLUDE_DIR}"
        )
        
        add_library(FFMPEG::avutil UNKNOWN IMPORTED)
        set_target_properties(FFMPEG::avutil PROPERTIES
            IMPORTED_LOCATION "${AVUTIL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${AVCODEC_INCLUDE_DIR}"
        )
        
        add_library(FFMPEG::swscale UNKNOWN IMPORTED)
        set_target_properties(FFMPEG::swscale PROPERTIES
            IMPORTED_LOCATION "${SWSCALE_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${AVCODEC_INCLUDE_DIR}"
        )
        
        set(FFMPEG_FOUND TRUE)
        message(STATUS "Found FFmpeg (manual discovery)")
        message(STATUS "  avcodec: ${AVCODEC_LIBRARY}")
        message(STATUS "  avformat: ${AVFORMAT_LIBRARY}")
        message(STATUS "  avutil: ${AVUTIL_LIBRARY}")
        message(STATUS "  swscale: ${SWSCALE_LIBRARY}")
    else()
        set(FFMPEG_FOUND FALSE)
    endif()
endif()

# Use pkg-config for FFmpeg on non-Windows platforms
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavcodec
        libavformat
        libavutil
        libswscale
    )

    # Create aggregate PkgConfig::FFMPEG target if it doesn't exist
    # (pkg_check_modules may create individual targets like PkgConfig::libavcodec)
    if(NOT TARGET PkgConfig::FFMPEG)
        add_library(PkgConfig::FFMPEG INTERFACE IMPORTED)
        set(_ffmpeg_pc_libs "")
        foreach(_ffmpeg_pc_target libavcodec libavformat libavutil libswscale)
            if(TARGET PkgConfig::${_ffmpeg_pc_target})
                list(APPEND _ffmpeg_pc_libs PkgConfig::${_ffmpeg_pc_target})
            endif()
        endforeach()

        if(_ffmpeg_pc_libs)
            set_target_properties(PkgConfig::FFMPEG PROPERTIES
                INTERFACE_LINK_LIBRARIES "${_ffmpeg_pc_libs}"
            )
        else()
            set_target_properties(PkgConfig::FFMPEG PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIRS}"
                INTERFACE_LINK_LIBRARIES "${FFMPEG_LIBRARIES}"
            )
            if(FFMPEG_LIBRARY_DIRS)
                set_property(TARGET PkgConfig::FFMPEG PROPERTY
                    INTERFACE_LINK_DIRECTORIES "${FFMPEG_LIBRARY_DIRS}"
                )
            endif()
        endif()
        unset(_ffmpeg_pc_libs)
    endif()
endif()

# Link FFmpeg (mirror encode-orc scheme)
target_compile_definitions(orc-core PUBLIC HAVE_FFMPEG)
if(WIN32 AND FFMPEG_FOUND AND TARGET FFMPEG::avcodec)
    # Using manual FFmpeg discovery on Windows
    target_link_libraries(orc-core PUBLIC
        FFMPEG::avcodec
        FFMPEG::avformat
        FFMPEG::avutil
        FFMPEG::swscale
    )
else()
    # Using pkg-config FFmpeg
    target_link_libraries(orc-core PUBLIC PkgConfig::FFMPEG)
endif()
