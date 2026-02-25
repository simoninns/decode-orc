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
    analysis/field_map_range/field_map_range_analysis.cpp
    
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
    stages/efm_decoder/efm_decoder_sink_stage.cpp
    stages/efm_decoder/config/efm_decoder_parameter_contract.cpp
    stages/efm_decoder/report/efm_decoder_report.cpp
    stages/efm_decoder/pipeline/unified_decoder.cpp
    stages/efm_decoder/pipeline/core/audio.cpp
    stages/efm_decoder/pipeline/core/delay_lines.cpp
    stages/efm_decoder/pipeline/core/efm.cpp
    stages/efm_decoder/pipeline/core/frame.cpp
    stages/efm_decoder/pipeline/core/interleave.cpp
    stages/efm_decoder/pipeline/core/inverter.cpp
    stages/efm_decoder/pipeline/core/reedsolomon.cpp
    stages/efm_decoder/pipeline/core/rspc.cpp
    stages/efm_decoder/pipeline/core/section.cpp
    stages/efm_decoder/pipeline/core/section_metadata.cpp
    stages/efm_decoder/pipeline/core/sector.cpp
    stages/efm_decoder/pipeline/core/subcode.cpp
    stages/efm_decoder/pipeline/core/tvalues.cpp
    stages/efm_decoder/pipeline/stages/audio/decoders/dec_audiocorrection.cpp
    stages/efm_decoder/pipeline/stages/audio/decoders/dec_data24toaudio.cpp
    stages/efm_decoder/pipeline/stages/audio/writers/writer_raw.cpp
    stages/efm_decoder/pipeline/stages/audio/writers/writer_wav.cpp
    stages/efm_decoder/pipeline/stages/audio/writers/writer_wav_metadata.cpp
    stages/efm_decoder/pipeline/stages/bridge/dec_f1sectiontodata24section.cpp
    stages/efm_decoder/pipeline/stages/bridge/dec_f2sectiontof1section.cpp
    stages/efm_decoder/pipeline/stages/data/decoders/dec_data24torawsector.cpp
    stages/efm_decoder/pipeline/stages/data/decoders/dec_rawsectortosector.cpp
    stages/efm_decoder/pipeline/stages/data/decoders/dec_sectorcorrection.cpp
    stages/efm_decoder/pipeline/stages/data/writers/writer_sector.cpp
    stages/efm_decoder/pipeline/stages/data/writers/writer_sector_metadata.cpp
    stages/efm_decoder/pipeline/stages/shared/decoders/dec_channeltof3frame.cpp
    stages/efm_decoder/pipeline/stages/shared/decoders/dec_f2sectioncorrection.cpp
    stages/efm_decoder/pipeline/stages/shared/decoders/dec_f3frametof2section.cpp
    stages/efm_decoder/pipeline/stages/shared/decoders/dec_tvaluestochannel.cpp
    stages/efm_decoder/pipeline/stages/shared/readers/reader_data.cpp
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

# Suppress known warnings from imported EFM decoder pipeline sources.
# Keep project warnings strict for non-imported code.
file(GLOB_RECURSE ORC_EFM_PIPELINE_CPP CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/stages/efm_decoder/pipeline/*.cpp"
)

if(ORC_EFM_PIPELINE_CPP)
    set_source_files_properties(${ORC_EFM_PIPELINE_CPP} PROPERTIES
        COMPILE_OPTIONS "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-sign-compare;-Wno-type-limits;-Wno-unused-variable;-Wno-cpp>"
    )
endif()

# Optional ezpwd header dependency for EFM decoder integration.
# EZPWD provides header-only includes such as <ezpwd/rs> and <ezpwd/rs_base>.
set(EZPWD_INCLUDE_DIR "${EZPWD_INCLUDE_DIR}" CACHE PATH "Path containing ezpwd headers (root that contains ezpwd/rs)")

set(_EZPWD_SEARCH_PATHS
    "/app/include"
    "${ORC_REPO_ROOT}/external/ezpwd-reed-solomon/c++"
    "${CMAKE_SOURCE_DIR}/external/ezpwd-reed-solomon/c++"
    "${CMAKE_SOURCE_DIR}/../external/ezpwd-reed-solomon/c++"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../external/ezpwd-reed-solomon/c++"
    "/usr/local/include"
    "/usr/include"
)

if(NOT EZPWD_INCLUDE_DIR AND DEFINED ENV{EZPWD_INCLUDE_DIR} AND NOT "$ENV{EZPWD_INCLUDE_DIR}" STREQUAL "")
    set(EZPWD_INCLUDE_DIR "$ENV{EZPWD_INCLUDE_DIR}" CACHE PATH "Path containing ezpwd headers (root that contains ezpwd/rs)" FORCE)
endif()

if(NOT EZPWD_INCLUDE_DIR)
    find_path(EZPWD_INCLUDE_DIR
        NAMES ezpwd/rs ezpwd/rs_base
        PATHS ${_EZPWD_SEARCH_PATHS}
    )
endif()

if(EZPWD_INCLUDE_DIR AND EXISTS "${EZPWD_INCLUDE_DIR}/ezpwd/rs" AND EXISTS "${EZPWD_INCLUDE_DIR}/ezpwd/rs_base")
    target_include_directories(orc-core PRIVATE "${EZPWD_INCLUDE_DIR}")
    target_compile_definitions(orc-core PUBLIC ORC_HAVE_EZPWD)
    message(STATUS "Using ezpwd headers from: ${EZPWD_INCLUDE_DIR}")
else()
    if(EZPWD_INCLUDE_DIR)
        message(STATUS "EZPWD_INCLUDE_DIR was set but required headers were not found under: ${EZPWD_INCLUDE_DIR}")
    endif()
    message(FATAL_ERROR
        "EZPWD headers not found. The EFM decoder stage is fully integrated and requires ezpwd headers "
        "(<ezpwd/rs_base> and <ezpwd/rs>). Set EZPWD_INCLUDE_DIR to the include root containing ezpwd/rs. "
        "Searched defaults: ${_EZPWD_SEARCH_PATHS}"
    )
endif()

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
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/efm_decoder
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/efm_decoder/pipeline
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/efm_decoder/pipeline/core
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/efm_decoder/pipeline/stages/audio/decoders
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/efm_decoder/pipeline/stages/audio/writers
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/efm_decoder/pipeline/stages/bridge
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/efm_decoder/pipeline/stages/data/decoders
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/efm_decoder/pipeline/stages/data/writers
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/efm_decoder/pipeline/stages/shared/decoders
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/efm_decoder/pipeline/stages/shared/readers
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

# Warning flags
target_compile_options(orc-core PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:/Od /Zi>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2>
    $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:Debug>>:-g -O0 -U_FORTIFY_SOURCE -Wno-cpp>
    $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:Release>>:-O3>
)

# Enable debug/trace logging at compile time
target_compile_definitions(orc-core PRIVATE 
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
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

if(WIN32)
    target_link_libraries(orc-core PRIVATE dbghelp)
endif()

# Find and link libpng
find_package(PNG REQUIRED)
target_link_libraries(orc-core PUBLIC PNG::PNG)

# Add chroma decoder library subdirectory (Pure C++17, no Qt6 dependencies)
add_subdirectory(stages/chroma_sink/decoders)
target_link_libraries(orc-core PRIVATE orc_chroma_decoders)

# Find and link FFmpeg libraries (for MP4 output)
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

target_compile_definitions(orc-core PUBLIC HAVE_FFMPEG)
target_link_libraries(orc-core PUBLIC PkgConfig::FFMPEG)
