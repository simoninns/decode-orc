/*
 * File:        stage_init.cpp
 * Module:      orc-core
 * Purpose:     Stage initialization
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#include "stages/ld_pal_source/ld_pal_source_stage.h"
#include "stages/ld_ntsc_source/ld_ntsc_source_stage.h"
#include "stages/ld_sink/ld_sink_stage.h"
#include "stages/dropout_correct/dropout_correct_stage.h"
#include "stages/passthrough/passthrough_stage.h"
#include "stages/passthrough_splitter/passthrough_splitter_stage.h"
#include "stages/passthrough_merger/passthrough_merger_stage.h"
#include "stages/passthrough_complex/passthrough_complex_stage.h"
#include "stages/overwrite/overwrite_stage.h"
#include "stages/field_map/field_map_stage.h"
#include "stages/stacker/stacker_stage.h"
#include <iostream>

namespace orc {

/**
 * @brief Force linking of all stage object files
 * 
 * This function creates dummy instances to force the linker to include
 * all stage object files, which ensures their static registrations execute.
 * This must be called before any stage lookups occur.
 */
void force_stage_linking() {
    // Create dummy shared_ptr to force vtable instantiation
    // This ensures the object files are linked
    [[maybe_unused]] auto dummy1 = std::make_shared<LDPALSourceStage>();
    [[maybe_unused]] auto dummy2 = std::make_shared<LDNTSCSourceStage>();
    [[maybe_unused]] auto dummy3 = std::make_shared<DropoutCorrectStage>();
    [[maybe_unused]] auto dummy4 = std::make_shared<PassthroughStage>();
    [[maybe_unused]] auto dummy5 = std::make_shared<PassthroughSplitterStage>();
    [[maybe_unused]] auto dummy6 = std::make_shared<PassthroughMergerStage>();
    [[maybe_unused]] auto dummy7 = std::make_shared<PassthroughComplexStage>();
    [[maybe_unused]] auto dummy8 = std::make_shared<OverwriteStage>();
    [[maybe_unused]] auto dummy9 = std::make_shared<FieldMapStage>();
    [[maybe_unused]] auto dummy10 = std::make_shared<LDSinkStage>();
    [[maybe_unused]] auto dummy11 = std::make_shared<StackerStage>();
}

} // namespace orc
