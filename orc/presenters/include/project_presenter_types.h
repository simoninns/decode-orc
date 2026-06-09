/*
 * File:        project_presenter_types.h
 * Module:      orc-presenters
 * Purpose:     Shared project presenter view types
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <node_id.h>
#include <node_type.h>

#include <functional>
#include <string>

namespace orc::presenters {

/**
 * @brief Video format enumeration for GUI use
 */
enum class VideoFormat { NTSC, PAL, Unknown };

/**
 * @brief Source type enumeration for GUI use
 */
enum class SourceType { Composite, YC, Unknown };

enum class PluginDiagnosticSeverity { Info, Warning, Error };

struct PluginDiagnosticInfo {
  PluginDiagnosticSeverity severity = PluginDiagnosticSeverity::Info;
  std::string path;
  std::string message;
};

struct LoadedPluginInfo {
  std::string path;
  std::string plugin_id;
  std::string plugin_version;
  std::string license_spdx;
  bool is_core_plugin = false;
  std::vector<std::string> registered_stage_names;
};

struct PluginRegistryEntryInfo {
  std::string plugin_id;
  std::string plugin_version;
  std::string path;
  std::string source_repo_url;
  std::string artifact_source = "local_path";
  std::string release_asset_url;
  std::string release_tag;
  std::string release_asset_name;
  std::string target_platform;
  std::string local_dev_path;
  bool enabled = true;
  std::string trust_state = "untrusted";
  std::string license_spdx;
  bool is_core_plugin = false;
  uint32_t required_host_abi = 0;
  bool is_loaded = false;
  bool path_exists = false;
};

struct PluginRegistryInfo {
  std::string registry_path;
  std::vector<PluginRegistryEntryInfo> entries;
};

struct PluginRegistryMutationResult {
  bool success = false;
  std::string error_message;
};

/**
 * @brief Information about a stage available in the registry
 */
struct StageInfo {
  std::string name;          ///< Internal stage name
  std::string display_name;  ///< User-friendly display name
  std::string description;   ///< Stage description
  std::string category;      ///< Add Stage menu category label
  NodeType node_type;        ///< Type of node
  bool is_source;            ///< True if this is a source stage
  bool is_sink;              ///< True if this is a sink stage
  bool is_runtime_plugin_stage =
      false;  ///< True if discovered from runtime plugin loading
  std::string owning_plugin_id;  ///< Plugin id when known
};

/**
 * @brief Information about a node in the project
 */
struct NodeInfo {
  NodeID node_id;              ///< Node identifier
  std::string stage_name;      ///< Stage type name
  std::string label;           ///< User-assigned label
  double x_position;           ///< X position in graph
  double y_position;           ///< Y position in graph
  bool can_remove;             ///< Whether node can be removed
  bool can_trigger;            ///< Whether node can be triggered
  bool can_inspect;            ///< Whether node can be inspected
  std::string remove_reason;   ///< Reason if cannot remove
  std::string trigger_reason;  ///< Reason if cannot trigger
  std::string inspect_reason;  ///< Reason if cannot inspect
};

/**
 * @brief Edge between two nodes
 */
struct EdgeInfo {
  NodeID source_node;  ///< Source node ID
  NodeID target_node;  ///< Target node ID
};

/**
 * @brief Progress callback for batch operations
 */
using ProgressCallback = std::function<void(size_t current, size_t total,
                                            const std::string& message)>;

}  // namespace orc::presenters
