/*
 * File:        project.cpp
 * Module:      orc-core
 * Purpose:     Project
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 *
 * ARCHITECTURE NOTE - STRICT ENCAPSULATION:
 * ==========================================
 * This file implements the ONLY functions that can modify Project state.
 * 
 * All Project fields are PRIVATE. This file accesses them via friend
 * declarations in project.h.
 * 
 * CRITICAL RULES:
 * 1. ALL Project modifications MUST go through project_io:: functions
 * 2. GUI/CLI code CANNOT directly modify Project fields
 * 3. All project_io functions MUST set is_modified_ = true when changing state
 * 4. Project fields can ONLY be read via public const getters externally
 * 
 * When adding new functionality:
 * - Add a new project_io:: function here
 * - Add friend declaration in project.h
 * - Forward-declare the function in project.h before the Project class
 * - Update GUI/CLI to use the new function
 * 
 * DO NOT bypass this architecture by making Project fields public.
 */

#include "project.h"
#include "tbc_source_internal/tbc_metadata.h"
#include "logging.h"
#include "stage_registry.h"
#include "project_to_dag.h"
#include "dag_executor.h"
#include "stages/ld_sink/ld_sink_stage.h"
#include "observation_context.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <queue>
#include <set>
#include <future>
#include <yaml-cpp/yaml.h>

namespace orc {

// Project class method implementations
bool Project::has_source() const
{
    for (const auto& node : nodes_) {
        if (node.node_type == NodeType::SOURCE) {
            return true;
        }
    }
    return false;
}

SourceType Project::get_source_type() const
{
    // Check all source nodes to determine the type
    for (const auto& node : nodes_) {
        if (node.node_type == NodeType::SOURCE) {
            // YC sources have "YC" in their stage name
            if (node.stage_name.find("YC") != std::string::npos ||
                node.stage_name.find("Yc") != std::string::npos ||
                node.stage_name.find("yc") != std::string::npos) {
                return SourceType::YC;
            }
            // Composite sources (PAL_Comp_Source, NTSC_Comp_Source, etc.)
            else if (node.stage_name.find("Source") != std::string::npos) {
                return SourceType::Composite;
            }
        }
    }
    return SourceType::Unknown;
}

namespace project_io {

namespace {

// Convert NodeType to string for serialization
// We save the node type as its enum name for clarity
std::string node_type_to_string(NodeType type) {
    switch (type) {
        case NodeType::SOURCE: return "SOURCE";
        case NodeType::SINK: return "SINK";
        case NodeType::TRANSFORM: return "TRANSFORM";
        case NodeType::MERGER: return "MERGER";
        case NodeType::COMPLEX: return "COMPLEX";
        case NodeType::ANALYSIS_SINK: return "ANALYSIS_SINK";
        default: return "UNKNOWN";
    }
}

// Convert SourceType to string for serialization
std::string source_type_to_string(SourceType type) {
    switch (type) {
        case SourceType::Composite: return "Composite";
        case SourceType::YC: return "YC";
        case SourceType::Unknown: return "Unknown";
        default: return "Unknown";
    }
}

// Convert string to SourceType for deserialization
SourceType source_type_from_string(const std::string& str) {
    if (str == "Composite") return SourceType::Composite;
    if (str == "YC") return SourceType::YC;
    return SourceType::Unknown;
}

// Convert string to NodeType for deserialization
NodeType string_to_node_type(const std::string& str) {
    if (str == "SOURCE") return NodeType::SOURCE;
    if (str == "SINK") return NodeType::SINK;
    if (str == "TRANSFORM") return NodeType::TRANSFORM;
    if (str == "MERGER") return NodeType::MERGER;
    if (str == "COMPLEX") return NodeType::COMPLEX;
    if (str == "ANALYSIS_SINK") return NodeType::ANALYSIS_SINK;
    // Default to TRANSFORM for unknown types (backward compatibility)
    return NodeType::TRANSFORM;
}

// Path resolution utilities
// Expand ${PROJECT_ROOT} variable in a path string
std::string expand_project_root_variable(const std::string& path, const std::string& project_root) {
    const std::string variable = "${PROJECT_ROOT}";
    std::string result = path;
    size_t pos = 0;
    while ((pos = result.find(variable, pos)) != std::string::npos) {
        result.replace(pos, variable.length(), project_root);
        pos += project_root.length();
    }
    return result;
}

// Resolve a path relative to the project root
// - If path is absolute, return it unchanged
// - If path is relative, resolve it relative to project_root
// - Supports ${PROJECT_ROOT} variable expansion
std::string resolve_path(const std::string& path, const std::string& project_root) {
    if (path.empty()) {
        return path;
    }
    
    // First expand any ${PROJECT_ROOT} variables
    std::string expanded = expand_project_root_variable(path, project_root);
    
    // Create a filesystem path
    std::filesystem::path p(expanded);
    
    // If already absolute, return normalized version
    if (p.is_absolute()) {
        try {
            return std::filesystem::weakly_canonical(p).string();
        } catch (const std::filesystem::filesystem_error&) {
            // If canonicalization fails (e.g., path doesn't exist yet), return as-is
            return p.string();
        }
    }
    
    // Resolve relative to project root
    std::filesystem::path resolved = std::filesystem::path(project_root) / p;
    try {
        return std::filesystem::weakly_canonical(resolved).string();
    } catch (const std::filesystem::filesystem_error&) {
        // If canonicalization fails (e.g., path doesn't exist yet), return the combined path
        return resolved.string();
    }
}

// Make a path relative to the project root for saving
// - If path is under project_root, return relative path
// - Otherwise, return absolute path
std::string make_path_relative(const std::string& path, const std::string& project_root) {
    if (path.empty()) {
        return path;
    }
    
    try {
        std::filesystem::path abs_path = std::filesystem::weakly_canonical(path);
        std::filesystem::path root_path = std::filesystem::weakly_canonical(project_root);
        
        // Try to make relative
        auto rel = std::filesystem::relative(abs_path, root_path);
        
        // If the relative path doesn't go up (no ..), use it
        std::string rel_str = rel.string();
        if (rel_str.find("..") == 0) {
            // Path is outside project root, use absolute
            return abs_path.string();
        }
        
        return rel_str;
    } catch (const std::filesystem::filesystem_error&) {
        // If canonicalization fails, return path as-is
        return path;
    }
}

} // anonymous namespace

Project load_project(const std::string& filename) {
    // Resolve the YAML file path to absolute and determine project root
    std::filesystem::path yaml_path;
    try {
        yaml_path = std::filesystem::weakly_canonical(filename);
    } catch (const std::filesystem::filesystem_error& e) {
        throw std::runtime_error("Failed to resolve project file path '" + filename + "': " + e.what());
    }
    
    std::string project_root = yaml_path.parent_path().string();
    
    ORC_LOG_DEBUG("Loading project from: {}", yaml_path.string());
    ORC_LOG_DEBUG("Project root directory: {}", project_root);
    
    YAML::Node root;
    
    try {
        root = YAML::LoadFile(yaml_path.string());
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("Failed to parse YAML file '" + filename + "': " + e.what());
    }
    
    Project project;
    
    // Store project root for path resolution
    project.project_root_ = project_root;
    
    // Validate project section exists
    if (!root["project"]) {
        throw std::runtime_error("Invalid project file '" + filename + "': missing required 'project' section");
    }
    
    // Validate project name (required)
    project.name_ = root["project"]["name"].as<std::string>("");
    if (project.name_.empty()) {
        throw std::runtime_error("Invalid project file '" + filename + "': project name is required");
    }
    
    project.description_ = root["project"]["description"].as<std::string>("");
    project.version_ = root["project"]["version"].as<std::string>("1.0");
    
    // Validate video format (required)
    if (!root["project"]["video_format"]) {
        throw std::runtime_error("Invalid project file '" + filename + "': missing required 'video_format' field. "
                               "Please create a new project or manually add 'video_format: NTSC' or 'video_format: PAL' to the project section.");
    }
    
    std::string format_str = root["project"]["video_format"].as<std::string>();
    project.video_format_ = video_system_from_string(format_str);
    if (project.video_format_ == VideoSystem::Unknown && format_str != "Unknown") {
        throw std::runtime_error("Invalid project file '" + filename + "': invalid video_format '" + format_str + "'. "
                               "Valid values are: NTSC, PAL, PAL-M, or Unknown");
    }
    
    // Load source format (required)
    if (!root["project"]["source_format"]) {
        throw std::runtime_error("Invalid project file '" + filename + "': missing required 'source_format' field. "
                               "Please create a new project or manually add 'source_format: Composite' or 'source_format: YC' to the project section.");
    }
    
    std::string source_format_str = root["project"]["source_format"].as<std::string>();
    project.source_format_ = source_type_from_string(source_format_str);
    if (project.source_format_ == SourceType::Unknown && source_format_str != "Unknown") {
        throw std::runtime_error("Invalid project file '" + filename + "': invalid source_format '" + source_format_str + "'. "
                               "Valid values are: Composite, YC, or Unknown");
    }
    
    // Load DAG nodes
    if (root["dag"] && root["dag"]["nodes"] && root["dag"]["nodes"].IsSequence()) {
        for (const auto& node_yaml : root["dag"]["nodes"]) {
            ProjectDAGNode node;
            node.node_id = NodeID(node_yaml["id"].as<int32_t>(0));
            node.stage_name = node_yaml["stage"].as<std::string>("");
            node.display_name = node_yaml["display_name"].as<std::string>("");
            node.user_label = node_yaml["user_label"].as<std::string>(node.display_name);  // Default to display_name if not present
            node.x_position = node_yaml["x"].as<double>(0.0);
            node.y_position = node_yaml["y"].as<double>(0.0);
            
            // Parse node_type if present (required field)
            if (node_yaml["node_type"]) {
                node.node_type = string_to_node_type(node_yaml["node_type"].as<std::string>("TRANSFORM"));
            } else {
                // Default to TRANSFORM if not specified
                node.node_type = NodeType::TRANSFORM;
            }
            
            // Load parameters
            if (node_yaml["parameters"]) {
                for (const auto& param : node_yaml["parameters"]) {
                    std::string param_name = param.first.as<std::string>();
                    auto param_map = param.second;
                    std::string type = param_map["type"].as<std::string>("string");
                    
                    ORC_LOG_DEBUG("Loading parameter '{}' for node '{}', type={}", 
                                 param_name, node.node_id.to_string(), type);
                    
                    if (type == "int32" || type == "int" || type == "integer") {
                        int value = param_map["value"].as<int>();
                        node.parameters[param_name] = value;
                        ORC_LOG_DEBUG("  Set to int: {}", value);
                    } else if (type == "uint32") {
                        node.parameters[param_name] = param_map["value"].as<uint32_t>();
                    } else if (type == "double") {
                        node.parameters[param_name] = param_map["value"].as<double>();
                    } else if (type == "bool") {
                        node.parameters[param_name] = param_map["value"].as<bool>();
                    } else {
                        // String parameter - check if it's a file path and resolve it
                        std::string value = param_map["value"].as<std::string>();
                        
                        // File path parameters end with "_path" or have type "file_path"
                        bool is_file_path = (type == "file_path" || 
                                           param_name.find("_path") != std::string::npos ||
                                           param_name == "output_path" ||
                                           param_name == "input_path");
                        
                        if (is_file_path && !value.empty()) {
                            std::string original_path = value;
                            value = resolve_path(value, project_root);
                            ORC_LOG_DEBUG("  Resolved path '{}' -> '{}'", original_path, value);
                        }
                        
                        node.parameters[param_name] = value;
                    }
                }
            }
            
            project.nodes_.push_back(node);
        }
    }
    
    // Load DAG edges
    if (root["dag"] && root["dag"]["edges"] && root["dag"]["edges"].IsSequence()) {
        for (const auto& edge_yaml : root["dag"]["edges"]) {
            ProjectDAGEdge edge;
            edge.source_node_id = NodeID(edge_yaml["from"].as<int32_t>(0));
            edge.target_node_id = NodeID(edge_yaml["to"].as<int32_t>(0));
            project.edges_.push_back(edge);
        }
    }
    
    // Validate all loaded edges to ensure they comply with current connection rules
    std::vector<std::string> validation_errors;
    for (const auto& edge : project.edges_) {
        // Find source and target nodes
        auto source_it = std::find_if(project.nodes_.begin(), project.nodes_.end(),
            [&edge](const ProjectDAGNode& n) { return n.node_id == edge.source_node_id; });
        auto target_it = std::find_if(project.nodes_.begin(), project.nodes_.end(),
            [&edge](const ProjectDAGNode& n) { return n.node_id == edge.target_node_id; });
        
        if (source_it == project.nodes_.end()) {
            validation_errors.push_back("Edge references non-existent source node: " + edge.source_node_id.to_string());
            continue;
        }
        if (target_it == project.nodes_.end()) {
            validation_errors.push_back("Edge references non-existent target node: " + edge.target_node_id.to_string());
            continue;
        }
        
        // Validate connection type compatibility
        if (!is_connection_valid(source_it->stage_name, target_it->stage_name)) {
            validation_errors.push_back("Invalid connection: " + source_it->stage_name + 
                                      " (" + edge.source_node_id.to_string() + ") -> " + 
                                      target_it->stage_name + " (" + edge.target_node_id.to_string() + 
                                      ") - incompatible stage types");
        }
        
        // Check fan-out constraint for MANY output stages
        const NodeTypeInfo* source_info = get_node_type_info(source_it->stage_name);
        if (source_info && source_info->min_outputs > 1) {
            // Count outgoing connections from this source
            uint32_t output_count = 0;
            for (const auto& e : project.edges_) {
                if (e.source_node_id == edge.source_node_id) {
                    output_count++;
                }
            }
            if (output_count > 1) {
                validation_errors.push_back("MANY output stage " + source_it->stage_name + 
                                          " (" + edge.source_node_id.to_string() + 
                                          ") has multiple outgoing connections (fan-out not allowed)");
            }
        }
    }
    
    // If there are validation errors, throw exception with details
    if (!validation_errors.empty()) {
        std::string error_msg = "Project file contains invalid connections:\n";
        for (const auto& err : validation_errors) {
            error_msg += "  - " + err + "\n";
        }
        error_msg += "\nPlease fix these connections in the project file or recreate the project.";
        throw std::runtime_error(error_msg);
    }
    
    // Clear modification flag - project is freshly loaded
    project.clear_modified_flag();
    
    return project;
}

void save_project(const Project& project, const std::string& filename) {
    // Determine project root from the save location
    std::filesystem::path save_path;
    try {
        save_path = std::filesystem::absolute(filename);
    } catch (const std::filesystem::filesystem_error& e) {
        throw std::runtime_error("Failed to resolve save path '" + filename + "': " + e.what());
    }
    std::string save_project_root = save_path.parent_path().string();
    
    YAML::Emitter out;
    out << YAML::BeginMap;
    
    // Project metadata
    out << YAML::Key << "project";
    out << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << project.name_;
    if (!project.description_.empty()) {
        out << YAML::Key << "description" << YAML::Value << project.description_;
    }
    out << YAML::Key << "version" << YAML::Value << project.version_;
    
    // Save video format if set
    if (project.video_format_ != VideoSystem::Unknown) {
        out << YAML::Key << "video_format" << YAML::Value << video_system_to_string(project.video_format_);
    }
    
    // Save source format if set
    if (project.source_format_ != SourceType::Unknown) {
        out << YAML::Key << "source_format" << YAML::Value << source_type_to_string(project.source_format_);
    }
    
    out << YAML::EndMap;
    
    // DAG
    out << YAML::Key << "dag";
    out << YAML::Value << YAML::BeginMap;
    
    // Nodes
    out << YAML::Key << "nodes";
    out << YAML::Value << YAML::BeginSeq;
    for (const auto& node : project.nodes_) {
        out << YAML::BeginMap;
        out << YAML::Key << "id" << YAML::Value << node.node_id.value();
        out << YAML::Key << "stage" << YAML::Value << node.stage_name;
        out << YAML::Key << "node_type" << YAML::Value << node_type_to_string(node.node_type);
        if (!node.display_name.empty()) {
            out << YAML::Key << "display_name" << YAML::Value << node.display_name;
        }
        if (!node.user_label.empty()) {
            out << YAML::Key << "user_label" << YAML::Value << node.user_label;
        }
        out << YAML::Key << "x" << YAML::Value << node.x_position;
        out << YAML::Key << "y" << YAML::Value << node.y_position;
        
        // Parameters (if any)
        if (!node.parameters.empty()) {
            out << YAML::Key << "parameters";
            out << YAML::Value << YAML::BeginMap;
            for (const auto& [param_name, param_value] : node.parameters) {
                out << YAML::Key << param_name;
                out << YAML::Value << YAML::BeginMap;
                
                // Write parameter value based on type
                if (std::holds_alternative<int32_t>(param_value)) {
                    out << YAML::Key << "type" << YAML::Value << "int32";
                    out << YAML::Key << "value" << YAML::Value << std::get<int32_t>(param_value);
                } else if (std::holds_alternative<uint32_t>(param_value)) {
                    out << YAML::Key << "type" << YAML::Value << "uint32";
                    out << YAML::Key << "value" << YAML::Value << std::get<uint32_t>(param_value);
                } else if (std::holds_alternative<double>(param_value)) {
                    out << YAML::Key << "type" << YAML::Value << "double";
                    out << YAML::Key << "value" << YAML::Value << std::get<double>(param_value);
                } else if (std::holds_alternative<bool>(param_value)) {
                    out << YAML::Key << "type" << YAML::Value << "bool";
                    out << YAML::Key << "value" << YAML::Value << std::get<bool>(param_value);
                } else if (std::holds_alternative<std::string>(param_value)) {
                    std::string value = std::get<std::string>(param_value);
                    
                    // File path parameters - convert to relative if possible
                    bool is_file_path = (param_name.find("_path") != std::string::npos ||
                                       param_name == "output_path" ||
                                       param_name == "input_path");
                    
                    if (is_file_path && !value.empty() && !save_project_root.empty()) {
                        value = make_path_relative(value, save_project_root);
                    }
                    
                    out << YAML::Key << "type" << YAML::Value << "string";
                    out << YAML::Key << "value" << YAML::Value << value;
                }
                out << YAML::EndMap;
            }
            out << YAML::EndMap;
        }
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    
    // Edges
    out << YAML::Key << "edges";
    out << YAML::Value << YAML::BeginSeq;
    for (const auto& edge : project.edges_) {
        out << YAML::BeginMap;
        out << YAML::Key << "from" << YAML::Value << edge.source_node_id.value();
        out << YAML::Key << "to" << YAML::Value << edge.target_node_id.value();
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    
    out << YAML::EndMap; // end dag
    out << YAML::EndMap; // end root
    
    // Write to file
    std::ofstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + filename);
    }
    file << "# ORC Project File\n";
    file << "# Version: " << project.version_ << "\n\n";
    file << out.c_str();
    file.close();
    
    // Clear modification flag - project has been saved
    project.clear_modified_flag();
}

Project create_empty_project(const std::string& project_name, VideoSystem video_format, SourceType source_format) {
    Project project;
    project.name_ = project_name;
    project.version_ = "1.0";
    project.video_format_ = video_format;
    project.source_format_ = source_format;
    // Empty sources, nodes_, and edges
    // Mark as modified since it's a newly created project
    project.is_modified_ = true;
    return project;
}

void update_project_dag(
    Project& project,
    const std::vector<ProjectDAGNode>& nodes_,
    const std::vector<ProjectDAGEdge>& edges
) {
    // Preserve SOURCE nodes_
    std::vector<ProjectDAGNode> source_nodes;
    for (const auto& node : project.nodes_) {
        if (node.node_type == NodeType::SOURCE) {
            source_nodes.push_back(node);
        }
    }
    
    // Clear all nodes_ and edges
    project.nodes_.clear();
    project.edges_.clear();
    
    // Restore SOURCE nodes_
    for (const auto& source_node : source_nodes) {
        project.nodes_.push_back(source_node);
    }
    
    // Add new nodes_ (should not include SOURCE nodes_ - those are managed separately)
    for (const auto& node : nodes_) {
        if (node.node_type != NodeType::SOURCE) {
            project.nodes_.push_back(node);
        }
    }
    
    // Add new edges
    for (const auto& edge : edges) {
        project.edges_.push_back(edge);
    }
    
    project.is_modified_ = true;
}

NodeID generate_unique_node_id(const Project& project) {
    int32_t max_id = 0;
    
    // Scan all existing nodes to find the highest ID
    for (const auto& node : project.nodes_) {
        if (node.node_id.value() > max_id) {
            max_id = node.node_id.value();
        }
    }
    
    // Return next available ID
    return NodeID(max_id + 1);
}

NodeID add_node(Project& project, const std::string& stage_name, double x_position, double y_position) {
    // Validate that project has been initialized
    if (project.name_.empty()) {
        throw std::runtime_error("Cannot add node to uninitialized project. Create or load a project first.");
    }
    
    // Validate stage name
    const NodeTypeInfo* type_info = get_node_type_info(stage_name);
    if (!type_info) {
        throw std::runtime_error("Invalid stage name: " + stage_name);
    }
    
    // Validate source stage compatibility with project's video and source format
    if (type_info->type == NodeType::SOURCE) {
        // Check source_format if set
        if (project.source_format_ != SourceType::Unknown) {
            bool is_yc_stage = (stage_name.find("YC") != std::string::npos);
            SourceType stage_type = is_yc_stage ? SourceType::YC : SourceType::Composite;
            if (stage_type != project.source_format_) {
                std::string expected_name = (project.source_format_ == SourceType::YC) ? "YC" : "Composite";
                std::string stage_type_name = is_yc_stage ? "YC" : "Composite";
                throw std::runtime_error(
                    "Cannot add " + stage_type_name + " source stage '" + stage_name +
                    "' to a project configured for " + expected_name + " sources."
                );
            }
        }
        
        // Check video_format if set
        if (project.video_format_ != VideoSystem::Unknown) {
            bool is_ntsc_stage = (stage_name.find("NTSC") != std::string::npos);
            bool is_pal_stage = (stage_name.find("PAL") != std::string::npos);
            if (is_ntsc_stage && project.video_format_ != VideoSystem::NTSC) {
                throw std::runtime_error(
                    "Cannot add NTSC source stage '" + stage_name + "' to a PAL project."
                );
            }
            if (is_pal_stage && !(project.video_format_ == VideoSystem::PAL || project.video_format_ == VideoSystem::PAL_M)) {
                throw std::runtime_error(
                    "Cannot add PAL source stage '" + stage_name + "' to an NTSC project."
                );
            }
        }
    }
    
    // Generate unique node ID
    NodeID node_id = generate_unique_node_id(project);
    
    // Create node
    ProjectDAGNode node;
    node.node_id = node_id;
    node.stage_name = stage_name;
    node.node_type = type_info->type;
    node.display_name = type_info->display_name;
    node.user_label = type_info->display_name;  // Initialize user label
    node.x_position = x_position;
    node.y_position = y_position;
    
    project.nodes_.push_back(node);
    project.is_modified_ = true;
    return node_id;
}

void remove_node(Project& project, NodeID node_id) {
    // Find the node
    auto node_it = std::find_if(project.nodes_.begin(), project.nodes_.end(),
        [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == project.nodes_.end()) {
        throw std::runtime_error("Node not found: " + node_id.to_string());
    }
    
    // Check if node can be removed
    std::string reason;
    if (!can_remove_node(project, node_id, &reason)) {
        throw std::runtime_error(reason);
    }
    
    // Remove all edges connected to this node
    project.edges_.erase(
        std::remove_if(project.edges_.begin(), project.edges_.end(),
            [&node_id](const ProjectDAGEdge& e) {
                return e.source_node_id == node_id || e.target_node_id == node_id;
            }),
        project.edges_.end()
    );
    
    // Remove the node
    project.nodes_.erase(node_it);
    
    project.is_modified_ = true;
}

bool can_remove_node(const Project& project, NodeID node_id, std::string* reason) {
    // Find the node
    auto node_it = std::find_if(project.nodes_.begin(), project.nodes_.end(),
        [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == project.nodes_.end()) {
        if (reason) *reason = "Node not found";
        return false;
    }
    
    // Check if node has any connections
    for (const auto& edge : project.edges_) {
        if (edge.source_node_id == node_id || edge.target_node_id == node_id) {
            if (reason) *reason = "Cannot delete node with connections - disconnect all edges first";
            return false;
        }
    }
    
    // Node can be removed
    if (reason) *reason = "";
    return true;
}

void set_node_parameters(Project& project, NodeID node_id, 
                        const std::map<std::string, ParameterValue>& parameters) {
    // Find the node
    auto node_it = std::find_if(project.nodes_.begin(), project.nodes_.end(),
        [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == project.nodes_.end()) {
        throw std::runtime_error("Node not found: " + node_id.to_string());
    }
    
    // Special validation for source nodes_
    if (node_it->node_type == NodeType::SOURCE) {
        auto input_path_it = parameters.find("input_path");
        if (input_path_it != parameters.end() && std::holds_alternative<std::string>(input_path_it->second)) {
            std::string input_path = std::get<std::string>(input_path_it->second);
            
            // Only validate if a path is provided (empty is allowed)
            if (!input_path.empty()) {
                // Validate using the existing add_source_to_project validation logic
                std::string db_path = input_path + ".db";
                
                try {
                    // Load metadata to validate system
                    auto metadata_reader = std::make_shared<TBCMetadataReader>();
                    if (!metadata_reader->open(db_path)) {
                        throw std::runtime_error("Failed to open TBC metadata database: " + db_path);
                    }
                    
                    auto video_params = metadata_reader->read_video_parameters();
                    if (!video_params) {
                        throw std::runtime_error("No video parameters found in TBC metadata");
                    }
                    
                    // Validate decoder
                if (video_params->decoder != "ld-decode" && video_params->decoder != "encode-orc") {
                    throw std::runtime_error(
                        "TBC file was not created by ld-decode or encode-orc (decoder: " + 
                        video_params->decoder + "). This source type requires ld-decode or encode-orc files."
                    );
                }
                
                // Validate system matches the node's stage type
                    if (node_it->stage_name == "PAL_Comp_Source") {
                        if (video_params->system != VideoSystem::PAL && 
                            video_params->system != VideoSystem::PAL_M) {
                            throw std::runtime_error(
                                "Selected TBC file is not PAL format. This is a PAL source node - use an NTSC source node for NTSC files."
                            );
                        }
                    } else if (node_it->stage_name == "NTSC_Comp_Source") {
                        if (video_params->system != VideoSystem::NTSC) {
                            throw std::runtime_error(
                                "Selected TBC file is not NTSC format. This is an NTSC source node - use a PAL source node for PAL files."
                            );
                        }
                    }
                    
                    // Check consistency with other sources in the project
                    for (const auto& other_node : project.nodes_) {
                        if (other_node.node_id != node_id && other_node.node_type == NodeType::SOURCE) {
                            if (other_node.stage_name != node_it->stage_name) {
                                throw std::runtime_error(
                                    "Cannot mix source types. Project already has " + other_node.stage_name + 
                                    " sources, cannot add " + node_it->stage_name + " TBC file."
                                );
                            }
                        }
                    }
                    
                    // Validate against project's source_format if set
                    if (project.source_format_ != SourceType::Unknown) {
                        bool is_yc_source = (node_it->stage_name.find("YC") != std::string::npos);
                        SourceType expected_type = is_yc_source ? SourceType::YC : SourceType::Composite;
                        
                        if (expected_type != project.source_format_) {
                            std::string expected_name = (project.source_format_ == SourceType::YC) ? "YC" : "Composite";
                            std::string actual_name = is_yc_source ? "YC" : "Composite";
                            throw std::runtime_error(
                                "Source type mismatch. Project is configured for " + expected_name + 
                                " sources, but attempting to add " + actual_name + " source (" + 
                                node_it->stage_name + ")."
                            );
                        }
                    }
                } catch (const std::exception& e) {
                    throw std::runtime_error(
                        std::string("Failed to validate TBC file: ") + e.what()
                    );
                }
            }
        }
    }
    
    node_it->parameters = parameters;
    
    // Source stages handle their own caching via set_parameters()
    
    project.is_modified_ = true;
}

void set_node_position(Project& project, NodeID node_id, double x_position, double y_position) {
    // Find the node
    auto node_it = std::find_if(project.nodes_.begin(), project.nodes_.end(),
        [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == project.nodes_.end()) {
        throw std::runtime_error("Node not found: " + node_id.to_string());
    }
    
    node_it->x_position = x_position;
    node_it->y_position = y_position;
    project.is_modified_ = true;
}

void set_node_label(Project& project, NodeID node_id, const std::string& label) {
    // Find the node
    auto node_it = std::find_if(project.nodes_.begin(), project.nodes_.end(),
        [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == project.nodes_.end()) {
        throw std::runtime_error("Node not found: " + node_id.to_string());
    }
    
    node_it->user_label = label;
    project.is_modified_ = true;
}

void add_edge(Project& project, NodeID source_node_id, NodeID target_node_id) {
    // Validate that project has been initialized
    if (project.name_.empty()) {
        throw std::runtime_error("Cannot add edge to uninitialized project. Create or load a project first.");
    }
    
    // Find source and target nodes_
    auto source_it = std::find_if(project.nodes_.begin(), project.nodes_.end(),
        [&source_node_id](const ProjectDAGNode& n) { return n.node_id == source_node_id; });
    auto target_it = std::find_if(project.nodes_.begin(), project.nodes_.end(),
        [&target_node_id](const ProjectDAGNode& n) { return n.node_id == target_node_id; });
    
    if (source_it == project.nodes_.end()) {
        throw std::runtime_error("Source node not found: " + source_node_id.to_string());
    }
    if (target_it == project.nodes_.end()) {
        throw std::runtime_error("Target node not found: " + target_node_id.to_string());
    }
    
    // Validate connection
    if (!is_connection_valid(source_it->stage_name, target_it->stage_name)) {
        throw std::runtime_error("Invalid connection between " + source_it->stage_name + 
                               " and " + target_it->stage_name);
    }
    
    // Check if edge already exists
    for (const auto& edge : project.edges_) {
        if (edge.source_node_id == source_node_id && edge.target_node_id == target_node_id) {
            throw std::runtime_error("Edge already exists");
        }
    }
    
    // Count existing connections to check limits
    uint32_t source_output_count = 0;
    uint32_t target_input_count = 0;
    for (const auto& edge : project.edges_) {
        if (edge.source_node_id == source_node_id) source_output_count++;
        if (edge.target_node_id == target_node_id) target_input_count++;
    }
    
    // Get node type info to check limits
    const NodeTypeInfo* source_info = get_node_type_info(source_it->stage_name);
    const NodeTypeInfo* target_info = get_node_type_info(target_it->stage_name);
    
    // MANY output stages (min_outputs > 1) cannot fan-out
    // They can only have ONE outgoing connection
    if (source_info && source_info->min_outputs > 1 && source_output_count > 0) {
        throw std::runtime_error("MANY output stages cannot fan-out (already has outgoing connection)");
    }
    
    if (source_info && source_output_count >= source_info->max_outputs) {
        throw std::runtime_error("Source node has reached maximum outputs");
    }
    if (target_info && target_input_count >= target_info->max_inputs) {
        throw std::runtime_error("Target node has reached maximum inputs");
    }
    
    // Add edge
    ProjectDAGEdge edge;
    edge.source_node_id = source_node_id;
    edge.target_node_id = target_node_id;
    project.edges_.push_back(edge);
    project.is_modified_ = true;
}

void remove_edge(Project& project, NodeID source_node_id, NodeID target_node_id) {
    // Find and remove the edge
    auto edge_it = std::find_if(project.edges_.begin(), project.edges_.end(),
        [&source_node_id, &target_node_id](const ProjectDAGEdge& e) {
            return e.source_node_id == source_node_id && e.target_node_id == target_node_id;
        });
    
    if (edge_it == project.edges_.end()) {
        throw std::runtime_error("Edge not found");
    }
    
    project.edges_.erase(edge_it);
    project.is_modified_ = true;
}

void clear_project(Project& project) {
    project.name_.clear();
    project.version_.clear();
    project.nodes_.clear();
    project.edges_.clear();
    project.clear_modified_flag();
}

void set_project_name(Project& project, const std::string& name) {
    if (name.empty()) {
        throw std::invalid_argument("Project name cannot be empty");
    }
    project.name_ = name;
    project.is_modified_ = true;
}

void set_project_description(Project& project, const std::string& description) {
    project.description_ = description;
    project.is_modified_ = true;
}

void set_video_format(Project& project, VideoSystem video_format) {
    project.video_format_ = video_format;
    project.is_modified_ = true;
}

void set_source_format(Project& project, SourceType source_format) {
    project.source_format_ = source_format;
    project.is_modified_ = true;
}

bool can_trigger_node(const Project& project, NodeID node_id, std::string* reason) {
    // Find the node
    auto it = std::find_if(project.nodes_.begin(), project.nodes_.end(),
        [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (it == project.nodes_.end()) {
        if (reason) *reason = "Node not found";
        return false;
    }
    
    // Check if stage is triggerable
    try {
        auto stage = StageRegistry::instance().create_stage(it->stage_name);
        if (!stage) {
            if (reason) *reason = "Failed to create stage";
            return false;
        }
        
        auto* trigger_stage = dynamic_cast<TriggerableStage*>(stage.get());
        if (!trigger_stage) {
            if (reason) *reason = "Stage is not triggerable";
            return false;
        }
        
        if (reason) *reason = "";
        return true;
    } catch (const std::exception& e) {
        if (reason) *reason = std::string("Error: ") + e.what();
        return false;
    }
}

bool trigger_node(Project& project, NodeID node_id, std::string& status_out, TriggerProgressCallback progress_callback) {
    // Find the node
    auto it = std::find_if(project.nodes_.begin(), project.nodes_.end(),
        [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (it == project.nodes_.end()) {
        status_out = "Node not found";
        throw std::runtime_error("Node '" + node_id.to_string() + "' not found");
    }
    
    // Get stage instance
    auto stage = StageRegistry::instance().create_stage(it->stage_name);
    if (!stage) {
        status_out = "Failed to create stage";
        throw std::runtime_error("Failed to create stage '" + it->stage_name + "'");
    }
    
    // Check if triggerable
    auto* trigger_stage = dynamic_cast<TriggerableStage*>(stage.get());
    if (!trigger_stage) {
        status_out = "Stage is not triggerable";
        throw std::runtime_error("Stage is not triggerable");
    }
    
    // Set progress callback if provided
    if (progress_callback) {
        trigger_stage->set_progress_callback(progress_callback);
    }
    
    // Build DAG
    auto dag = project_to_dag(project);
    
    // CRITICAL: Keep executor alive during trigger to prevent dangling pointers
    // Artifacts from execute_to_node may contain representations (like CorrectedVideoFieldRepresentation)
    // that hold raw pointers to stages owned by the executor/DAG. These stages must outlive the trigger operation.
    auto executor = std::make_shared<DAGExecutor>();
    
    // Get inputs by executing to predecessor nodes
    std::vector<ArtifactPtr> inputs;
    for (const auto& edge : project.edges_) {
        if (edge.target_node_id == node_id) {
            auto node_outputs = executor->execute_to_node(*dag, edge.source_node_id);
            if (node_outputs.find(edge.source_node_id) != node_outputs.end()) {
                auto& outputs = node_outputs[edge.source_node_id];
                // For now, assume single output per stage (most common case)
                if (!outputs.empty()) {
                    inputs.push_back(outputs[0]);
                }
            }
        }
    }
    
    if (inputs.empty()) {
        status_out = "No inputs available";
        throw std::runtime_error("No inputs for node '" + node_id.to_string() + "'");
    }
    
    // Trigger (DAG and executor stay alive, keeping stage instances valid)
    ObservationContext observation_context;
    bool success = trigger_stage->trigger(inputs, it->parameters, observation_context);
    status_out = trigger_stage->get_trigger_status();
    
    // DAG and executor destroyed here AFTER trigger completes, ensuring stages outlive artifacts
    return success;
}

std::future<std::pair<bool, std::string>> trigger_node_async(
    Project& project,
    NodeID node_id,
    TriggerProgressCallback progress_callback)
{
    // Build DAG and get inputs BEFORE launching async task
    // This ensures the DAG's lifetime is managed by the async task
    auto dag = project_to_dag(project);
    auto executor = std::make_shared<DAGExecutor>();
    
    // Get inputs by executing to predecessor nodes
    std::vector<ArtifactPtr> inputs;
    const auto& edges = project.get_edges();
    for (const auto& edge : edges) {
        if (edge.target_node_id == node_id) {
            auto node_outputs = executor->execute_to_node(*dag, edge.source_node_id);
            if (node_outputs.find(edge.source_node_id) != node_outputs.end()) {
                auto& outputs = node_outputs[edge.source_node_id];
                if (!outputs.empty()) {
                    inputs.push_back(outputs[0]);
                }
            }
        }
    }
    
    // Launch async task, capturing DAG and inputs to keep stages alive
    return std::async(std::launch::async, [dag, node_id, inputs, progress_callback]() -> std::pair<bool, std::string> {
        try {
            // Find the target node in the DAG
            const DAGNode* target_node = nullptr;
            for (const auto& node : dag->nodes()) {
                if (node.node_id == node_id) {
                    target_node = &node;
                    break;
                }
            }
            
            if (!target_node) {
                return {false, "Node not found in DAG"};
            }
            
            auto trigger_stage = dynamic_cast<TriggerableStage*>(target_node->stage.get());
            if (!trigger_stage) {
                return {false, "Stage is not triggerable"};
            }
            
            if (progress_callback) {
                trigger_stage->set_progress_callback(progress_callback);
            }
            
            ObservationContext observation_context;
            bool success = trigger_stage->trigger(inputs, target_node->parameters, observation_context);
            std::string status = trigger_stage->get_trigger_status();
            
            // DAG will be destroyed here, keeping stages alive throughout trigger
            return {success, status};
            
        } catch (const std::exception& e) {
            return {false, std::string("Exception: ") + e.what()};
        }
    });
}

std::string find_source_file_for_node(const Project& project, NodeID node_id) {
    // Find node
    auto node_it = std::find_if(project.nodes_.begin(), project.nodes_.end(),
        [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == project.nodes_.end()) {
        return "";
    }
    
    // Check if this node has input_path
    auto param_it = node_it->parameters.find("input_path");
    if (param_it != node_it->parameters.end()) {
        if (auto* path = std::get_if<std::string>(&param_it->second)) {
            return *path;
        }
    }
    
    // Trace back through DAG
    std::queue<NodeID> to_visit;
    std::set<NodeID> visited;
    
    for (const auto& edge : project.edges_) {
        if (edge.target_node_id == node_id) {
            to_visit.push(edge.source_node_id);
        }
    }
    
    while (!to_visit.empty()) {
        NodeID current_id = to_visit.front();
        to_visit.pop();
        
        if (visited.count(current_id)) continue;
        visited.insert(current_id);
        
        auto current_it = std::find_if(project.nodes_.begin(), project.nodes_.end(),
            [&current_id](const ProjectDAGNode& n) { return n.node_id == current_id; });
        
        if (current_it != project.nodes_.end()) {
            auto path_param = current_it->parameters.find("input_path");
            if (path_param != current_it->parameters.end()) {
                if (auto* path = std::get_if<std::string>(&path_param->second)) {
                    return *path;
                }
            }
            
            for (const auto& edge : project.edges_) {
                if (edge.target_node_id == current_id) {
                    to_visit.push(edge.source_node_id);
                }
            }
        }
    }
    
    return "";
}

NodeCapabilities get_node_capabilities(const Project& project, NodeID node_id) {
    NodeCapabilities caps;
    caps.node_id = node_id;
    
    // Find the node
    auto it = std::find_if(project.nodes_.begin(), project.nodes_.end(),
        [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (it == project.nodes_.end()) {
        caps.remove_reason = "Node not found";
        caps.trigger_reason = "Node not found";
        caps.inspect_reason = "Node not found";
        return caps;
    }
    
    caps.stage_name = it->stage_name;
    caps.node_label = it->user_label.empty() ? it->display_name : it->user_label;
    
    // Check can_remove - cannot remove if node has connections
    bool has_connections = false;
    for (const auto& edge : project.edges_) {
        if (edge.source_node_id == node_id || edge.target_node_id == node_id) {
            has_connections = true;
            break;
        }
    }
    caps.can_remove = !has_connections;
    if (has_connections) {
        caps.remove_reason = "Cannot remove connected node";
    }
    
    // Check can_trigger - must be TriggerableStage
    try {
        auto stage = StageRegistry::instance().create_stage(it->stage_name);
        if (stage) {
            auto* trigger_stage = dynamic_cast<TriggerableStage*>(stage.get());
            caps.can_trigger = (trigger_stage != nullptr);
            if (!caps.can_trigger) {
                caps.trigger_reason = "Stage is not triggerable";
            } else {
                // For sink stages, check if output filename is set
                auto node_type = stage->get_node_type_info().type;
                if (node_type == NodeType::SINK) {
                    // All sink stages use "output_path" parameter
                    auto param_it = it->parameters.find("output_path");
                    bool has_output = false;
                    
                    if (param_it != it->parameters.end() && 
                        std::holds_alternative<std::string>(param_it->second)) {
                        std::string path = std::get<std::string>(param_it->second);
                        has_output = !path.empty();
                    }
                    
                    if (!has_output) {
                        caps.can_trigger = false;
                        caps.trigger_reason = "No output filename specified";
                    }
                }
            }
            
            // Check can_inspect - must have generate_report
            caps.can_inspect = stage->generate_report().has_value();
            if (!caps.can_inspect) {
                caps.inspect_reason = "Stage does not support inspection";
            }
        } else {
            caps.trigger_reason = "Failed to create stage";
            caps.inspect_reason = "Failed to create stage";
        }
    } catch (const std::exception& e) {
        caps.trigger_reason = std::string("Error: ") + e.what();
        caps.inspect_reason = std::string("Error: ") + e.what();
    }
    
    return caps;
}

} // namespace project_io
} // namespace orc
