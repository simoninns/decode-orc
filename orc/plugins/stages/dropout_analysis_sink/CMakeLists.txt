set(STAGE_PLUGIN_SOURCES
    dropout_analysis_sink_stage.cpp
    dropout_analysis_sink_deps.cpp
    plugin.cpp
)

orc_add_stage_plugin(
    orc-stage-plugin-dropout-analysis-sink
    OUTPUT_NAME orc-stage-plugin-dropout-analysis-sink
    PLUGIN_VERSION "${ORC_VERSION}"
    SOURCES ${STAGE_PLUGIN_SOURCES}
)
