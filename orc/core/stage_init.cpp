/*
 * File:        stage_init.cpp
 * Module:      orc-core
 * Purpose:     Stage initialization
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


namespace orc {

// Forward declarations of force-link functions from each stage
void force_link_LDPALSourceStage();
void force_link_LDNTSCSourceStage();
void force_link_DropoutCorrectStage();
void force_link_DropoutMapStage();
void force_link_FieldInvertStage();
void force_link_FieldMapStage();
void force_link_SourceAlignStage();
void force_link_LDSinkStage();
void force_link_StackerStage();
void force_link_ChromaSinkStage();
void force_link_AudioSinkStage();
void force_link_EFMSinkStage();
void force_link_VideoParamsStage();

/**
 * @brief Force linking of all stage object files
 * 
 * This function ensures the linker includes all stage object files by calling
 * a dummy function from each stage .cpp file. This is necessary because the
 * ORC_REGISTER_STAGE macro creates static initialization objects that only
 * execute if their object files are linked into the final binary.
 * 
 * Without these explicit references, the linker would strip out the stage
 * object files as "unused", preventing their registration code from running.
 * 
 * This must be called before any stage lookups occur (typically during
 * application initialization).
 */
void force_stage_linking() {
    // Call dummy functions to force linker to include stage object files
    // This ensures the ORC_REGISTER_STAGE static initializers execute
    force_link_LDPALSourceStage();
    force_link_LDNTSCSourceStage();
    force_link_DropoutCorrectStage();
    force_link_DropoutMapStage();
    force_link_FieldInvertStage();
    force_link_FieldMapStage();
    force_link_SourceAlignStage();
    force_link_LDSinkStage();
    force_link_StackerStage();
    force_link_ChromaSinkStage();
    force_link_AudioSinkStage();
    force_link_EFMSinkStage();
    force_link_VideoParamsStage();
    force_link_VideoParamsStage();
}

} // namespace orc
