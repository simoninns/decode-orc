/*
 * File:        project_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     Project management presenter implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "project_presenter.h"
#include "../core/include/project.h"
#include "../core/include/project_to_dag.h"
#include "../core/include/stage_registry.h"
#include "../core/include/tbc_metadata.h"
#include "../core/include/stage_parameter.h"
#include "../core/include/logging.h"
#include "../core/stages/ld_sink/ld_sink_stage.h"  // For TriggerableStage definition
#include <stdexcept>
#include <algorithm>

namespace orc::presenters {

// === Helper Functions ===

static VideoSystem toVideoSystem(VideoFormat format) {
    switch (format) {
        case VideoFormat::NTSC: return VideoSystem::NTSC;
        case VideoFormat::PAL: return VideoSystem::PAL;
        case VideoFormat::Unknown: return VideoSystem::Unknown;
    }
    return VideoSystem::Unknown;
}

static VideoFormat fromVideoSystem(VideoSystem system) {
    switch (system) {
        case VideoSystem::NTSC: return VideoFormat::NTSC;
        case VideoSystem::PAL: return VideoFormat::PAL;
        case VideoSystem::Unknown: return VideoFormat::Unknown;
    }
    return VideoFormat::Unknown;
}

static orc::SourceType toSourceType(SourceType type) {
    switch (type) {
        case SourceType::Composite: return orc::SourceType::Composite;
        case SourceType::YC: return orc::SourceType::YC;
        case SourceType::Unknown: return orc::SourceType::Unknown;
    }
    return orc::SourceType::Unknown;
}

static SourceType fromSourceType(orc::SourceType type) {
    switch (type) {
        case orc::SourceType::Composite: return SourceType::Composite;
        case orc::SourceType::YC: return SourceType::YC;
        case orc::SourceType::Unknown: return SourceType::Unknown;
    }
    return SourceType::Unknown;
}

// === Static Utility Methods ===

std::optional<orc::public_api::VideoParameters> ProjectPresenter::readVideoParameters(
    const std::string& metadata_path)
{
    try {
        orc::TBCMetadataReader reader;
        if (!reader.open(metadata_path)) {
            ORC_LOG_WARN("Failed to open metadata file: {}", metadata_path);
            return std::nullopt;
        }
        
        auto params = reader.read_video_parameters();
        reader.close();
        return params;
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Exception reading metadata from {}: {}", metadata_path, e.what());
        return std::nullopt;
    }
}

// === ProjectPresenter Implementation ===

ProjectPresenter::ProjectPresenter()
    : project_(std::make_unique<orc::Project>(orc::project_io::create_empty_project("Untitled Project")))
    , project_path_()
    , is_modified_(false)
    , dag_(nullptr)
{
    ORC_LOG_DEBUG("ProjectPresenter default constructor: project = {}", static_cast<void*>(project_.get()));
}

ProjectPresenter::ProjectPresenter(orc::Project& project)
    : project_(nullptr)  // Don't own the project
    , project_path_()
    , is_modified_(false)
    , dag_(nullptr)
{
}

ProjectPresenter::ProjectPresenter(const std::string& project_path)
    : project_(nullptr)
    , project_path_(project_path)
    , is_modified_(false)
    , dag_(nullptr)
{
    project_ = std::make_unique<orc::Project>(orc::project_io::load_project(project_path));
}

ProjectPresenter::~ProjectPresenter() = default;

ProjectPresenter::ProjectPresenter(ProjectPresenter&& other) noexcept
    : project_(std::move(other.project_))
    , project_path_(std::move(other.project_path_))
    , is_modified_(other.is_modified_)
    , dag_(std::move(other.dag_))
{
    ORC_LOG_DEBUG("ProjectPresenter move constructor: project = {}", 
                  static_cast<void*>(project_.get()));
}

ProjectPresenter& ProjectPresenter::operator=(ProjectPresenter&& other) noexcept
{
    if (this != &other) {
        project_ = std::move(other.project_);
        project_path_ = std::move(other.project_path_);
        is_modified_ = other.is_modified_;
        dag_ = std::move(other.dag_);
    }
    return *this;
}

bool ProjectPresenter::createQuickProject(VideoFormat format, SourceType source, 
                                         const std::vector<std::string>& input_files)
{
    if (input_files.empty()) {
        return false;
    }
    
    // Create empty project with format
    project_ = std::make_unique<Project>(
        orc::project_io::create_empty_project("Quick Project", toVideoSystem(format), toSourceType(source))
    );
    
    // Add source nodes for each input file
    double y_offset = 0.0;
    std::vector<NodeID> source_nodes;
    
    for (const auto& file : input_files) {
        orc::NodeID source_id = orc::project_io::add_node(*project_.get(), "tbc-source", 0.0, y_offset);
        
        // Set the TBC path parameter
        std::map<std::string, orc::ParameterValue> params;
        params["tbc_path"] = file;
        orc::project_io::set_node_parameters(*project_.get(), source_id, params);
        
        source_nodes.push_back(source_id);
        y_offset += 100.0;
    }
    
    // Add appropriate decoder based on format and source
    orc::NodeID decoder_id;
    if (format == VideoFormat::NTSC) {
        if (source == SourceType::Composite) {
            decoder_id = orc::project_io::add_node(*project_.get(), "ntsc-comb-decode", 200.0, 50.0);
        } else {
            decoder_id = orc::project_io::add_node(*project_.get(), "ntsc-yc-decode", 200.0, 50.0);
        }
    } else if (format == VideoFormat::PAL) {
        if (source == SourceType::Composite) {
            decoder_id = orc::project_io::add_node(*project_.get(), "pal-transform-2d", 200.0, 50.0);
        } else {
            decoder_id = orc::project_io::add_node(*project_.get(), "pal-yc-decode", 200.0, 50.0);
        }
    } else {
        return false;
    }
    
    // Connect first source to decoder
    if (!source_nodes.empty()) {
        orc::project_io::add_edge(*project_.get(), source_nodes[0], decoder_id);
    }
    
    // Add a preview sink
    orc::NodeID preview_id = orc::project_io::add_node(*project_.get(), "preview-sink", 400.0, 50.0);
    orc::project_io::add_edge(*project_.get(), decoder_id, preview_id);
    
    is_modified_ = true;
    return true;
}

bool ProjectPresenter::loadProject(const std::string& project_path)
{
    try {
        project_ = std::make_unique<Project>(orc::project_io::load_project(project_path));
        project_path_ = project_path;
        is_modified_ = false;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool ProjectPresenter::saveProject(const std::string& project_path)
{
    try {
        orc::project_io::save_project(*project_.get(), project_path);
        project_path_ = project_path;
        is_modified_ = false;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void ProjectPresenter::clearProject()
{
    orc::project_io::clear_project(*project_);
    is_modified_ = true;
}

bool ProjectPresenter::isModified() const
{
    return is_modified_;
}

std::string ProjectPresenter::getProjectPath() const
{
    return project_path_;
}

std::string ProjectPresenter::getProjectName() const
{
    return project_.get()->get_name();
}

void ProjectPresenter::setProjectName(const std::string& name)
{
    orc::project_io::set_project_name(*project_.get(), name);
    is_modified_ = true;
}

std::string ProjectPresenter::getProjectDescription() const
{
    return project_.get()->get_description();
}

void ProjectPresenter::setProjectDescription(const std::string& description)
{
    orc::project_io::set_project_description(*project_.get(), description);
    is_modified_ = true;
}

VideoFormat ProjectPresenter::getVideoFormat() const
{
    return fromVideoSystem(project_.get()->get_video_format());
}

void ProjectPresenter::setVideoFormat(VideoFormat format)
{
    orc::project_io::set_video_format(*project_.get(), toVideoSystem(format));
    is_modified_ = true;
}

SourceType ProjectPresenter::getSourceType() const
{
    return fromSourceType(project_.get()->get_source_format());
}

void ProjectPresenter::setSourceType(SourceType source)
{
    orc::project_io::set_source_format(*project_.get(), toSourceType(source));
    is_modified_ = true;
}

std::shared_ptr<const orc::Project> ProjectPresenter::createSnapshot() const
{
    if (!project_) {
        return nullptr;
    }
    return std::make_shared<orc::Project>(*project_);
}

orc::NodeID ProjectPresenter::addNode(const std::string& stage_name, double x_position, double y_position)
{
    orc::NodeID id = orc::project_io::add_node(*project_.get(), stage_name, x_position, y_position);
    is_modified_ = true;
    return id;
}

bool ProjectPresenter::removeNode(orc::NodeID node_id)
{
    std::string reason;
    if (!canRemoveNode(node_id, &reason)) {
        return false;
    }
    
    orc::project_io::remove_node(*project_.get(), node_id);
    is_modified_ = true;
    return true;
}

bool ProjectPresenter::canRemoveNode(orc::NodeID node_id, std::string* reason) const
{
    return orc::project_io::can_remove_node(*project_.get(), node_id, reason);
}

void ProjectPresenter::setNodePosition(orc::NodeID node_id, double x, double y)
{
    orc::project_io::set_node_position(*project_.get(), node_id, x, y);
    is_modified_ = true;
}

void ProjectPresenter::setNodeLabel(orc::NodeID node_id, const std::string& label)
{
    orc::project_io::set_node_label(*project_.get(), node_id, label);
    is_modified_ = true;
}

void ProjectPresenter::setNodeParameters(orc::NodeID node_id, const std::map<std::string, std::string>& parameters)
{
    std::map<std::string, orc::ParameterValue> param_values;
    for (const auto& [key, value] : parameters) {
        param_values[key] = value;
    }
    orc::project_io::set_node_parameters(*project_.get(), node_id, param_values);
    is_modified_ = true;
}

void ProjectPresenter::addEdge(orc::NodeID source_node, orc::NodeID target_node)
{
    orc::project_io::add_edge(*project_.get(), source_node, target_node);
    is_modified_ = true;
}

void ProjectPresenter::removeEdge(orc::NodeID source_node, orc::NodeID target_node)
{
    orc::project_io::remove_edge(*project_.get(), source_node, target_node);
    is_modified_ = true;
}

std::vector<NodeInfo> ProjectPresenter::getNodes() const
{
    auto* proj = project_.get();
    
    if (!proj) {
        ORC_LOG_ERROR("ProjectPresenter::getNodes called but no project exists!");
        return {};
    }
    
    std::vector<NodeInfo> result;
    
    for (const auto& node : proj->get_nodes()) {
        orc::NodeCapabilities caps = orc::project_io::get_node_capabilities(*proj, node.node_id);
        
        NodeInfo info;
        info.node_id = node.node_id;
        info.stage_name = node.stage_name;
        info.label = node.user_label;
        info.x_position = node.x_position;
        info.y_position = node.y_position;
        info.can_remove = caps.can_remove;
        info.can_trigger = caps.can_trigger;
        info.can_inspect = caps.can_inspect;
        info.remove_reason = caps.remove_reason;
        info.trigger_reason = caps.trigger_reason;
        info.inspect_reason = caps.inspect_reason;
        
        result.push_back(info);
    }
    
    return result;
}

NodeID ProjectPresenter::getFirstNode() const
{
    if (!project_.get()) return NodeID();
    
    const auto& nodes = project_.get()->get_nodes();
    if (nodes.empty()) {
        return NodeID();  // Invalid NodeID
    }
    
    return nodes[0].node_id;
}

bool ProjectPresenter::hasNode(NodeID node_id) const
{
    if (!project_.get()) return false;
    
    const auto& nodes = project_.get()->get_nodes();
    auto it = std::find_if(nodes.begin(), nodes.end(),
        [node_id](const auto& node) { return node.node_id == node_id; });
    
    return it != nodes.end();
}

std::vector<EdgeInfo> ProjectPresenter::getEdges() const
{
    std::vector<EdgeInfo> result;
    
    for (const auto& edge : project_.get()->get_edges()) {
        EdgeInfo info;
        info.source_node = edge.source_node_id;
        info.target_node = edge.target_node_id;
        result.push_back(info);
    }
    
    return result;
}

NodeInfo ProjectPresenter::getNodeInfo(orc::NodeID node_id) const
{
    for (const auto& node : project_.get()->get_nodes()) {
        if (node.node_id == node_id) {
            orc::NodeCapabilities caps = orc::project_io::get_node_capabilities(*project_.get(), node_id);
            
            NodeInfo info;
            info.node_id = node.node_id;
            info.stage_name = node.stage_name;
            info.label = node.display_name;
            info.x_position = node.x_position;
            info.y_position = node.y_position;
            info.can_remove = caps.can_remove;
            info.can_trigger = caps.can_trigger;
            info.can_inspect = caps.can_inspect;
            info.remove_reason = caps.remove_reason;
            info.trigger_reason = caps.trigger_reason;
            info.inspect_reason = caps.inspect_reason;
            
            return info;
        }
    }
    
    throw std::runtime_error("Node not found");
}

std::vector<StageInfo> ProjectPresenter::getAvailableStages(VideoFormat format)
{
    std::vector<StageInfo> result;
    
    auto& registry = orc::StageRegistry::instance();
    auto stage_names = registry.get_registered_stages();
    
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
                        compatible = (node_type_info.compatible_formats == orc::VideoFormatCompatibility::NTSC_ONLY ||
                                    node_type_info.compatible_formats == orc::VideoFormatCompatibility::ALL);
                        break;
                    case VideoFormat::PAL:
                        compatible = (node_type_info.compatible_formats == orc::VideoFormatCompatibility::PAL_ONLY ||
                                    node_type_info.compatible_formats == orc::VideoFormatCompatibility::ALL);
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
            info.node_type = node_type_info.type;
            info.is_source = (node_type_info.type == orc::NodeType::SOURCE);
            info.is_sink = (node_type_info.type == orc::NodeType::SINK);
            
            result.push_back(info);
        } catch (const std::exception& e) {
            ORC_LOG_ERROR("Failed to get stage info for '{}': {}", stage_name, e.what());
        }
    }
    
    return result;
}

std::vector<StageInfo> ProjectPresenter::getAllStages()
{
    return getAvailableStages(VideoFormat::Unknown);
}

bool ProjectPresenter::hasStage(const std::string& stage_name)
{
    return orc::StageRegistry::instance().has_stage(stage_name);
}

std::shared_ptr<void> ProjectPresenter::getStageForInspection(NodeID node_id) const
{
    if (!project_.get()) return nullptr;
    
    // Try to get from DAG first (preserves execution state)
    auto dag = orc::project_to_dag(*project_.get());
    if (dag) {
        const auto& dag_nodes = dag->nodes();
        auto it = std::find_if(dag_nodes.begin(), dag_nodes.end(),
            [&node_id](const orc::DAGNode& n) { return n.node_id == node_id; });
        if (it != dag_nodes.end() && it->stage) {
            return std::static_pointer_cast<void>(it->stage);
        }
    }
    
    // Fall back to creating fresh instance
    const auto& nodes = project_.get()->get_nodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
        [&node_id](const orc::ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it != nodes.end()) {
        return createStageInstance(node_it->stage_name);
    }
    
    return nullptr;
}

std::shared_ptr<void> ProjectPresenter::createStageInstance(const std::string& stage_name)
{
    auto stage = orc::StageRegistry::instance().create_stage(stage_name);
    return std::static_pointer_cast<void>(stage);
}

bool ProjectPresenter::canTriggerNode(orc::NodeID node_id, std::string* reason) const
{
    return orc::project_io::can_trigger_node(*project_.get(), node_id, reason);
}

bool ProjectPresenter::triggerNode(orc::NodeID node_id, ProgressCallback progress_callback)
{
    std::string status;
    
    TriggerProgressCallback core_callback;
    if (progress_callback) {
        core_callback = [&](size_t current, size_t total, const std::string& msg) {
            progress_callback(current, total, msg);
        };
    }
    
    bool success = orc::project_io::trigger_node(*project_.get(), node_id, status, core_callback);
    
    if (success) {
        is_modified_ = true;
    }
    
    return success;
}

bool ProjectPresenter::triggerAllSinks(ProgressCallback progress_callback)
{
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
            ORC_LOG_DEBUG("Found triggerable node: {} ({})", node.node_id, node.stage_name);
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
        ORC_LOG_INFO("Processing sink {}/{}: {}", sink_index, sink_nodes.size(), node_id);
        ORC_LOG_INFO("========================================");
        
        // Create wrapper callback that adds sink context
        ProgressCallback sink_callback;
        if (progress_callback) {
            sink_callback = [&, node_id](size_t current, size_t total, const std::string& msg) {
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

bool ProjectPresenter::validateProject() const
{
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

std::vector<std::string> ProjectPresenter::getValidationErrors() const
{
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

std::optional<StageInspectionView> ProjectPresenter::getNodeInspection(NodeID node_id) const
{
    if (!project_) {
        return std::nullopt;
    }
    
    // Find the node in the project
    const auto& nodes = project_.get()->get_nodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
        [&node_id](const orc::ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == nodes.end()) {
        return std::nullopt;  // Node not found
    }
    
    const std::string& stage_name = node_it->stage_name;
    
    // Create a stage instance from the registry
    std::shared_ptr<orc::DAGStage> stage;
    try {
        auto& stage_registry = orc::StageRegistry::instance();
        stage = stage_registry.create_stage(stage_name);
        
        // Apply the node's parameters to the stage if it's parameterized
        auto* param_stage = dynamic_cast<orc::ParameterizedStage*>(stage.get());
        if (param_stage) {
            param_stage->set_parameters(node_it->parameters);
        }
    } catch (const std::exception&) {
        return std::nullopt;  // Failed to create stage
    }
    
    // Generate report from the stage
    auto core_report = stage->generate_report();
    if (!core_report) {
        return std::nullopt;  // Stage doesn't support inspection
    }
    
    // Convert to view model
    StageInspectionView view;
    view.summary = core_report->summary;
    view.items = core_report->items;
    view.metrics = core_report->metrics;
    
    return view;
}

std::shared_ptr<void> ProjectPresenter::getDAG() const
{
    if (!project_.get()) return nullptr;
    
    // Return cached DAG if available
    if (dag_) {
        return dag_;
    }
    
    // Otherwise build new DAG
    return std::static_pointer_cast<void>(orc::project_to_dag(*project_.get()));
}

std::shared_ptr<void> ProjectPresenter::buildDAG()
{
    if (!project_.get()) return nullptr;
    
    try {
        // Build and cache the DAG
        dag_ = std::static_pointer_cast<void>(orc::project_to_dag(*project_.get()));
        return dag_;
    } catch (const std::exception&) {
        dag_.reset();
        return nullptr;
    }
}

bool ProjectPresenter::validateDAG()
{
    if (!project_.get()) return false;
    
    try {
        // Try to build the DAG - if successful, it's valid
        auto test_dag = orc::project_to_dag(*project_.get());
        return test_dag != nullptr;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<ParameterDescriptor> ProjectPresenter::getStageParameters(const std::string& stage_name)
{
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
        auto video_format = project_ ? project_->get_video_format() : VideoSystem::Unknown;
        auto source_type_core = project_ ? project_->get_source_type() : orc::SourceType::Unknown;
        
        // Get parameter descriptors with project context
        return param_stage->get_parameter_descriptors(video_format, source_type_core);
    } catch (const std::exception&) {
        return {};
    }
}

std::map<std::string, ParameterValue> ProjectPresenter::getNodeParameters(NodeID node_id)
{
    if (!project_.get()) return {};
    
    // Find the node
    const auto& nodes = project_.get()->get_nodes();
    auto it = std::find_if(nodes.begin(), nodes.end(),
        [node_id](const auto& node) { return node.node_id == node_id; });
    
    if (it == nodes.end()) {
        return {};
    }
    
    return it->parameters;
}

bool ProjectPresenter::setNodeParameters(NodeID node_id, const std::map<std::string, ParameterValue>& params)
{
    if (!project_.get()) return false;
    
    try {
        orc::project_io::set_node_parameters(*project_.get(), node_id, params);
        is_modified_ = true;
        
        // Invalidate cached DAG since parameters changed
        dag_.reset();
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace orc::presenters
