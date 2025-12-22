add_library(orc-core STATIC
    # Phase 1: Foundations
    field_id.cpp
    video_field_representation.cpp
    artifact.cpp
    dag_executor.cpp
    dag_field_renderer.cpp
    preview_renderer.cpp
    
    # Logging
    logging.cpp
    
    # TBC file I/O
    tbc_reader.cpp
    tbc_metadata.cpp
    tbc_video_field_representation.cpp
    
    # VBI utilities
    vbi_utilities.cpp
    
    # Observers (organized in subdirectory)
    observers/observer.cpp
    observers/observation_history.cpp
    observers/biphase_observer.cpp
    observers/vitc_observer.cpp
    observers/closed_caption_observer.cpp
    observers/video_id_observer.cpp
    observers/fm_code_observer.cpp
    observers/white_flag_observer.cpp
    observers/vits_observer.cpp
    observers/burst_level_observer.cpp
    # observers/field_parity_observer.cpp  # Removed: field parity comes from hints only
    # observers/pal_phase_observer.cpp  # Removed: PAL phase comes from hints only
    observers/disc_quality_observer.cpp
    observers/pulldown_observer.cpp
    observers/lead_in_out_observer.cpp
    
    # Analysis framework
    analysis/analysis_registry.cpp
    analysis/analysis_init.cpp
    
    # Field mapping analysis
    analysis/field_mapping/field_mapping_analyzer.cpp
    analysis/field_mapping/field_mapping_analysis.cpp
    
    # Stages (organized in subdirectories)
    stages/ld_pal_source/ld_pal_source_stage.cpp
    stages/ld_ntsc_source/ld_ntsc_source_stage.cpp
    stages/ld_sink/ld_sink_stage.cpp
    stages/ld_sink/tbc_metadata_writer.cpp
    stages/dropout_correct/dropout_correct_stage.cpp
    stages/passthrough/passthrough_stage.cpp
    stages/passthrough_splitter/passthrough_splitter_stage.cpp
    stages/passthrough_merger/passthrough_merger_stage.cpp
    stages/passthrough_complex/passthrough_complex_stage.cpp
    stages/overwrite/overwrite_stage.cpp
    stages/field_map/field_map_stage.cpp
    stages/stacker/stacker_stage.cpp
    stages/chroma_sink/chroma_sink_stage.cpp
    
    # Dropout decision utilities
    dropout_decision.cpp
    
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
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/ld_pal_source
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/ld_ntsc_source
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/ld_sink
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/dropout_correct
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/passthrough
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/passthrough_splitter
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/passthrough_merger
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/passthrough_complex
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/field_map
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/stacker
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/chroma_sink
)

target_compile_options(orc-core PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O3>
)

# Enable debug/trace logging at compile time
target_compile_definitions(orc-core PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

# Find and link SQLite3
find_package(SQLite3 REQUIRED)
target_link_libraries(orc-core PUBLIC SQLite::SQLite3)

# Find and link yaml-cpp
find_package(yaml-cpp REQUIRED)
target_link_libraries(orc-core PUBLIC yaml-cpp)

# Find and link spdlog
find_package(spdlog REQUIRED)
target_link_libraries(orc-core PUBLIC spdlog::spdlog)

# Find and link libpng
find_package(PNG REQUIRED)
target_link_libraries(orc-core PUBLIC PNG::PNG)
