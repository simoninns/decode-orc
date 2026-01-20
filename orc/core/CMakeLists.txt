add_library(orc-core STATIC
    # Phase 1: Foundations
    field_id.cpp
    node_id.cpp
    video_field_representation.cpp
    observation_wrapper_representation.cpp
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
    tbc_video_field_representation.cpp
    tbc_yc_video_field_representation.cpp
    
    # VBI utilities
    vbi_utilities.cpp
    vbi_decoder.cpp
    
    # EIA-608 decoder for closed captions
    eia608_decoder.cpp
    
    # Observers (organized in subdirectory)
    observers/observer.cpp
    # TODO: Old observers temporarily disabled during refactor
    # observers/observation_history.cpp
    # observers/biphase_observer.cpp
    # observers/vitc_observer.cpp
    # observers/closed_caption_observer.cpp
    # observers/video_id_observer.cpp
    # observers/fm_code_observer.cpp
    # observers/white_flag_observer.cpp
    # observers/vits_observer.cpp
    # observers/burst_level_observer.cpp
    # observers/pulldown_observer.cpp
    # observers/lead_in_out_observer.cpp
    # observers/dropout_analysis_observer.cpp
    # observers/snr_analysis_observer.cpp
    
    # Analysis framework
    analysis/analysis_registry.cpp
    analysis/analysis_init.cpp
    analysis/batch_analysis_tool.cpp
    
    # Vectorscope analysis
    analysis/vectorscope/vectorscope_analysis.cpp
    
    # Field mapping analysis
    # TODO: Temporarily disabled during observer refactor - uses old BiphaseObservation API
    # analysis/field_mapping/field_mapping_analyzer.cpp
    # analysis/field_mapping/field_mapping_analysis.cpp
    # analysis/field_mapping/field_mapping_lookup.cpp
    # analysis/field_mapping/field_mapping_range_analysis.cpp
    
    # Field corruption analysis (testing tool)
    analysis/field_corruption/field_corruption_analyzer.cpp
    analysis/field_corruption/field_corruption_analysis.cpp
    
    # Dropout analysis
    analysis/dropout/dropout_analysis_decoder.cpp
    
    # Dropout editor tool
    analysis/dropout_editor_tool.cpp
    
    # SNR analysis
    analysis/snr/snr_analysis_decoder.cpp
    
    # Burst level analysis
    analysis/burst_level/burst_level_analysis_decoder.cpp
    
    # Source alignment analysis
    # TODO: Temporarily disabled during observer refactor - uses old observation API
    # analysis/source_alignment/source_alignment_analysis.cpp
    
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

target_include_directories(orc-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
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
)

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
find_package(PkgConfig REQUIRED)
pkg_check_modules(YAML_CPP REQUIRED yaml-cpp)
target_include_directories(orc-core PUBLIC ${YAML_CPP_INCLUDE_DIRS})
target_link_directories(orc-core PUBLIC ${YAML_CPP_LIBRARY_DIRS})
target_link_libraries(orc-core PUBLIC ${YAML_CPP_LIBRARIES})

# Find and link spdlog
find_package(spdlog REQUIRED)
target_link_libraries(orc-core PUBLIC spdlog::spdlog)

# Find and link libpng
find_package(PNG REQUIRED)
target_link_libraries(orc-core PUBLIC PNG::PNG)

# Add chroma decoder library subdirectory (Pure C++17, no Qt6 dependencies)
add_subdirectory(stages/chroma_sink/decoders)
target_link_libraries(orc-core PRIVATE orc_chroma_decoders)

# Find and optionally link FFmpeg libraries (for MP4 output)
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(FFMPEG libavformat libavcodec libavutil libswscale)
    if(FFMPEG_FOUND)
        message(STATUS "FFmpeg libraries found - MP4 output will be available")
        target_compile_definitions(orc-core PUBLIC HAVE_FFMPEG)
        target_include_directories(orc-core PUBLIC ${FFMPEG_INCLUDE_DIRS})
        target_link_directories(orc-core PUBLIC ${FFMPEG_LIBRARY_DIRS})
        target_link_libraries(orc-core PUBLIC ${FFMPEG_LIBRARIES})
    else()
        message(STATUS "FFmpeg libraries not found - MP4 output will be unavailable")
    endif()
else()
    message(STATUS "pkg-config not found - skipping FFmpeg detection")
endif()
