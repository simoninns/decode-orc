file(GLOB EFM_PLUGIN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

orc_add_stage_plugin(
    orc-stage-plugin-efm-sink
    OUTPUT_NAME orc-stage-plugin-efm-sink
    PLUGIN_VERSION "${ORC_VERSION}"
    SOURCES ${EFM_PLUGIN_SOURCES}
    LINK_LIBRARIES orc-efm-decode
)
