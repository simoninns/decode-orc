/*
 * File:        project_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     Project management presenter implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "project_presenter.h"

#include <orc/stage/common_types.h>
#include <orc/stage/logging.h>
#include <orc/stage/orc_source_parameters.h>
#include <orc/stage/stage_parameter.h>
#include <orc/stage/triggerable_stage.h>
#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <stdexcept>

#include "../core/include/plugin_remote_loader.h"
#include "../core/include/project.h"
#include "../core/include/project_to_dag.h"
#include "../core/include/stage_plugin_registry.h"
#include "../core/include/stage_registry.h"

namespace orc::presenters {

// === Helper Functions ===

static std::string current_platform_plugin_extension() {
#if defined(_WIN32)
  return ".dll";
#elif defined(__APPLE__)
  return ".dylib";
#else
  return ".so";
#endif
}

static bool has_case_insensitive_suffix(const std::string& value,
                                        const std::string& suffix) {
  if (value.size() < suffix.size()) {
    return false;
  }

  return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin(),
                    [](unsigned char a, unsigned char b) {
                      return std::tolower(a) == std::tolower(b);
                    });
}

static VideoSystem toVideoSystem(VideoFormat format) {
  switch (format) {
    case VideoFormat::NTSC:
      return VideoSystem::NTSC;
    case VideoFormat::PAL:
      return VideoSystem::PAL;
    case VideoFormat::PAL_M:
      return VideoSystem::PAL_M;
    case VideoFormat::Unknown:
      return VideoSystem::Unknown;
  }
  return VideoSystem::Unknown;
}

static VideoFormat fromVideoSystem(VideoSystem system) {
  switch (system) {
    case VideoSystem::NTSC:
      return VideoFormat::NTSC;
    case VideoSystem::PAL:
      return VideoFormat::PAL;
    case VideoSystem::PAL_M:
      return VideoFormat::PAL_M;
    case VideoSystem::Unknown:
      return VideoFormat::Unknown;
  }
  return VideoFormat::Unknown;
}

static orc::SourceType toSourceType(SourceType type) {
  switch (type) {
    case SourceType::Composite:
      return orc::SourceType::Composite;
    case SourceType::YC:
      return orc::SourceType::YC;
    case SourceType::Unknown:
      return orc::SourceType::Unknown;
  }
  return orc::SourceType::Unknown;
}

static SourceType fromSourceType(orc::SourceType type) {
  switch (type) {
    case orc::SourceType::Composite:
      return SourceType::Composite;
    case orc::SourceType::YC:
      return SourceType::YC;
    case orc::SourceType::Unknown:
      return SourceType::Unknown;
  }
  return SourceType::Unknown;
}

static PluginDiagnosticSeverity fromPluginDiagnosticSeverity(
    orc::StagePluginDiagnosticSeverity severity) {
  switch (severity) {
    case orc::StagePluginDiagnosticSeverity::Info:
      return PluginDiagnosticSeverity::Info;
    case orc::StagePluginDiagnosticSeverity::Warning:
      return PluginDiagnosticSeverity::Warning;
    case orc::StagePluginDiagnosticSeverity::Error:
      return PluginDiagnosticSeverity::Error;
  }
  return PluginDiagnosticSeverity::Info;
}

// === Static Utility Methods ===

std::optional<orc::SourceParameters> ProjectPresenter::readVideoParameters(
    const std::string& metadata_path) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(metadata_path.c_str(), &db, SQLITE_OPEN_READONLY,
                           nullptr);
  if (rc != SQLITE_OK) {
    ORC_LOG_WARN("Failed to open metadata file: {}", metadata_path);
    if (db) sqlite3_close(db);
    return std::nullopt;
  }

  sqlite3_stmt* stmt = nullptr;
  rc = sqlite3_prepare_v2(db, "SELECT system FROM capture WHERE capture_id = 1",
                          -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    ORC_LOG_WARN("Failed to query video system from: {}", metadata_path);
    sqlite3_close(db);
    return std::nullopt;
  }

  std::optional<orc::SourceParameters> result;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* sys_text =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (sys_text) {
      orc::SourceParameters sp;
      sp.system = orc::video_system_from_string(sys_text);
      result = sp;
    }
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return result;
}

std::optional<orc::SourceParameters> ProjectPresenter::readCVBSVideoParameters(
    const std::string& meta_path) {
  sqlite3* db = nullptr;
  int rc =
      sqlite3_open_v2(meta_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  if (rc != SQLITE_OK) {
    ORC_LOG_WARN("Failed to open CVBS metadata file: {}", meta_path);
    if (db) sqlite3_close(db);
    return std::nullopt;
  }

  sqlite3_stmt* stmt = nullptr;
  rc = sqlite3_prepare_v2(
      db, "SELECT preset FROM cvbs_file ORDER BY cvbs_file_id LIMIT 1", -1,
      &stmt, nullptr);
  if (rc != SQLITE_OK) {
    ORC_LOG_WARN("Failed to query preset from CVBS metadata: {}", meta_path);
    sqlite3_close(db);
    return std::nullopt;
  }

  std::optional<orc::SourceParameters> result;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* preset_text =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (preset_text) {
      orc::SourceParameters sp;
      sp.system = orc::video_system_from_string(preset_text);
      result = sp;
    }
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return result;
}

// === ProjectPresenter Implementation ===

ProjectPresenter::ProjectPresenter()
    : project_(std::make_unique<orc::Project>(
          orc::project_io::create_empty_project(""))),
      project_path_(),
      is_modified_(false),
      dag_(nullptr) {
  if (project_) {
    project_->clear_modified_flag();
  }
  ORC_LOG_DEBUG("ProjectPresenter default constructor: project = {}",
                static_cast<void*>(project_.get()));
}

ProjectPresenter::ProjectPresenter(void* project_handle)
    : project_(nullptr)  // Don't own the project
      ,
      external_project_(static_cast<orc::Project*>(project_handle)),
      project_path_(),
      is_modified_(false),
      dag_(nullptr) {}

ProjectPresenter::ProjectPresenter(const std::string& project_path)
    : project_(nullptr),
      project_path_(project_path),
      is_modified_(false),
      dag_(nullptr) {
  project_ = std::make_unique<orc::Project>(
      orc::project_io::load_project(project_path));
}

ProjectPresenter::~ProjectPresenter() = default;

ProjectPresenter::ProjectPresenter(ProjectPresenter&& other) noexcept
    : project_(std::move(other.project_)),
      project_path_(std::move(other.project_path_)),
      is_modified_(other.is_modified_),
      dag_(std::move(other.dag_)) {
  ORC_LOG_DEBUG("ProjectPresenter move constructor: project = {}",
                static_cast<void*>(project_.get()));
}

ProjectPresenter& ProjectPresenter::operator=(
    ProjectPresenter&& other) noexcept {
  if (this != &other) {
    project_ = std::move(other.project_);
    external_project_ = other.external_project_;
    other.external_project_ = nullptr;
    project_path_ = std::move(other.project_path_);
    is_modified_ = other.is_modified_;
    dag_ = std::move(other.dag_);
  }
  return *this;
}

bool ProjectPresenter::createQuickProject(
    VideoFormat format, SourceType source,
    const std::vector<std::string>& input_files) {
  if (input_files.empty()) {
    return false;
  }

  // Create empty project with format
  project_ = std::make_unique<Project>(orc::project_io::create_empty_project(
      "Quick Project", toVideoSystem(format), toSourceType(source)));

  // Add source nodes for each input file
  double y_offset = 0.0;
  std::vector<NodeID> source_nodes;

  for (const auto& file : input_files) {
    orc::NodeID source_id =
        orc::project_io::add_node(*getProject(), "tbc-source", 0.0, y_offset);

    // Set the TBC path parameter
    std::map<std::string, orc::ParameterValue> params;
    params["tbc_path"] = file;
    orc::project_io::set_node_parameters(*getProject(), source_id, params);

    source_nodes.push_back(source_id);
    y_offset += 100.0;
  }

  // Add appropriate decoder based on format and source
  orc::NodeID decoder_id;
  if (format == VideoFormat::NTSC) {
    if (source == SourceType::Composite) {
      decoder_id = orc::project_io::add_node(*getProject(), "ntsc-comb-decode",
                                             200.0, 50.0);
    } else {
      decoder_id = orc::project_io::add_node(*getProject(), "ntsc-yc-decode",
                                             200.0, 50.0);
    }
  } else if (format == VideoFormat::PAL) {
    if (source == SourceType::Composite) {
      decoder_id = orc::project_io::add_node(*getProject(), "pal-transform-2d",
                                             200.0, 50.0);
    } else {
      decoder_id = orc::project_io::add_node(*getProject(), "pal-yc-decode",
                                             200.0, 50.0);
    }
  } else {
    return false;
  }

  // Connect first source to decoder
  if (!source_nodes.empty()) {
    orc::project_io::add_edge(*getProject(), source_nodes[0], decoder_id);
  }

  // Add a preview sink
  orc::NodeID preview_id =
      orc::project_io::add_node(*getProject(), "preview-sink", 400.0, 50.0);
  orc::project_io::add_edge(*getProject(), decoder_id, preview_id);

  is_modified_ = true;
  return true;
}

bool ProjectPresenter::loadProject(const std::string& project_path) {
  try {
    project_ =
        std::make_unique<Project>(orc::project_io::load_project(project_path));
    project_path_ = project_path;
    is_modified_ = false;
    return true;
  } catch (const std::exception&) {
    throw;
  }
}

bool ProjectPresenter::saveProject(const std::string& project_path) {
  try {
    orc::project_io::save_project(*getProject(), project_path);
    project_path_ = project_path;
    is_modified_ = false;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

void ProjectPresenter::clearProject() {
  orc::project_io::clear_project(*getProject());
  is_modified_ = true;
}

bool ProjectPresenter::isModified() const {
  if (!project_) {
    return is_modified_;
  }
  return is_modified_ || project_->has_unsaved_changes();
}

void ProjectPresenter::clearModifiedFlag() {
  is_modified_ = false;
  if (project_) {
    project_->clear_modified_flag();
  }
}

std::string ProjectPresenter::getProjectPath() const { return project_path_; }

std::string ProjectPresenter::getProjectName() const {
  if (!getProject()) {
    return "";
  }
  return getProject()->get_name();
}

void ProjectPresenter::setProjectName(const std::string& name) {
  orc::project_io::set_project_name(*getProject(), name);
  is_modified_ = true;
}

std::string ProjectPresenter::getProjectDescription() const {
  if (!getProject()) {
    return "";
  }
  return getProject()->get_description();
}

void ProjectPresenter::setProjectDescription(const std::string& description) {
  orc::project_io::set_project_description(*getProject(), description);
  is_modified_ = true;
}

VideoFormat ProjectPresenter::getVideoFormat() const {
  if (!getProject()) {
    return VideoFormat::Unknown;
  }
  return fromVideoSystem(getProject()->get_video_format());
}

void ProjectPresenter::setVideoFormat(VideoFormat format) {
  orc::project_io::set_video_format(*getProject(), toVideoSystem(format));
  is_modified_ = true;
}

SourceType ProjectPresenter::getSourceType() const {
  if (!getProject()) {
    return SourceType::Unknown;
  }
  return fromSourceType(getProject()->get_source_format());
}

void ProjectPresenter::setSourceType(SourceType source) {
  orc::project_io::set_source_format(*getProject(), toSourceType(source));
  is_modified_ = true;
}

orc::AmplitudeDisplayUnit ProjectPresenter::getAmplitudeUnit() const {
  if (!getProject()) {
    return orc::AmplitudeDisplayUnit::IRE;
  }
  return getProject()->get_amplitude_unit();
}

void ProjectPresenter::setAmplitudeUnit(orc::AmplitudeDisplayUnit unit) {
  orc::project_io::set_amplitude_unit(*getProject(), unit);
  is_modified_ = true;
}

std::shared_ptr<const void> ProjectPresenter::createSnapshot() const {
  if (!project_) {
    return nullptr;
  }
  return std::static_pointer_cast<const void>(
      std::make_shared<orc::Project>(*getProject()));
}

orc::NodeID ProjectPresenter::addNode(const std::string& stage_name,
                                      double x_position, double y_position) {
  orc::NodeID id = orc::project_io::add_node(*getProject(), stage_name,
                                             x_position, y_position);
  is_modified_ = true;
  return id;
}

bool ProjectPresenter::removeNode(orc::NodeID node_id) {
  std::string reason;
  if (!canRemoveNode(node_id, &reason)) {
    return false;
  }

  orc::project_io::remove_node(*getProject(), node_id);
  is_modified_ = true;
  return true;
}

bool ProjectPresenter::canRemoveNode(orc::NodeID node_id,
                                     std::string* reason) const {
  return orc::project_io::can_remove_node(*getProject(), node_id, reason);
}

void ProjectPresenter::setNodePosition(orc::NodeID node_id, double x,
                                       double y) {
  orc::project_io::set_node_position(*getProject(), node_id, x, y);
  is_modified_ = true;
}

void ProjectPresenter::setNodeLabel(orc::NodeID node_id,
                                    const std::string& label) {
  orc::project_io::set_node_label(*getProject(), node_id, label);
  is_modified_ = true;
}

void ProjectPresenter::setNodeParameters(
    orc::NodeID node_id, const std::map<std::string, std::string>& parameters) {
  std::map<std::string, orc::ParameterValue> param_values;
  for (const auto& [key, value] : parameters) {
    param_values[key] = value;
  }
  orc::project_io::set_node_parameters(*getProject(), node_id, param_values);
  is_modified_ = true;
}

void ProjectPresenter::addEdge(orc::NodeID source_node,
                               orc::NodeID target_node) {
  orc::project_io::add_edge(*getProject(), source_node, target_node);
  is_modified_ = true;
}

void ProjectPresenter::removeEdge(orc::NodeID source_node,
                                  orc::NodeID target_node) {
  orc::project_io::remove_edge(*getProject(), source_node, target_node);
  is_modified_ = true;
}

std::vector<NodeInfo> ProjectPresenter::getNodes() const {
  auto* proj = project_.get();

  if (!proj) {
    ORC_LOG_ERROR("ProjectPresenter::getNodes called but no project exists!");
    return {};
  }

  std::vector<NodeInfo> result;

  for (const auto& node : proj->get_nodes()) {
    orc::NodeCapabilities caps =
        orc::project_io::get_node_capabilities(*proj, node.node_id);

    NodeInfo info;
    info.node_id = node.node_id;
    info.stage_name = node.stage_name;
    info.label = node.user_label;
    info.x_position = node.x_position;
    info.y_position = node.y_position;
    info.can_remove = caps.can_remove;
    info.can_trigger = caps.can_trigger;
    info.remove_reason = caps.remove_reason;
    info.trigger_reason = caps.trigger_reason;

    result.push_back(info);
  }

  return result;
}

NodeID ProjectPresenter::getFirstNode() const {
  if (!project_.get()) return NodeID();

  const auto& nodes = project_.get()->get_nodes();
  if (nodes.empty()) {
    return NodeID();  // Invalid NodeID
  }

  return nodes[0].node_id;
}

bool ProjectPresenter::hasNode(NodeID node_id) const {
  if (!getProject()) return false;

  const auto& nodes = getProject()->get_nodes();
  auto it = std::find_if(
      nodes.begin(), nodes.end(),
      [node_id](const auto& node) { return node.node_id == node_id; });

  return it != nodes.end();
}

std::vector<EdgeInfo> ProjectPresenter::getEdges() const {
  std::vector<EdgeInfo> result;
  if (!getProject()) {
    return result;
  }
  for (const auto& edge : getProject()->get_edges()) {
    EdgeInfo info;
    info.source_node = edge.source_node_id;
    info.target_node = edge.target_node_id;
    result.push_back(info);
  }

  return result;
}

NodeInfo ProjectPresenter::getNodeInfo(orc::NodeID node_id) const {
  if (!getProject()) {
    throw std::runtime_error("Project not available");
  }
  for (const auto& node : getProject()->get_nodes()) {
    if (node.node_id == node_id) {
      orc::NodeCapabilities caps =
          orc::project_io::get_node_capabilities(*getProject(), node_id);

      NodeInfo info;
      info.node_id = node.node_id;
      info.stage_name = node.stage_name;
      info.label =
          node.user_label.empty() ? node.display_name : node.user_label;
      info.x_position = node.x_position;
      info.y_position = node.y_position;
      info.can_remove = caps.can_remove;
      info.can_trigger = caps.can_trigger;
      info.remove_reason = caps.remove_reason;
      info.trigger_reason = caps.trigger_reason;

      return info;
    }
  }

  throw std::runtime_error("Node not found");
}

std::vector<StageInfo> ProjectPresenter::getAvailableStages(
    VideoFormat format) {
  std::vector<StageInfo> result;

  auto& registry = orc::StageRegistry::instance();
  auto stage_names = registry.get_registered_stages();
  const auto loaded_plugins = registry.get_loaded_plugins();
  std::map<std::string, std::string> stage_to_plugin_id;

  for (const auto& plugin : loaded_plugins) {
    for (const auto& stage_name : plugin.registered_stage_names) {
      stage_to_plugin_id[stage_name] = plugin.plugin_id;
    }
  }

  for (const auto& stage_name : stage_names) {
    try {
      // Create temporary instance to get node type info
      auto stage = registry.create_stage(stage_name);
      auto node_type_info = stage->get_node_type_info();

      // Filter by video format if specified
      if (format != VideoFormat::Unknown) {
        // Check compatibility - skip if incompatible
        bool compatible = false;
        switch (format) {
          case VideoFormat::NTSC:
            compatible = (node_type_info.compatible_formats ==
                              orc::VideoFormatCompatibility::NTSC_ONLY ||
                          node_type_info.compatible_formats ==
                              orc::VideoFormatCompatibility::ALL);
            break;
          case VideoFormat::PAL:
            compatible = (node_type_info.compatible_formats ==
                              orc::VideoFormatCompatibility::PAL_ONLY ||
                          node_type_info.compatible_formats ==
                              orc::VideoFormatCompatibility::ALL);
            break;
          case VideoFormat::PAL_M:
            compatible = (node_type_info.compatible_formats ==
                              orc::VideoFormatCompatibility::PAL_M_ONLY ||
                          node_type_info.compatible_formats ==
                              orc::VideoFormatCompatibility::ALL);
            break;
          default:
            compatible = true;
            break;
        }
        if (!compatible) continue;
      }

      StageInfo info;
      info.name = node_type_info.stage_name;
      info.display_name = node_type_info.display_name;
      info.description = node_type_info.description;
      info.category = node_type_info.menu_category;
      if (info.display_name.empty()) {
        throw std::runtime_error("Stage '" + stage_name +
                                 "' is missing required display_name metadata");
      }
      if (info.category.empty()) {
        throw std::runtime_error(
            "Stage '" + stage_name +
            "' is missing required menu_category metadata");
      }
      info.node_type = node_type_info.type;
      info.is_source = (node_type_info.type == orc::NodeType::SOURCE);
      info.is_sink = (node_type_info.type == orc::NodeType::SINK ||
                      node_type_info.type == orc::NodeType::ANALYSIS_SINK);
      auto plugin_it = stage_to_plugin_id.find(stage_name);
      if (plugin_it != stage_to_plugin_id.end()) {
        info.is_runtime_plugin_stage = true;
        info.owning_plugin_id = plugin_it->second;
      }

      result.push_back(info);
    } catch (const std::exception& e) {
      ORC_LOG_ERROR("Failed to get stage info for '{}': {}", stage_name,
                    e.what());
    }
  }

  return result;
}

std::vector<StageInfo> ProjectPresenter::getAllStages() {
  return getAvailableStages(VideoFormat::Unknown);
}

bool ProjectPresenter::hasStage(const std::string& stage_name) {
  return orc::StageRegistry::instance().has_stage(stage_name);
}

std::vector<LoadedPluginInfo> ProjectPresenter::getLoadedPlugins() {
  std::vector<LoadedPluginInfo> result;

  const auto& loaded_plugins =
      orc::StageRegistry::instance().get_loaded_plugins();
  result.reserve(loaded_plugins.size());

  for (const auto& plugin : loaded_plugins) {
    LoadedPluginInfo info;
    info.path = plugin.path;
    info.plugin_id = plugin.plugin_id;
    info.plugin_version = plugin.plugin_version;
    info.license_spdx = plugin.license_spdx;
    info.is_core_plugin = plugin.is_core_plugin;
    info.registered_stage_names = plugin.registered_stage_names;
    result.push_back(std::move(info));
  }

  return result;
}

std::vector<PluginDiagnosticInfo> ProjectPresenter::getPluginDiagnostics() {
  std::vector<PluginDiagnosticInfo> result;

  const auto& diagnostics =
      orc::StageRegistry::instance().get_plugin_diagnostics();
  result.reserve(diagnostics.size());

  for (const auto& diagnostic : diagnostics) {
    PluginDiagnosticInfo info;
    info.severity = fromPluginDiagnosticSeverity(diagnostic.severity);
    info.path = diagnostic.path;
    info.message = diagnostic.message;
    result.push_back(std::move(info));
  }

  return result;
}

std::vector<std::string> ProjectPresenter::getPluginSearchPaths() {
  return orc::StageRegistry::instance().get_plugin_search_paths();
}

PluginRegistryInfo ProjectPresenter::readPluginRegistry() {
  PluginRegistryInfo result;

  const auto& registry = orc::StageRegistry::instance();
  const auto persisted_registry = orc::StagePluginRegistry::load_default();
  result.registry_path = persisted_registry.registry_path;

  const auto& loaded_plugins = registry.get_loaded_plugins();
  std::set<std::string> loaded_paths;
  std::set<std::string> loaded_plugin_ids;
  for (const auto& plugin : loaded_plugins) {
    loaded_paths.insert(plugin.path);
    loaded_plugin_ids.insert(plugin.plugin_id);
  }

  const auto& entries = persisted_registry.entries;
  result.entries.reserve(entries.size());

  for (const auto& entry : entries) {
    PluginRegistryEntryInfo info;
    info.plugin_id = entry.plugin_id;
    info.plugin_version = entry.plugin_version;
    info.path = entry.path;
    info.source_repo_url = entry.source_repo_url;
    info.artifact_source = entry.artifact_source;
    info.release_asset_url = entry.release_asset_url;
    info.release_tag = entry.release_tag;
    info.release_asset_name = entry.release_asset_name;
    info.target_platform = entry.target_platform;
    info.local_dev_path = entry.local_dev_path;
    info.enabled = entry.enabled;
    info.trust_state = entry.trust_state;
    info.license_spdx = entry.license_spdx;
    info.is_core_plugin = entry.is_core_plugin;
    info.required_host_abi = entry.required_host_abi;
    info.sha256 = entry.sha256;
    info.is_loaded = loaded_paths.count(entry.path) > 0 ||
                     (!entry.plugin_id.empty() &&
                      loaded_plugin_ids.count(entry.plugin_id) > 0);

    std::error_code error_code;
    info.path_exists = !entry.path.empty() &&
                       std::filesystem::exists(entry.path, error_code) &&
                       !error_code;
    result.entries.push_back(std::move(info));
  }

  return result;
}

PluginRegistryMutationResult ProjectPresenter::addPluginToRegistry(
    const std::string& path, const std::string& plugin_id,
    const std::string& plugin_version, const std::string& license_spdx,
    bool is_core_plugin, bool trusted) {
  PluginRegistryEntryInfo entry_info;
  entry_info.path = path;
  entry_info.plugin_id = plugin_id;
  entry_info.plugin_version = plugin_version;
  entry_info.license_spdx = license_spdx;
  entry_info.is_core_plugin = is_core_plugin;
  entry_info.trust_state = trusted ? "trusted" : "untrusted";
  entry_info.enabled = true;
  entry_info.artifact_source = "local_path";
  return addPluginRegistryEntry(entry_info);
}

PluginRegistryMutationResult ProjectPresenter::addPluginRegistryEntry(
    const PluginRegistryEntryInfo& entry_info) {
  PluginRegistryMutationResult result;

  const bool is_remote_entry =
      entry_info.artifact_source == "github_release_asset";
  const bool has_local_path =
      !entry_info.path.empty() || !entry_info.local_dev_path.empty();

  if (!has_local_path && !is_remote_entry) {
    result.error_message = "Plugin path cannot be empty";
    return result;
  }

  if (is_remote_entry) {
    if (entry_info.release_asset_url.empty()) {
      result.error_message = "Remote plugin URL cannot be empty";
      return result;
    }
    if (entry_info.release_asset_name.empty()) {
      result.error_message = "Remote plugin asset name cannot be empty";
      return result;
    }
  } else {
    if (!entry_info.path.empty()) {
      const std::string expected_ext = current_platform_plugin_extension();
      if (!has_case_insensitive_suffix(entry_info.path, expected_ext)) {
        result.error_message = "Plugin path '" + entry_info.path +
                               "' is not valid for this platform "
                               "(expected a '" +
                               expected_ext + "' plugin binary)";
        return result;
      }
    }
  }

  if (entry_info.is_core_plugin) {
    result.error_message = "User-added plugins cannot be marked as core";
    return result;
  }

  const auto persisted_registry = orc::StagePluginRegistry::load_default();
  auto entries = persisted_registry.entries;
  const std::string registry_path = persisted_registry.registry_path;

  for (const auto& entry : entries) {
    if (!entry_info.plugin_id.empty() &&
        entry.plugin_id == entry_info.plugin_id) {
      result.error_message = "A plugin with id '" + entry_info.plugin_id +
                             "' already exists in the registry";
      return result;
    }
    if (!entry_info.path.empty() && !entry.path.empty() &&
        entry.path == entry_info.path) {
      result.error_message =
          "Path '" + entry_info.path + "' is already registered";
      return result;
    }
    if (!entry_info.local_dev_path.empty() && !entry.local_dev_path.empty() &&
        entry.local_dev_path == entry_info.local_dev_path) {
      result.error_message = "Local override path '" +
                             entry_info.local_dev_path +
                             "' is already registered";
      return result;
    }
  }

  orc::StagePluginRegistryEntry new_entry;
  new_entry.path = entry_info.path;
  new_entry.plugin_id = entry_info.plugin_id;
  new_entry.plugin_version = entry_info.plugin_version;
  new_entry.source_repo_url = entry_info.source_repo_url;
  new_entry.artifact_source = entry_info.artifact_source;
  new_entry.release_asset_url = entry_info.release_asset_url;
  new_entry.release_tag = entry_info.release_tag;
  new_entry.release_asset_name = entry_info.release_asset_name;
  new_entry.target_platform = entry_info.target_platform;
  new_entry.local_dev_path = entry_info.local_dev_path;
  new_entry.enabled = entry_info.enabled;
  new_entry.trust_state = entry_info.trust_state;
  new_entry.license_spdx = entry_info.license_spdx;
  new_entry.is_core_plugin = false;
  new_entry.required_host_abi = entry_info.required_host_abi;
  new_entry.sha256 = entry_info.sha256;
  entries.push_back(std::move(new_entry));

  std::string error;
  if (!orc::StagePluginRegistry::save(registry_path, entries, &error)) {
    result.error_message = "Failed to save registry: " + error;
    return result;
  }

  result.success = true;
  return result;
}

PluginRegistryMutationResult ProjectPresenter::addPluginFromReleasesUrl(
    const std::string& releases_url) {
  PluginRegistryMutationResult result;

  if (releases_url.empty()) {
    result.error_message = "Release URL cannot be empty";
    return result;
  }

  std::vector<std::string> warnings;
#if defined(_WIN32)
  const std::string target_platform = "windows";
#elif defined(__APPLE__)
  const std::string target_platform = "macos";
#else
  const std::string target_platform = "linux";
#endif

  const auto resolved =
      orc::PluginRemoteLoader::resolve_release_asset_from_releases_url(
          releases_url, target_platform, &warnings);

  if (!resolved.success) {
    result.error_message = resolved.error_message;
    return result;
  }

  PluginRegistryEntryInfo entry_info;
  entry_info.artifact_source = "github_release_asset";
  entry_info.source_repo_url = resolved.source_repo_url;
  entry_info.release_tag = resolved.release_tag;
  entry_info.release_asset_url = resolved.release_asset_url;
  entry_info.release_asset_name = resolved.release_asset_name;
  entry_info.target_platform = target_platform;
  entry_info.enabled = true;
  // Adding a plugin is the user's explicit consent to run it, so entries
  // created through this flow are trusted immediately. The trust gate still
  // guards entries that arrive from outside the application (hand-edited or
  // copied registry files), which default to untrusted on load.
  entry_info.trust_state = "trusted";

  auto add_result = addPluginRegistryEntry(entry_info);
  if (!add_result.success) {
    return add_result;
  }

  if (!warnings.empty()) {
    add_result.error_message = warnings.front();
  }

  return add_result;
}

PluginRegistryMutationResult ProjectPresenter::removePluginFromRegistry(
    const std::string& plugin_id) {
  return removePluginRegistryEntry(plugin_id, std::string(), std::string());
}

PluginRegistryMutationResult ProjectPresenter::removePluginRegistryEntry(
    const std::string& plugin_id, const std::string& path,
    const std::string& release_asset_url) {
  PluginRegistryMutationResult result;

  if (plugin_id.empty() && path.empty() && release_asset_url.empty()) {
    result.error_message = "No plugin identifier was provided for removal";
    return result;
  }

  const auto persisted_registry = orc::StagePluginRegistry::load_default();
  auto entries = persisted_registry.entries;
  const std::string registry_path = persisted_registry.registry_path;

  auto matches_identity = [&](const orc::StagePluginRegistryEntry& e) {
    if (!plugin_id.empty() && e.plugin_id == plugin_id) {
      return true;
    }
    if (!path.empty() && e.path == path) {
      return true;
    }
    if (!release_asset_url.empty() &&
        e.release_asset_url == release_asset_url) {
      return true;
    }
    return false;
  };

  auto it = std::find_if(entries.begin(), entries.end(), matches_identity);

  if (it == entries.end()) {
    result.error_message = "No matching plugin entry found in registry";
    return result;
  }

  entries.erase(it);

  std::string error;
  if (!orc::StagePluginRegistry::save(registry_path, entries, &error)) {
    result.error_message = "Failed to save registry: " + error;
    return result;
  }

  result.success = true;
  return result;
}

// Find a registry entry by plugin_id. If no direct match exists, fall back
// to matching a loaded plugin's path against entries whose plugin_id is
// still empty (happens when plugins are added via file before their ID is
// known) and backfill the id.
static std::vector<orc::StagePluginRegistryEntry>::iterator
findRegistryEntryByPluginId(std::vector<orc::StagePluginRegistryEntry>& entries,
                            const std::string& plugin_id) {
  auto it = std::find_if(entries.begin(), entries.end(),
                         [&plugin_id](const orc::StagePluginRegistryEntry& e) {
                           return e.plugin_id == plugin_id;
                         });

  if (it == entries.end()) {
    const auto& loaded_plugins =
        orc::StageRegistry::instance().get_loaded_plugins();
    auto loaded_it = std::find_if(
        loaded_plugins.begin(), loaded_plugins.end(),
        [&plugin_id](const auto& lp) { return lp.plugin_id == plugin_id; });

    if (loaded_it != loaded_plugins.end()) {
      it = std::find_if(entries.begin(), entries.end(),
                        [&loaded_it](const orc::StagePluginRegistryEntry& e) {
                          return !e.path.empty() && e.path == loaded_it->path;
                        });

      if (it != entries.end() && it->plugin_id.empty()) {
        it->plugin_id = plugin_id;
      }
    }
  }

  return it;
}

PluginRegistryMutationResult ProjectPresenter::setPluginRegistryEntryEnabled(
    const std::string& plugin_id, bool enabled) {
  PluginRegistryMutationResult result;

  if (plugin_id.empty()) {
    result.error_message = "Plugin id cannot be empty";
    return result;
  }

  const auto persisted_registry = orc::StagePluginRegistry::load_default();
  auto entries = persisted_registry.entries;
  const std::string registry_path = persisted_registry.registry_path;

  auto it = findRegistryEntryByPluginId(entries, plugin_id);

  if (it == entries.end()) {
    result.error_message =
        "No plugin with id '" + plugin_id + "' found in registry";
    return result;
  }

  it->enabled = enabled;

  std::string error;
  if (!orc::StagePluginRegistry::save(registry_path, entries, &error)) {
    result.error_message = "Failed to save registry: " + error;
    return result;
  }

  result.success = true;
  return result;
}

PluginRegistryMutationResult ProjectPresenter::setPluginRegistryEntryTrusted(
    const std::string& plugin_id, bool trusted) {
  PluginRegistryMutationResult result;

  if (plugin_id.empty()) {
    result.error_message = "Plugin id cannot be empty";
    return result;
  }

  const auto persisted_registry = orc::StagePluginRegistry::load_default();
  auto entries = persisted_registry.entries;
  const std::string registry_path = persisted_registry.registry_path;

  auto it = findRegistryEntryByPluginId(entries, plugin_id);

  if (it == entries.end()) {
    result.error_message =
        "No plugin with id '" + plugin_id + "' found in registry";
    return result;
  }

  if (it->is_core_plugin) {
    result.error_message =
        "Core plugins are always trusted; their trust "
        "state cannot be changed";
    return result;
  }

  it->trust_state = trusted ? "trusted" : "untrusted";

  std::string error;
  if (!orc::StagePluginRegistry::save(registry_path, entries, &error)) {
    result.error_message = "Failed to save registry: " + error;
    return result;
  }

  result.success = true;
  return result;
}

PluginRegistryMutationResult
ProjectPresenter::clearPluginRegistryForSafeMode() {
  PluginRegistryMutationResult result;

  const std::string registry_path =
      orc::StagePluginRegistry::default_registry_path();
  std::string error;
  if (!orc::StagePluginRegistry::save(registry_path, {}, &error)) {
    result.error_message = "Failed to clear plugin registry: " + error;
    return result;
  }

  result.success = true;
  return result;
}

std::shared_ptr<void> ProjectPresenter::createStageInstance(
    const std::string& stage_name) {
  auto stage = orc::StageRegistry::instance().create_stage(stage_name);
  return std::static_pointer_cast<void>(stage);
}

bool ProjectPresenter::canTriggerNode(orc::NodeID node_id,
                                      std::string* reason) const {
  return orc::project_io::can_trigger_node(*getProject(), node_id, reason);
}

bool ProjectPresenter::triggerNode(orc::NodeID node_id,
                                   ProgressCallback progress_callback) {
  std::string status;

  TriggerProgressCallback core_callback;
  if (progress_callback) {
    core_callback = [&](size_t current, size_t total, const std::string& msg) {
      progress_callback(current, total, msg);
    };
  }

  bool success = orc::project_io::trigger_node(*getProject(), node_id, status,
                                               core_callback);

  if (success) {
    is_modified_ = true;
  }

  return success;
}

bool ProjectPresenter::triggerAllSinks(ProgressCallback progress_callback) {
  if (!project_) {
    return false;
  }

  // Find all triggerable sink nodes
  std::vector<orc::NodeID> sink_nodes;
  auto& registry = orc::StageRegistry::instance();

  for (const auto& node : project_->get_nodes()) {
    if (!registry.has_stage(node.stage_name)) {
      ORC_LOG_WARN("Unknown stage type: {}", node.stage_name);
      continue;
    }

    auto stage = registry.create_stage(node.stage_name);
    if (!stage) {
      ORC_LOG_WARN("Failed to create stage: {}", node.stage_name);
      continue;
    }

    // Check if stage is triggerable (sink node)
    auto* trigger_stage = dynamic_cast<orc::TriggerableStage*>(stage.get());
    if (trigger_stage) {
      sink_nodes.push_back(node.node_id);
      ORC_LOG_DEBUG("Found triggerable node: {} ({})", node.node_id,
                    node.stage_name);
    }
  }

  if (sink_nodes.empty()) {
    ORC_LOG_ERROR("No triggerable sink nodes found in project");
    return false;
  }

  ORC_LOG_INFO("Found {} triggerable sink nodes", sink_nodes.size());

  // Trigger each sink node
  bool all_success = true;
  size_t sink_index = 0;

  for (const auto& node_id : sink_nodes) {
    ++sink_index;
    ORC_LOG_INFO("========================================");
    ORC_LOG_INFO("Processing sink {}/{}: {}", sink_index, sink_nodes.size(),
                 node_id);
    ORC_LOG_INFO("========================================");

    // Create wrapper callback that adds sink context
    ProgressCallback sink_callback;
    if (progress_callback) {
      sink_callback = [&, node_id](size_t current, size_t total,
                                   const std::string& msg) {
        std::string prefixed_msg = "[" + node_id.to_string() + "] " + msg;
        progress_callback(current, total, prefixed_msg);
      };
    }

    bool success = triggerNode(node_id, sink_callback);

    if (!success) {
      ORC_LOG_ERROR("Failed to trigger node: {}", node_id);
      all_success = false;
    } else {
      ORC_LOG_INFO("Successfully triggered node: {}", node_id);
    }
  }

  if (all_success) {
    ORC_LOG_INFO("========================================");
    ORC_LOG_INFO("All {} sink nodes triggered successfully", sink_nodes.size());
    ORC_LOG_INFO("========================================");
  } else {
    ORC_LOG_ERROR("========================================");
    ORC_LOG_ERROR("One or more sink nodes failed");
    ORC_LOG_ERROR("========================================");
  }

  return all_success;
}

bool ProjectPresenter::validateProject() const {
  // Basic validation - could be expanded
  if (project_.get()->get_nodes().empty()) {
    return false;
  }

  // Check for at least one source and one sink
  bool has_source = false;
  bool has_sink = false;

  for (const auto& node : project_.get()->get_nodes()) {
    // For now, skip detailed validation - just check we have nodes
    has_source = true;  // Placeholder
    has_sink = true;    // Placeholder
  }

  return has_source && has_sink;
}

std::vector<std::string> ProjectPresenter::getValidationErrors() const {
  std::vector<std::string> errors;

  if (project_.get()->get_nodes().empty()) {
    errors.push_back("Project has no nodes");
  }

  bool has_source = false;
  bool has_sink = false;

  for (const auto& node : project_.get()->get_nodes()) {
    // Placeholder validation
    has_source = true;
    has_sink = true;
  }

  if (!has_source) {
    errors.push_back("Project has no source nodes");
  }
  if (!has_sink) {
    errors.push_back("Project has no sink nodes");
  }

  return errors;
}

orc::ConfigurationStatus ProjectPresenter::getNodeConfigurationStatus(
    orc::NodeID node_id) const {
  if (!getProject()) return orc::ConfigurationStatus::Green;

  const auto& nodes = getProject()->get_nodes();
  auto node_it = std::find_if(nodes.begin(), nodes.end(),
                              [&node_id](const orc::ProjectDAGNode& n) {
                                return n.node_id == node_id;
                              });

  if (node_it == nodes.end()) return orc::ConfigurationStatus::Green;

  try {
    auto stage =
        orc::StageRegistry::instance().create_stage(node_it->stage_name);

    auto* param_stage = dynamic_cast<orc::ParameterizedStage*>(stage.get());
    if (param_stage) {
      param_stage->set_parameters(node_it->parameters);
    }

    return stage->get_configuration_status();
  } catch (...) {
    return orc::ConfigurationStatus::Green;
  }
}

std::shared_ptr<void> ProjectPresenter::getDAG() const {
  if (!project_.get()) return nullptr;

  // Return cached DAG if available
  if (dag_) {
    return dag_;
  }

  // Otherwise build new DAG
  return std::static_pointer_cast<void>(orc::project_to_dag(*getProject()));
}

std::shared_ptr<void> ProjectPresenter::buildDAG() {
  if (!project_.get()) return nullptr;

  try {
    // Build and cache the DAG
    dag_ = std::static_pointer_cast<void>(orc::project_to_dag(*getProject()));
    return dag_;
  } catch (const std::exception&) {
    dag_.reset();
    return nullptr;
  }
}

bool ProjectPresenter::validateDAG() {
  if (!project_.get()) return false;

  try {
    // Try to build the DAG - if successful, it's valid
    auto test_dag = orc::project_to_dag(*getProject());
    return test_dag != nullptr;
  } catch (const std::exception&) {
    return false;
  }
}

std::string ProjectPresenter::getStageInstructions(
    const std::string& stage_name) const {
  try {
    auto& registry = orc::StageRegistry::instance();
    if (!registry.has_stage(stage_name)) {
      return {};
    }
    auto stage = registry.create_stage(stage_name);
    return stage->get_instructions();
  } catch (const std::exception&) {
    return {};
  }
}

std::vector<ParameterDescriptor> ProjectPresenter::getStageParameters(
    const std::string& stage_name) {
  try {
    // Create a temporary stage instance
    auto& registry = orc::StageRegistry::instance();
    auto stage = registry.create_stage(stage_name);

    // Cast to ParameterizedStage
    auto* param_stage = dynamic_cast<orc::ParameterizedStage*>(stage.get());
    if (!param_stage) {
      return {};  // Stage doesn't have parameters
    }

    // Get current project's video format and source type for context
    auto video_format =
        project_ ? project_->get_video_format() : VideoSystem::Unknown;
    auto source_type_core =
        project_ ? project_->get_source_type() : orc::SourceType::Unknown;

    // Get parameter descriptors with project context
    return param_stage->get_parameter_descriptors(video_format,
                                                  source_type_core);
  } catch (const std::exception&) {
    return {};
  }
}

std::map<std::string, ParameterValue> ProjectPresenter::getNodeParameters(
    NodeID node_id) {
  if (!project_.get()) return {};

  // Find the node
  const auto& nodes = project_.get()->get_nodes();
  auto it = std::find_if(
      nodes.begin(), nodes.end(),
      [node_id](const auto& node) { return node.node_id == node_id; });

  if (it == nodes.end()) {
    return {};
  }

  return it->parameters;
}

bool ProjectPresenter::setNodeParameters(
    NodeID node_id, const std::map<std::string, ParameterValue>& params) {
  if (!project_.get()) return false;

  try {
    orc::project_io::set_node_parameters(*getProject(), node_id, params);
    is_modified_ = true;

    // Invalidate cached DAG since parameters changed
    dag_.reset();

    return true;
  } catch (const std::exception& e) {
    ORC_LOG_ERROR("Failed to set node parameters for node {}: {}",
                  node_id.to_string(), e.what());
    throw;
  }
}

}  // namespace orc::presenters
