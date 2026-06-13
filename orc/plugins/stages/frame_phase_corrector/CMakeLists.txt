file(GLOB_RECURSE STAGE_PLUGIN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

orc_add_stage_plugin(
    orc-stage-plugin-frame-phase-corrector
    OUTPUT_NAME orc-stage-plugin-frame-phase-corrector
    PLUGIN_VERSION "${ORC_VERSION}"
    SOURCES ${STAGE_PLUGIN_SOURCES}
)
