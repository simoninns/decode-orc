/*
 * File:        orc_stage_api.h
 * Module:      decode-orc Plugin SDK
 * Purpose:     Stage API — stable types and interfaces for stage plugin
 * implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 *
 * STABILITY: PUBLIC — This header is part of the stable plugin SDK.
 *            All types exposed here are subject to the plugin_api_version
 *            compatibility guarantee (see orc_plugin_abi.h).
 *
 * USAGE:
 *   Plugin implementors should include <orc/plugin/orc_plugin_sdk.h> (the
 *   umbrella header) rather than including this file directly.
 *
 * WHAT THIS HEADER PROVIDES:
 *   - ParameterizedStage mixin (optional; stages that expose configurable
 * parameters)
 *   - TriggerableStage mixin (optional; sink stages that can be triggered to
 * run)
 *   - NodeTypeInfo, NodeType, VideoFormatCompatibility, SinkCategory
 *   - ParameterValue, ParameterType, ParameterConstraints, ParameterDescriptor
 *   - VideoSystem, SourceType (format/source enums used in parameter
 * descriptors)
 *
 * WHAT THIS HEADER DOES NOT PROVIDE:
 *   - DAGStage base class — obtain this from your stage-specific implementation
 *     header, which inherits from DAGStage and includes the full stage.h chain.
 *   - Internal host infrastructure (StageRegistry, StagePluginLoader, etc.).
 *
 * INCLUDE PATH REQUIREMENTS:
 *   The orc-plugin-sdk CMake target (or the installed find_package target) sets
 *   up all required include directories automatically. Plugins should NOT add
 *   manual include_directories() calls for orc-core or orc-common headers.
 */

#pragma once

// Parameter schema types: ParameterValue, ParameterType, ParameterConstraints,
// ParameterDescriptor, ParameterDependency.
#include <parameter_types.h>

// Common enumeration types: VideoSystem, SourceType.
#include <common_types.h>

// Node metadata types: NodeTypeInfo, NodeType, VideoFormatCompatibility,
// SinkCategory.
#include <node_type.h>

// ParameterizedStage mixin interface: get_parameter_descriptors(),
// get_parameters(), set_parameters(). Include this when your stage exposes
// configurable parameters.
#include <stage_parameter.h>

// TriggerableStage mixin interface: trigger(), get_trigger_status(), etc.
// Include-guarded; only defines the interface; no MVP enforcement on plugin
// builds.
#include <triggerable_stage.h>
