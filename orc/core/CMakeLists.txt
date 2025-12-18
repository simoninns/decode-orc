add_library(orc-core STATIC
    # Phase 1: Foundations
    field_id.cpp
    video_field_representation.cpp
    artifact.cpp
    dag_executor.cpp
    
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
    
    # Phase 3: Dropout Correction
    dropout_decision.cpp
    dropout_correct_stage.cpp
    
    # Utility stages
    passthrough_stage.cpp
    
    # Stage parameters
    stage_parameter.cpp
    
    # DAG serialization
    dag_serialization.cpp
    
    # Project file I/O
    project.cpp
)

target_include_directories(orc-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(orc-core PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O3>
)

# Find and link SQLite3
find_package(SQLite3 REQUIRED)
target_link_libraries(orc-core PUBLIC SQLite::SQLite3)
