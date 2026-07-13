/*
 * File:        quick_project_planner.cpp
 * Module:      orc-gui
 * Purpose:     Pure decision logic for the quick-project downstream graph
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "quick_project_planner.h"

namespace orc::gui {

QuickProjectDownstreamPlan plan_quick_project_downstream(bool is_ld_decode,
                                                         bool has_efm_sidecar) {
  QuickProjectDownstreamPlan plan;

  if (is_ld_decode) {
    // ld-decode sources always get dropout correction before the video sink.
    plan.video_transforms.push_back("dropout_correct");

    if (has_efm_sidecar) {
      // Splice EFM audio decode after dropout correction so the video sink
      // embeds the disc's digital audio. The inclusion alone is enough — no
      // standalone EFM sink is added.
      plan.video_transforms.push_back("efm_audio_decode");
    }
  }

  return plan;
}

}  // namespace orc::gui
