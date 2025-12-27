# Chroma Decoder Sink Stage

add_library(chroma_sink_stage STATIC
    chroma_sink_stage.cpp
    output_backend.cpp
    raw_output_backend.cpp
    ffmpeg_output_backend.cpp
)

target_include_directories(chroma_sink_stage
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(chroma_sink_stage
    PUBLIC orc_core_interfaces
    PRIVATE orc_tbc_library
)

# Register with parent
set_property(GLOBAL APPEND PROPERTY ORC_STAGE_LIBRARIES chroma_sink_stage)
