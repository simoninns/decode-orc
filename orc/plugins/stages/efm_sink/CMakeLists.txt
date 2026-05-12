file(GLOB_RECURSE EFM_PLUGIN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

if(NOT MSVC)
    # Vendored EFM pipeline sources are noisy and performance-critical.
    set_source_files_properties(${EFM_PLUGIN_SOURCES} PROPERTIES COMPILE_FLAGS "-w -O2")
endif()

orc_add_stage_plugin(
    orc-stage-plugin-efm-sink
    OUTPUT_NAME orc-stage-plugin-efm-sink
    PLUGIN_VERSION "${ORC_VERSION}"
    SOURCES ${EFM_PLUGIN_SOURCES}
)

target_include_directories(orc-stage-plugin-efm-sink PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/decoders
    ${CMAKE_CURRENT_SOURCE_DIR}/writers
    ${CMAKE_CURRENT_SOURCE_DIR}/efm-lib
    ${EZPWD_INCLUDE_DIR}
)
