/******************************************************************************
 * stage_init.cpp
 *
 * Force linking of all stage registration units
 *
 * This file ensures that all stage object files are linked into the final
 * binary, which is necessary for their static registration to execute.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#include "stages/tbc_source/tbc_source_stage.h"
#include "stages/dropout_correct/dropout_correct_stage.h"
#include "stages/passthrough/passthrough_stage.h"
#include "stages/passthrough_splitter/passthrough_splitter_stage.h"
#include "stages/passthrough_merger/passthrough_merger_stage.h"
#include "stages/passthrough_complex/passthrough_complex_stage.h"
#include "stages/overwrite/overwrite_stage.h"
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
    [[maybe_unused]] auto dummy1 = std::make_shared<TBCSourceStage>();
    [[maybe_unused]] auto dummy2 = std::make_shared<DropoutCorrectStage>();
    [[maybe_unused]] auto dummy3 = std::make_shared<PassthroughStage>();
    [[maybe_unused]] auto dummy4 = std::make_shared<PassthroughSplitterStage>();
    [[maybe_unused]] auto dummy5 = std::make_shared<PassthroughMergerStage>();
    [[maybe_unused]] auto dummy6 = std::make_shared<PassthroughComplexStage>();
    [[maybe_unused]] auto dummy7 = std::make_shared<OverwriteStage>();
}

} // namespace orc
