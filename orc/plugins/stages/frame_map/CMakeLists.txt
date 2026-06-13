file(GLOB_RECURSE STAGE_PLUGIN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

orc_add_stage_plugin(
    orc-stage-plugin-frame-map
    OUTPUT_NAME orc-stage-plugin-frame-map
    PLUGIN_VERSION "${ORC_VERSION}"
    SOURCES ${STAGE_PLUGIN_SOURCES}
)
