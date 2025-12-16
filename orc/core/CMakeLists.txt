add_library(orc-core STATIC
    # Phase 1: Foundations
    field_id.cpp
    video_field_representation.cpp
    artifact.cpp
    dag_executor.cpp
)

target_include_directories(orc-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(orc-core PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O3>
)
