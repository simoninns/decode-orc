add_library(orc-core STATIC
    # Phase 1: Foundations
    field_id.cpp
    video_field_representation.cpp
    artifact.cpp
    dag_executor.cpp
    dag_field_renderer.cpp
    
    # Logging
    logging.cpp
    
    # TBC file I/O
    tbc_reader.cpp
    tbc_metadata.cpp
    tbc_video_field_representation.cpp
    
    # Phase 2: Observers
    observer.cpp
    vits_observer.cpp
    
    # Phase 2b: VBI Observers
    vbi_utilities.cpp
    biphase_observer.cpp
    vitc_observer.cpp
    closed_caption_observer.cpp
    video_id_observer.cpp
    fm_code_observer.cpp
    white_flag_observer.cpp
    
    # Stages (organized in subdirectories)
    stages/ld_pal_source/ld_pal_source_stage.cpp
    stages/ld_ntsc_source/ld_ntsc_source_stage.cpp
    stages/ld_sink/ld_sink_stage.cpp
    stages/dropout_correct/dropout_correct_stage.cpp
    stages/passthrough/passthrough_stage.cpp
    stages/passthrough_splitter/passthrough_splitter_stage.cpp
    stages/passthrough_merger/passthrough_merger_stage.cpp
    stages/passthrough_complex/passthrough_complex_stage.cpp
    stages/overwrite/overwrite_stage.cpp
    stages/field_map/field_map_stage.cpp
    
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
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/ld_pal_source
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/ld_ntsc_source
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/ld_sink
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/dropout_correct
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/passthrough
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/passthrough_splitter
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/passthrough_merger
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/passthrough_complex
    ${CMAKE_CURRENT_SOURCE_DIR}/stages/field_map
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
