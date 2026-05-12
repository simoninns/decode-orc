set(CHROMA_DECODER_SHARED_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/../common/chroma_sink_stage.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../common/output_backend.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../common/raw_output_backend.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../common/ffmpeg_output_backend.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../common/ffmpeg_video_sink_stage.cpp
)

orc_add_stage_plugin(
    orc-stage-plugin-ffmpeg-video-sink
    OUTPUT_NAME orc-stage-plugin-ffmpeg-video-sink
    PLUGIN_VERSION "${ORC_VERSION}"
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin.cpp
        ${CHROMA_DECODER_SHARED_SOURCES}
    LINK_LIBRARIES
        orc_chroma_decoders
)

target_include_directories(orc-stage-plugin-ffmpeg-video-sink PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../common
)
