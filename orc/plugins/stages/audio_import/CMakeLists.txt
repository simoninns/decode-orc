file(GLOB STAGE_PLUGIN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

orc_add_stage_plugin(
    orc-stage-plugin-audio-import
    OUTPUT_NAME orc-stage-plugin-audio-import
    PLUGIN_VERSION "${ORC_VERSION}"
    SOURCES ${STAGE_PLUGIN_SOURCES}
    # orc-audio-resample is the shared SoXR-based ingest converter to the
    # synchronous 48 kHz 24-bit channel-pair pipeline form
    # (orc/plugins/stages/common/audio-resample).
    LINK_LIBRARIES orc-audio-resample
)
