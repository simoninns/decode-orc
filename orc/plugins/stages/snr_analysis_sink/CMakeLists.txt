set(STAGE_PLUGIN_SOURCES
    snr_analysis_sink_stage.cpp
    snr_analysis_sink_deps.cpp
    plugin.cpp
)

orc_add_stage_plugin(
    orc-stage-plugin-snr-analysis-sink
    OUTPUT_NAME orc-stage-plugin-snr-analysis-sink
    PLUGIN_VERSION "${ORC_VERSION}"
    SOURCES ${STAGE_PLUGIN_SOURCES}
)
