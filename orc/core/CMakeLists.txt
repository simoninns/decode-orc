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
