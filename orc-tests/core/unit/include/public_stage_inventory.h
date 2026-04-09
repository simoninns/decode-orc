/*
 * File:        public_stage_inventory.h
 * Module:      orc-core-tests
 * Purpose:     Shared inventory of public core stages for cross-stage contracts
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../../../orc/core/factories.h"
#include "../../../orc/core/stages/audio_sink/audio_sink_stage.h"
#include "../../../orc/core/stages/burst_level_analysis_sink/burst_level_analysis_sink_stage.h"
#include "../../../orc/core/stages/cc_sink/cc_sink_stage.h"
#include "../../../orc/core/stages/chroma_sink/chroma_sink_stage.h"
#include "../../../orc/core/stages/chroma_sink/ffmpeg_video_sink_stage.h"
#include "../../../orc/core/stages/chroma_sink/raw_video_sink_stage.h"
#include "../../../orc/core/stages/daphne_vbi_sink/daphne_vbi_sink_stage.h"
#include "../../../orc/core/stages/dropout_analysis_sink/dropout_analysis_sink_stage.h"
#include "../../../orc/core/stages/dropout_correct/dropout_correct_stage.h"
#include "../../../orc/core/stages/dropout_map/dropout_map_stage.h"
#include "../../../orc/core/stages/efm_sink/efm_sink_stage.h"
#include "../../../orc/core/stages/field_invert/field_invert_stage.h"
#include "../../../orc/core/stages/field_map/field_map_stage.h"
#include "../../../orc/core/stages/hackdac_sink/hackdac_sink_stage.h"
#include "../../../orc/core/stages/ld_sink/ld_sink_stage.h"
#include "../../../orc/core/stages/mask_line/mask_line_stage.h"
#include "../../../orc/core/stages/nn_ntsc_chroma_sink/nn_ntsc_chroma_sink_stage.h"
#include "../../../orc/core/stages/ntsc_comp_source/ntsc_comp_source_stage.h"
#include "../../../orc/core/stages/ntsc_yc_source/ntsc_yc_source_stage.h"
#include "../../../orc/core/stages/pal_comp_source/pal_comp_source_stage.h"
#include "../../../orc/core/stages/pal_yc_source/pal_yc_source_stage.h"
#include "../../../orc/core/stages/raw_efm_sink/efm_sink_stage.h"
#include "../../../orc/core/stages/snr_analysis_sink/snr_analysis_sink_stage.h"
#include "../../../orc/core/stages/source_align/source_align_stage.h"
#include "../../../orc/core/stages/stacker/stacker_stage.h"
#include "../../../orc/core/stages/video_params/video_params_stage.h"

namespace orc_unit_test
{
    enum class PublicStageFamily {
        Source,
        Transform,
        Sink,
    };

    struct PublicStageSpec {
        std::string inventory_id;
        PublicStageFamily family;
        bool registry_backed;
        std::function<orc::DAGStagePtr()> create;
    };

    inline const std::vector<PublicStageSpec>& public_stage_specs()
    {
        static const std::vector<PublicStageSpec> specs = {
            {"ntsc_comp_source", PublicStageFamily::Source, true, [] { return std::make_shared<orc::NTSCCompSourceStage>(); }},
            {"ntsc_yc_source", PublicStageFamily::Source, true, [] { return std::make_shared<orc::NTSCYCSourceStage>(); }},
            {"pal_comp_source", PublicStageFamily::Source, true, [] { return std::make_shared<orc::PALCompSourceStage>(); }},
            {"pal_yc_source", PublicStageFamily::Source, true, [] { return std::make_shared<orc::PALYCSourceStage>(); }},
            {"stacker", PublicStageFamily::Transform, true, [] { return std::make_shared<orc::StackerStage>(); }},
            {"field_invert", PublicStageFamily::Transform, true, [] { return std::make_shared<orc::FieldInvertStage>(); }},
            {"field_map", PublicStageFamily::Transform, true, [] { return std::make_shared<orc::FieldMapStage>(); }},
            {"video_params", PublicStageFamily::Transform, true, [] { return std::make_shared<orc::VideoParamsStage>(); }},
            {"dropout_correct", PublicStageFamily::Transform, true, [] { return std::make_shared<orc::DropoutCorrectStage>(); }},
            {"dropout_map", PublicStageFamily::Transform, true, [] { return std::make_shared<orc::DropoutMapStage>(); }},
            {"source_align", PublicStageFamily::Transform, true, [] { return std::make_shared<orc::SourceAlignStage>(); }},
            {"mask_line", PublicStageFamily::Transform, true, [] { return std::make_shared<orc::MaskLineStage>(); }},
            {"chroma_sink", PublicStageFamily::Sink, false, [] { return std::make_shared<orc::ChromaSinkStage>(); }},
            {"ffmpeg_video_sink", PublicStageFamily::Sink, true, [] { return std::make_shared<orc::FFmpegVideoSinkStage>(); }},
            {"raw_video_sink", PublicStageFamily::Sink, true, [] { return std::make_shared<orc::RawVideoSinkStage>(); }},
            {"daphne_vbi_sink", PublicStageFamily::Sink, true, [] { return std::make_shared<orc::DaphneVBISinkStage>(orc::Factories::instance()); }},
            {"audio_sink", PublicStageFamily::Sink, true, [] { return std::make_shared<orc::AudioSinkStage>(); }},
            {"cc_sink", PublicStageFamily::Sink, true, [] { return std::make_shared<orc::CCSinkStage>(); }},
            {"ld_sink", PublicStageFamily::Sink, true, [] { return std::make_shared<orc::LDSinkStage>(orc::Factories::instance()); }},
            {"efm_sink", PublicStageFamily::Sink, true, [] { return std::make_shared<orc::EFMSinkStage>(); }},
            {"raw_efm_sink", PublicStageFamily::Sink, true, [] { return std::make_shared<orc::RawEFMSinkStage>(); }},
            {"hackdac_sink", PublicStageFamily::Sink, true, [] { return std::make_shared<orc::HackdacSinkStage>(); }},
            {"dropout_analysis_sink", PublicStageFamily::Sink, true, [] { return std::make_shared<orc::DropoutAnalysisSinkStage>(); }},
            {"snr_analysis_sink", PublicStageFamily::Sink, true, [] { return std::make_shared<orc::SNRAnalysisSinkStage>(); }},
            {"burst_level_analysis_sink", PublicStageFamily::Sink, true, [] { return std::make_shared<orc::BurstLevelAnalysisSinkStage>(); }},
            {"nn_ntsc_chroma_sink", PublicStageFamily::Sink, true, [] { return std::make_shared<orc::NnNtscChromaSinkStage>(); }},
        };

        return specs;
    }
} // namespace orc_unit_test