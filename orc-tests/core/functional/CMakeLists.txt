cmake_minimum_required(VERSION 3.20)

project(orc-core-functional-tests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(GTest REQUIRED)

include(GoogleTest)

function(orc_add_core_functional_tests target labels)
    string(REPLACE ";" "\\;" escaped_labels "${labels}")

    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/orc/plugins/stages/cvbs_sink
        ${CMAKE_SOURCE_DIR}/orc/plugins/stages/cvbs_source
        ${CMAKE_SOURCE_DIR}/orc/plugins/stages/daphne_vbi_sink
        ${CMAKE_SOURCE_DIR}/orc/plugins/stages/ld_sink
        ${CMAKE_SOURCE_DIR}/orc/sdk/include
    )
    target_link_libraries(${target}
        orc-core
        GTest::gmock_main
    )
    target_compile_definitions(${target} PRIVATE
        ORC_STAGE_PLUGIN_BUILD_DIR="${CMAKE_BINARY_DIR}/lib/orc-stage-plugins"
    )

    if(TARGET orc_chroma_decoders)
        target_link_libraries(${target} orc_chroma_decoders)
    endif()

    foreach(_orc_stage_plugin_target
            orc-stage-plugin-ntsc-comp-source
            orc-stage-plugin-ntsc-yc-source
            orc-stage-plugin-pal-comp-source
            orc-stage-plugin-pal-yc-source
            orc-stage-plugin-cvbs-source
            orc-stage-plugin-cvbs-sink
            orc-stage-plugin-stacker
            orc-stage-plugin-field-invert
            orc-stage-plugin-field-map
            orc-stage-plugin-video-params
            orc-stage-plugin-dropout-correct
            orc-stage-plugin-dropout-map
            orc-stage-plugin-source-align
            orc-stage-plugin-mask-line
            orc-stage-plugin-video-sink
            orc-stage-plugin-daphne-vbi-sink
            orc-stage-plugin-audio-sink
            orc-stage-plugin-cc-sink
            orc-stage-plugin-ld-sink
            orc-stage-plugin-efm-sink
            orc-stage-plugin-raw-efm-sink
            orc-stage-plugin-hackdac-sink
            orc-stage-plugin-ac3rf-sink
            orc-stage-plugin-dropout-analysis-sink
            orc-stage-plugin-snr-analysis-sink
            orc-stage-plugin-burst-level-analysis-sink)
        if(TARGET ${_orc_stage_plugin_target})
            target_link_libraries(${target} ${_orc_stage_plugin_target})
        endif()
    endforeach()

    if(TARGET orc-stage-plugins)
        add_dependencies(${target} orc-stage-plugins)
    endif()

    gtest_discover_tests(${target}
        DISCOVERY_MODE POST_BUILD
        DISCOVERY_TIMEOUT 30
        PROPERTIES LABELS "${escaped_labels}")
endfunction()

orc_add_core_functional_tests(
    orc-core-functional-tests-contracts
    "functional;contracts"

    contracts/stage_plugin_loader_smoke_test.cpp
    contracts/runtime_stage_contracts_test.cpp
    contracts/multi_track_audio_roundtrip_test.cpp
)

# Plugin isolation tests get a dedicated executable that deliberately does
# NOT link any stage plugin target: linking a plugin would map its library
# (and unify its symbols/typeinfo) before StagePluginLoader runs, making the
# RTLD_LOCAL RTTI and keep-alive lifetime checks pass trivially.
add_executable(orc-core-functional-tests-plugin-isolation
    contracts/stage_plugin_isolation_test.cpp
)
target_include_directories(orc-core-functional-tests-plugin-isolation PRIVATE
    ${CMAKE_SOURCE_DIR}/orc/sdk/include
)
target_link_libraries(orc-core-functional-tests-plugin-isolation
    orc-core
    GTest::gmock_main
)
target_compile_definitions(orc-core-functional-tests-plugin-isolation PRIVATE
    ORC_STAGE_PLUGIN_BUILD_DIR="${CMAKE_BINARY_DIR}/lib/orc-stage-plugins"
)
if(TARGET orc-stage-plugins)
    add_dependencies(orc-core-functional-tests-plugin-isolation orc-stage-plugins)
endif()
gtest_discover_tests(orc-core-functional-tests-plugin-isolation
    DISCOVERY_MODE POST_BUILD
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "functional\;contracts")
