/*
 * File:        bad_abi_version_plugin.cpp
 * Module:      orc-core-tests / test fixtures
 * Purpose:     Minimal plugin that declares a wrong host_abi_version.
 *              Used by stage_plugin_loader_version_rejection_test.cpp to verify
 *              that the loader rejects plugins with a mismatched host ABI.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <orc/plugin/orc_plugin_abi.h>

namespace {

constexpr orc::StagePluginDescriptor kDescriptor{
    "test.bad-abi-version",  // plugin_id
    "0.0.1",                 // plugin_version
    9999u,                   // host_abi_version — intentionally wrong
    orc::kStagePluginApiVersion,
    "GPL-3.0-or-later",      // license_spdx
    false,                   // is_core_plugin
};

} // namespace

ORC_STAGE_PLUGIN_EXPORT const orc::StagePluginDescriptor* orc_get_stage_plugin_descriptor()
{
    return &kDescriptor;
}

ORC_STAGE_PLUGIN_EXPORT bool orc_register_stage_plugin(
    const orc::OrcPluginServices* /*services*/,
    void* /*host_context*/,
    bool(* /*register_stage*/)(void*, const char*, orc::OrcStageFactoryFn),
    const char** /*out_error*/)
{
    return false;
}
