/*
 * File:        vectorscope_data.h
 * Module:      orc-core
 * Purpose:     Vectorscope data structures (uses public API types)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_ANALYSIS_VECTORSCOPE_DATA_H
#define ORC_CORE_ANALYSIS_VECTORSCOPE_DATA_H

#if defined(ORC_GUI_BUILD)
#error "GUI code cannot include core/analysis/vectorscope/vectorscope_data.h. Use VectorscopePresenter or public API instead."
#endif

// Core uses public API types
#include <orc_vectorscope.h>

namespace orc {

// Type aliases for convenience in core code
using UVSample = orc::public_api::UVSample;
using VectorscopeData = orc::public_api::VectorscopeData;
using orc::public_api::rgb_to_uv;

} // namespace orc

#endif // ORC_CORE_ANALYSIS_VECTORSCOPE_DATA_H
