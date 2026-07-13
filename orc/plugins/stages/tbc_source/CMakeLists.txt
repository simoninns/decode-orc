file(GLOB_RECURSE STAGE_PLUGIN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

orc_add_stage_plugin(
    orc-stage-plugin-tbc-source
    OUTPUT_NAME orc-stage-plugin-tbc-source
    PLUGIN_VERSION "${ORC_VERSION}"
    SOURCES ${STAGE_PLUGIN_SOURCES}
    # SQLite is used by tbc_metadata_reader.cpp; declared here because the
    # SDK no longer propagates host third-party dependencies to plugins.
    # orc-audio-resample is the shared SoXR-based free-running↔locked
    # resampler (orc/plugins/stages/common/audio-resample).
    LINK_LIBRARIES orc-audio-resample SQLite::SQLite3
)
