set(STAGE_PLUGIN_SOURCES
    burst_level_analysis_sink_stage.cpp
    burst_level_analysis_sink_deps.cpp
    plugin.cpp
)

orc_add_stage_plugin(
    orc-stage-plugin-burst-level-analysis-sink
    OUTPUT_NAME orc-stage-plugin-burst-level-analysis-sink
    PLUGIN_VERSION "${ORC_VERSION}"
    SOURCES ${STAGE_PLUGIN_SOURCES}
)
