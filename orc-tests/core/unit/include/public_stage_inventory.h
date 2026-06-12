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

// Stage instances are obtained through the runtime registry, not by including
// concrete stage implementation headers. This keeps contract tests aligned with
// the plugin-first architecture and prevents coupling to internal stage
// classes.
#include "../../../orc/core/include/stage_registry.h"

namespace orc_unit_test {
enum class PublicStageFamily {
  Source,
  Transform,
  Sink,
};

struct PublicStageSpec {
  std::string inventory_id;
  PublicStageFamily family;
  // registry_backed is always true for all inventory entries.
  // Non-registry stages (e.g. internal chroma_sink) are excluded from the
  // shared inventory; their behaviour is covered by dedicated stage-level
  // tests.
  bool registry_backed;
  // create() goes through StageRegistry so tests exercise the same factory
  // path as the host runtime. No concrete stage classes are instantiated here.
  std::function<orc::DAGStagePtr()> create;
};

inline const std::vector<PublicStageSpec>& public_stage_specs() {
  static const std::vector<PublicStageSpec> specs = {
      {"tbc_source", PublicStageFamily::Source, true,
       [] {
         return orc::StageRegistry::instance().create_stage("tbc_source");
       }},
      {"stacker", PublicStageFamily::Transform, true,
       [] { return orc::StageRegistry::instance().create_stage("stacker"); }},
      {"field_invert", PublicStageFamily::Transform, true,
       [] {
         return orc::StageRegistry::instance().create_stage("field_invert");
       }},
      {"field_map", PublicStageFamily::Transform, true,
       [] { return orc::StageRegistry::instance().create_stage("field_map"); }},
      {"video_params", PublicStageFamily::Transform, true,
       [] {
         return orc::StageRegistry::instance().create_stage("video_params");
       }},
      {"dropout_correct", PublicStageFamily::Transform, true,
       [] {
         return orc::StageRegistry::instance().create_stage("dropout_correct");
       }},
      {"dropout_map", PublicStageFamily::Transform, true,
       [] {
         return orc::StageRegistry::instance().create_stage("dropout_map");
       }},
      {"source_align", PublicStageFamily::Transform, true,
       [] {
         return orc::StageRegistry::instance().create_stage("source_align");
       }},
      {"mask_line", PublicStageFamily::Transform, true,
       [] { return orc::StageRegistry::instance().create_stage("mask_line"); }},
      {"ffmpeg_video_sink", PublicStageFamily::Sink, true,
       [] {
         return orc::StageRegistry::instance().create_stage(
             "ffmpeg_video_sink");
       }},
      {"raw_video_sink", PublicStageFamily::Sink, true,
       [] {
         return orc::StageRegistry::instance().create_stage("raw_video_sink");
       }},
      {"daphne_vbi_sink", PublicStageFamily::Sink, true,
       [] {
         return orc::StageRegistry::instance().create_stage("daphne_vbi_sink");
       }},
      {"AudioSink", PublicStageFamily::Sink, true,
       [] { return orc::StageRegistry::instance().create_stage("AudioSink"); }},
      {"CCSink", PublicStageFamily::Sink, true,
       [] { return orc::StageRegistry::instance().create_stage("CCSink"); }},
      {"ld_sink", PublicStageFamily::Sink, true,
       [] { return orc::StageRegistry::instance().create_stage("ld_sink"); }},
      {"EFMSink", PublicStageFamily::Sink, true,
       [] { return orc::StageRegistry::instance().create_stage("EFMSink"); }},
      {"RawEFMSink", PublicStageFamily::Sink, true,
       [] {
         return orc::StageRegistry::instance().create_stage("RawEFMSink");
       }},
      {"hackdac_sink", PublicStageFamily::Sink, true,
       [] {
         return orc::StageRegistry::instance().create_stage("hackdac_sink");
       }},
      {"dropout_analysis_sink", PublicStageFamily::Sink, true,
       [] {
         return orc::StageRegistry::instance().create_stage(
             "dropout_analysis_sink");
       }},
      {"snr_analysis_sink", PublicStageFamily::Sink, true,
       [] {
         return orc::StageRegistry::instance().create_stage(
             "snr_analysis_sink");
       }},
      {"burst_level_analysis_sink", PublicStageFamily::Sink, true,
       [] {
         return orc::StageRegistry::instance().create_stage(
             "burst_level_analysis_sink");
       }},
  };

  return specs;
}
}  // namespace orc_unit_test