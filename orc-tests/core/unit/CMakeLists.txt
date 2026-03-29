cmake_minimum_required(VERSION 3.20)

project(orc-core-unit-tests)

# GoogleTest requires at least C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(GTest REQUIRED)

# This is already included in the top-level cmakelist so does not need to be included here
#enable_testing()

function(orc_add_core_unit_tests target labels)
    add_executable(${target} ${ARGN})
    target_link_libraries(
            ${target}
            orc-core
            GTest::gmock_main
    )
    gtest_discover_tests(${target} PROPERTIES LABELS ${labels})
endfunction()

include(GoogleTest)

orc_add_core_unit_tests(
        orc-core-unit-tests-sources
        "unit;sources"

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
        stages/chroma_sink/pal_m_integration_test.cpp
        stages/chroma_sink/raw_video_sink_stage_test.cpp
        stages/chroma_sink/sourcefield_test.cpp
        stages/daphne_vbi_sink/daphne_vbi_writer_util_test.cpp
        stages/daphne_vbi_sink/daphne_vbi_sink_stage_deps_test.cpp
        stages/daphne_vbi_sink/daphne_vbi_sink_stage_test.cpp
        stages/audio_sink/audio_sink_stage_test.cpp
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
        contracts/project_to_dag_contract_test.cpp
)

orc_add_core_unit_tests(
        orc-core-unit-tests-metadata
        "unit"

        metadata/video_system_parsing_test.cpp
)

        orc_add_core_unit_tests(
                orc-core-unit-tests-preview-foundation
                "unit;preview"

                preview_types/preview_types_test.cpp
                preview_types/preview_carrier_conversion_test.cpp
                preview_types/preview_view_registry_test.cpp
                preview_types/preview_helpers_yc_combine_test.cpp
        )