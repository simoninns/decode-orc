/*
 * File:        quick_project_planner.h
 * Module:      orc-gui
 * Purpose:     Pure decision logic for the quick-project downstream graph
 *              (which transforms and sink branches to wire for a source)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <string>
#include <vector>

namespace orc::gui {

// Deterministic plan for the downstream half of a quick project: the transform
// stages spliced between the source and the video sink, plus any parallel sink
// branches. Kept free of Qt and filesystem access so it is unit-testable in
// isolation; MainWindow::quickProject executes the plan against the presenter.
struct QuickProjectDownstreamPlan {
  // Transform stage names, in order, forming the video chain
  //   source -> video_transforms[0] -> ... -> video sink
  // Empty means the source feeds the video sink directly.
  std::vector<std::string> video_transforms;
};

// Decide the downstream plan from the two source properties the caller derives
// from metadata and sidecar probing:
//   is_ld_decode    — the source was produced by ld-decode (gets dropout
//                     correction, and can carry EFM digital audio)
//   has_efm_sidecar — an EFM t-value sidecar is present alongside the source
//
// For an ld-decode source with EFM, the EFM audio decode transform is spliced
// after dropout correction (so it decodes the best stream) and the video sink
// embeds the disc's digital audio. No standalone EFM sink is added — the
// inclusion of the decode transform is enough.
QuickProjectDownstreamPlan plan_quick_project_downstream(bool is_ld_decode,
                                                         bool has_efm_sidecar);

}  // namespace orc::gui
