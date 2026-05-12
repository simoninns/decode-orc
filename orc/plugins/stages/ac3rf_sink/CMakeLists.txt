file(GLOB AC3RF_VENDOR_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/ac3rf-lib/ac3/*.cpp
)

if(NOT MSVC)
    set_source_files_properties(${AC3RF_VENDOR_SOURCES} PROPERTIES COMPILE_FLAGS "-w")
endif()

set(AC3RF_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ac3rf_sink_stage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ac3rf_sink_stage_deps.cpp
    ${AC3RF_VENDOR_SOURCES}
)

orc_add_stage_plugin(
    orc-stage-plugin-ac3rf-sink
    OUTPUT_NAME orc-stage-plugin-ac3rf-sink
    PLUGIN_VERSION "${ORC_VERSION}"
    SOURCES ${AC3RF_PLUGIN_SOURCES}
)

target_include_directories(orc-stage-plugin-ac3rf-sink PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ac3rf-lib
)
