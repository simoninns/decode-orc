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
                ${CMAKE_SOURCE_DIR}/orc/plugins/stages/audio_sink
                ${CMAKE_SOURCE_DIR}/orc/plugins/stages/common
                ${CMAKE_SOURCE_DIR}/orc/plugins/stages/daphne_vbi_sink
                ${CMAKE_SOURCE_DIR}/orc/plugins/stages/ld_sink
                ${CMAKE_SOURCE_DIR}/orc/plugins/stages/raw_efm_sink
                ${CMAKE_SOURCE_DIR}/orc/plugins/stages/tbc_source
                ${CMAKE_SOURCE_DIR}/orc/sdk/include
        )
    target_link_libraries(
            ${target}
            orc-core
            GTest::gmock_main
    )

        if(TARGET orc-audio-resample)
                target_link_libraries(${target} orc-audio-resample)
        endif()

    target_compile_definitions(${target} PRIVATE
            ORC_STAGE_PLUGIN_BUILD_DIR="${CMAKE_BINARY_DIR}/lib/orc-stage-plugins"
    )

        if(TARGET orc_chroma_decoders)
                target_link_libraries(${target} orc_chroma_decoders)
        endif()

        # Stage instances in contract tests are obtained via StageRegistry, not by
        # direct concrete class construction. These static plugin links are required
        # so that each plugin's registration callback fires during static
        # initialisation, populating the registry before any test runs.
        # Dedicated functional tests cover the runtime dlopen/LoadLibrary path
        # (ABI matching, missing symbols, etc.) alongside these registry links.
        foreach(_orc_stage_plugin_target
                        orc-stage-plugin-tbc-source
                        orc-stage-plugin-cvbs-source
                        orc-stage-plugin-stacker
                        orc-stage-plugin-frame-map
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
                        orc-stage-plugin-efm-audio-decode
                        orc-stage-plugin-audio-import
                        orc-stage-plugin-audio-channel-map
                        orc-stage-plugin-audio-align
                        orc-stage-plugin-raw-efm-sink
                        orc-stage-plugin-ac3rf-sink
                        orc-stage-plugin-dropout-analysis-sink
                        orc-stage-plugin-snr-analysis-sink
                        orc-stage-plugin-burst-level-analysis-sink
                        orc-stage-plugin-cvbs-sink)
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
        stages/tbc_source/tbc_source_stage_test.cpp
        stages/tbc_source/pal_tbc_converter_test.cpp
        stages/tbc_source/ntsc_tbc_converter_test.cpp
        stages/cvbs_source/cvbs_source_stage_test.cpp
        stages/audio_resample/audio_resampler_test.cpp
)

orc_add_core_unit_tests(
        orc-core-unit-tests-transforms
        "unit;transforms"

        stages/stacker/stacker_stage_test.cpp
        stages/frame_map/frame_map_stage_test.cpp
        analysis/frame_map_range_search_test.cpp
        stages/video_params/video_params_stage_test.cpp
        stages/dropout_correct/dropout_correct_stage_test.cpp
        stages/dropout_map/dropout_map_stage_test.cpp
        stages/source_align/source_align_stage_test.cpp
        stages/mask_line/mask_line_stage_test.cpp
        stages/efm_audio_decode/efm_audio_decode_stage_test.cpp
        stages/audio_import/audio_import_stage_test.cpp
        stages/audio_channel_map/audio_channel_map_stage_test.cpp
        stages/audio_align/audio_align_stage_test.cpp
)

orc_add_core_unit_tests(
        orc-core-unit-tests-sinks
        "unit;sinks"

        stages/video_sink/componentframe_test.cpp
        stages/video_sink/vectorscope_analysis_test.cpp
        stages/video_sink/video_sink_stage_defaults_test.cpp
        stages/video_sink/video_sink_stage_safety_test.cpp
        stages/video_sink/comb_test.cpp
        stages/video_sink/video_sink_stage_test.cpp
        stages/video_sink/audio_pair_selection_test.cpp
        stages/video_sink/audio_sample_feed_test.cpp
        stages/video_sink/monodecoder_test.cpp
        stages/video_sink/ntsc_pal_decoder_wrapper_test.cpp
        stages/video_sink/palcolour_test.cpp
        stages/video_sink/sourcefield_test.cpp
        stages/daphne_vbi_sink/daphne_vbi_writer_util_test.cpp
        stages/daphne_vbi_sink/daphne_vbi_sink_stage_deps_test.cpp
        stages/daphne_vbi_sink/daphne_vbi_sink_stage_test.cpp
        stages/audio_sink/audio_sink_stage_test.cpp
        stages/audio_sink/audio_sink_stage_deps_test.cpp
        stages/ac3rf_sink/ac3rf_sink_stage_test.cpp
        stages/cc_sink/cc_sink_stage_test.cpp
        stages/ld_sink/ld_sink_stage_test.cpp
        stages/ld_sink/ld_sink_stage_deps_test.cpp
        stages/efm_sink/efm_sink_stage_test.cpp
        stages/raw_efm_sink/raw_efm_sink_stage_test.cpp
        stages/raw_efm_sink/raw_efm_sink_stage_deps_test.cpp
        stages/cvbs_sink/cvbs_sink_stage_test.cpp
        stages/dropout_analysis_sink/dropout_analysis_sink_stage_test.cpp
        stages/snr_analysis_sink/snr_analysis_sink_stage_test.cpp
        stages/burst_level_analysis_sink/burst_level_analysis_sink_stage_test.cpp
)

orc_add_core_unit_tests(
        orc-core-unit-tests-contracts
        "unit;contracts"

        contracts/public_stage_contract_test.cpp
        contracts/stage_registry_contract_test.cpp
        contracts/project_to_dag_contract_test.cpp
        contracts/plugin_safe_call_test.cpp
        contracts/video_frame_representation_wrapper_contract_test.cpp
        contracts/audio_channel_pair_contract_test.cpp
        contracts/observation_service_accessor_test.cpp
        contracts/observation_service_identity_contract_test.cpp
)

# Presenter-layer unit tests. These link orc-presenters in addition to the
# core/plugin libraries provided by orc_add_core_unit_tests().
orc_add_core_unit_tests(
        orc-core-unit-tests-presenters
        "unit;presenters"

        presenters/project_presenter_validation_test.cpp
)
target_link_libraries(orc-core-unit-tests-presenters orc-presenters)

# ---------------------------------------------------------------------------
# Test-fixture plugin shared libraries for loader version rejection tests
# ---------------------------------------------------------------------------
set(ORC_TEST_FIXTURE_PLUGIN_BUILD_DIR "${CMAKE_BINARY_DIR}/lib/orc-test-fixture-plugins")

foreach(_fixture_name bad_abi_version bad_api_version abi_v4 bad_toolchain_tag
        compatible)
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
        orc-test-fixture-bad_api_version
        orc-test-fixture-abi_v4
        orc-test-fixture-bad_toolchain_tag
        orc-test-fixture-compatible)
gtest_discover_tests(orc-core-unit-tests-loader-rejection
        DISCOVERY_MODE POST_BUILD
        DISCOVERY_TIMEOUT 30
        PROPERTIES LABELS "unit\;contracts")

orc_add_core_unit_tests(
        orc-core-unit-tests-plugin-keepalive
        "unit;contracts"

        contracts/stage_plugin_keepalive_test.cpp
)

# Q-8: 50/15 us audio de-emphasis filter unit test. Links the shared EFM decode
# library directly, as the DSP class is internal to orc-efm-decode and not
# reachable through orc-core (orc-efm-decode's PUBLIC include dirs bring in the
# decoder/efm-lib headers).
add_executable(orc-core-unit-tests-efm-deemphasis
        stages/efm_common/audio_deemphasis_test.cpp)
target_link_libraries(orc-core-unit-tests-efm-deemphasis
        orc-efm-decode
        GTest::gmock_main)
gtest_discover_tests(orc-core-unit-tests-efm-deemphasis
        DISCOVERY_MODE POST_BUILD
        DISCOVERY_TIMEOUT 30
        PROPERTIES LABELS "unit\;sinks")

# P-9: bounded blocking queue used to hand F2 sections from the EFM front-end
# thread to the back-end thread. Links the shared EFM decode library directly
# (the queue header is internal to orc-efm-decode, as with the de-emphasis DSP).
add_executable(orc-core-unit-tests-efm-bounded-queue
        stages/efm_common/bounded_queue_test.cpp)
target_link_libraries(orc-core-unit-tests-efm-bounded-queue
        orc-efm-decode
        GTest::gmock_main)
gtest_discover_tests(orc-core-unit-tests-efm-bounded-queue
        DISCOVERY_MODE POST_BUILD
        DISCOVERY_TIMEOUT 30
        PROPERTIES LABELS "unit\;sinks")

orc_add_core_unit_tests(
        orc-core-unit-tests-metadata
        "unit"

        metadata/video_system_parsing_test.cpp
        metadata/project_plugin_metadata_test.cpp
        metadata/stage_plugin_registry_test.cpp
        metadata/stage_registry_trust_test.cpp
        metadata/plugin_remote_loader_test.cpp
        metadata/plugin_artifact_name_test.cpp
        metadata/plugin_index_client_test.cpp
        metadata/sha256_hash_test.cpp
        metadata/skeleton_plugin_registry_test.cpp
        metadata/dropout_util_test.cpp
)

# Phase 11: project format version 2.0 enforcement tests (label: unit)
orc_add_core_unit_tests(
        orc-core-unit-tests-project-format
        "unit"

        project/project_format_test.cpp
)

# Phase 1: core type and signal constant tests (label: unit)
orc_add_core_unit_tests(
        orc-core-unit-tests-types
        "unit"

        types/frame_id_test.cpp
        types/cvbs_signal_constants_test.cpp
        types/line_numbering_test.cpp
        types/frame_numbering_test.cpp
        types/amplitude_conversion_test.cpp
        types/lru_cache_test.cpp
)

orc_add_core_unit_tests(
        orc-core-unit-tests-observers
        "unit"

        observers/colour_frame_phase_observer_test.cpp
        observers/core_observation_service_test.cpp
)

        orc_add_core_unit_tests(
                orc-core-unit-tests-preview-foundation
                "unit;preview"

                preview_types/preview_types_test.cpp
                preview_types/preview_carrier_conversion_test.cpp
                preview_types/preview_helpers_test.cpp
                preview_types/preview_view_registry_test.cpp
        )