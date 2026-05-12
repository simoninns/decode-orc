cmake_minimum_required(VERSION 3.20)

project(orc-core-unit-tests)

# GoogleTest requires at least C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(GTest REQUIRED)

# This is already included in the top-level cmakelist so does not need to be included here
#enable_testing()

function(orc_add_core_unit_tests target labels)
        string(REPLACE ";" "\\;" escaped_labels "${labels}")

    add_executable(${target} ${ARGN})
        target_include_directories(${target} PRIVATE
                ${CMAKE_SOURCE_DIR}/orc/plugins/stages/daphne_vbi_sink
                ${CMAKE_SOURCE_DIR}/orc/plugins/stages/ld_sink
                ${CMAKE_SOURCE_DIR}/orc/sdk/include
        )
    target_link_libraries(
            ${target}
            orc-core
            GTest::gmock_main
    )

    target_compile_definitions(${target} PRIVATE
            ORC_STAGE_PLUGIN_BUILD_DIR="${CMAKE_BINARY_DIR}/lib/orc-stage-plugins"
    )

        if(TARGET orc_chroma_decoders)
                target_link_libraries(${target} orc_chroma_decoders)
        endif()

        # Phase 7A: stage instances in contract tests are now obtained via StageRegistry,
        # not by direct concrete class construction. These static plugin links are still
        # required so that each plugin's registration callback fires during static
        # initialisation, populating the registry before any test runs.
        # Phase 8 will add dedicated integration tests that exercise the runtime
        # dlopen/LoadLibrary plugin-loading path (ABI matching, missing symbols, etc.)
        # alongside these registry-population links.
        foreach(_orc_stage_plugin_target
                        orc-stage-plugin-ntsc-comp-source
                        orc-stage-plugin-ntsc-yc-source
                        orc-stage-plugin-pal-comp-source
                        orc-stage-plugin-pal-yc-source
                        orc-stage-plugin-stacker
                        orc-stage-plugin-field-invert
                        orc-stage-plugin-field-map
                        orc-stage-plugin-video-params
                        orc-stage-plugin-dropout-correct
                        orc-stage-plugin-dropout-map
                        orc-stage-plugin-source-align
                        orc-stage-plugin-mask-line
                        orc-stage-plugin-raw-video-sink
                        orc-stage-plugin-ffmpeg-video-sink
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

include(GoogleTest)

orc_add_core_unit_tests(
        orc-core-unit-tests-sources
        "unit;sources"

        stages/source_common/source_stage_contract_test.cpp
        stages/ntsc_comp_source/ntsc_comp_source_stage_test.cpp
        stages/ntsc_yc_source/ntsc_yc_source_stage_test.cpp
        stages/pal_comp_source/pal_comp_source_stage_test.cpp
        stages/pal_yc_source/pal_yc_source_stage_test.cpp
)

orc_add_core_unit_tests(
        orc-core-unit-tests-transforms
        "unit;transforms"

        stages/stacker/stacker_stage_test.cpp
        stages/field_invert/field_invert_stage_test.cpp
        stages/field_map/field_map_stage_test.cpp
        stages/video_params/video_params_stage_test.cpp
        stages/dropout_correct/dropout_correct_stage_test.cpp
        stages/dropout_map/dropout_map_stage_test.cpp
        stages/source_align/source_align_stage_test.cpp
        stages/mask_line/mask_line_stage_test.cpp
)

orc_add_core_unit_tests(
        orc-core-unit-tests-sinks
        "unit;sinks"

        stages/chroma_sink/componentframe_test.cpp
        stages/chroma_sink/vectorscope_analysis_test.cpp
        stages/chroma_sink/chroma_sink_stage_defaults_test.cpp
        stages/chroma_sink/chroma_sink_stage_safety_test.cpp
        stages/chroma_sink/chroma_sink_tweak_panel_test.cpp
        stages/chroma_sink/comb_test.cpp
        stages/chroma_sink/ffmpeg_video_sink_stage_test.cpp
        stages/chroma_sink/monodecoder_test.cpp
        stages/chroma_sink/ntsc_pal_decoder_wrapper_test.cpp
        stages/chroma_sink/palcolour_test.cpp
        stages/chroma_sink/raw_video_sink_stage_test.cpp
        stages/chroma_sink/sourcefield_test.cpp
        stages/daphne_vbi_sink/daphne_vbi_writer_util_test.cpp
        stages/daphne_vbi_sink/daphne_vbi_sink_stage_deps_test.cpp
        stages/daphne_vbi_sink/daphne_vbi_sink_stage_test.cpp
        stages/audio_sink/audio_sink_stage_test.cpp
        stages/ac3rf_sink/ac3rf_sink_stage_test.cpp
        stages/cc_sink/cc_sink_stage_test.cpp
        stages/ld_sink/ld_sink_stage_test.cpp
        stages/ld_sink/ld_sink_stage_deps_test.cpp
        stages/efm_sink/efm_sink_stage_test.cpp
        stages/raw_efm_sink/raw_efm_sink_stage_test.cpp
        stages/hackdac_sink/hackdac_sink_stage_test.cpp
        stages/dropout_analysis_sink/dropout_analysis_sink_stage_test.cpp
        stages/snr_analysis_sink/snr_analysis_sink_stage_test.cpp
        stages/burst_level_analysis_sink/burst_level_analysis_sink_stage_test.cpp
)

orc_add_core_unit_tests(
        orc-core-unit-tests-contracts
        "unit;contracts"

        contracts/public_stage_contract_test.cpp
        contracts/stage_registry_contract_test.cpp
        contracts/stage_plugin_loader_smoke_test.cpp
        contracts/project_to_dag_contract_test.cpp
        contracts/phase_d_runtime_architecture_test.cpp
        contracts/plugin_safe_call_test.cpp
)

# ---------------------------------------------------------------------------
# Test-fixture plugin shared libraries for loader version rejection tests
# ---------------------------------------------------------------------------
set(ORC_TEST_FIXTURE_PLUGIN_BUILD_DIR "${CMAKE_BINARY_DIR}/lib/orc-test-fixture-plugins")

foreach(_fixture_name bad_abi_version bad_api_version)
        set(_target "orc-test-fixture-${_fixture_name}")
        add_library(${_target} SHARED
                contracts/test_fixtures/${_fixture_name}_plugin.cpp)
        target_include_directories(${_target} PRIVATE
                ${CMAKE_SOURCE_DIR}/orc/sdk/include)
        set_target_properties(${_target} PROPERTIES
                LIBRARY_OUTPUT_DIRECTORY "${ORC_TEST_FIXTURE_PLUGIN_BUILD_DIR}"
                RUNTIME_OUTPUT_DIRECTORY "${ORC_TEST_FIXTURE_PLUGIN_BUILD_DIR}")
endforeach()

add_executable(orc-core-unit-tests-loader-rejection
        contracts/stage_plugin_loader_version_rejection_test.cpp)
target_include_directories(orc-core-unit-tests-loader-rejection PRIVATE
        ${CMAKE_SOURCE_DIR}/orc/sdk/include)
target_link_libraries(orc-core-unit-tests-loader-rejection
        orc-core
        GTest::gmock_main)
target_compile_definitions(orc-core-unit-tests-loader-rejection PRIVATE
        ORC_TEST_FIXTURE_PLUGIN_BUILD_DIR="${ORC_TEST_FIXTURE_PLUGIN_BUILD_DIR}")
add_dependencies(orc-core-unit-tests-loader-rejection
        orc-test-fixture-bad_abi_version
        orc-test-fixture-bad_api_version)
gtest_discover_tests(orc-core-unit-tests-loader-rejection
        DISCOVERY_MODE POST_BUILD
        DISCOVERY_TIMEOUT 30
        PROPERTIES LABELS "unit\;contracts")

orc_add_core_unit_tests(
        orc-core-unit-tests-metadata
        "unit"

        metadata/video_system_parsing_test.cpp
        metadata/project_plugin_metadata_test.cpp
        metadata/stage_plugin_registry_test.cpp
        metadata/plugin_remote_loader_test.cpp
        metadata/phase_7d_skeleton_load_test.cpp
)

        orc_add_core_unit_tests(
                orc-core-unit-tests-preview-foundation
                "unit;preview"

                preview_types/preview_types_test.cpp
                preview_types/preview_carrier_conversion_test.cpp
                preview_types/preview_view_registry_test.cpp
                preview_types/preview_helpers_yc_combine_test.cpp
        )