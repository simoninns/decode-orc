// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include "project.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <yaml-cpp/yaml.h>

namespace orc {
namespace project_io {

namespace {

// Convert NodeType to string for serialization
// We save the node type as its enum name for clarity
std::string node_type_to_string(NodeType type) {
    switch (type) {
        case NodeType::SOURCE: return "SOURCE";
        case NodeType::SINK: return "SINK";
        case NodeType::TRANSFORM: return "TRANSFORM";
        case NodeType::SPLITTER: return "SPLITTER";
        case NodeType::MERGER: return "MERGER";
        case NodeType::COMPLEX: return "COMPLEX";
        default: return "UNKNOWN";
    }
}

// Convert string to NodeType for deserialization
NodeType string_to_node_type(const std::string& str) {
    if (str == "SOURCE") return NodeType::SOURCE;
    if (str == "SINK") return NodeType::SINK;
    if (str == "TRANSFORM") return NodeType::TRANSFORM;
    if (str == "SPLITTER") return NodeType::SPLITTER;
    if (str == "MERGER") return NodeType::MERGER;
    if (str == "COMPLEX") return NodeType::COMPLEX;
    // Default to TRANSFORM for unknown types (backward compatibility)
    return NodeType::TRANSFORM;
}
} // anonymous namespace

// Helper to extract display name from TBC path (public in project_io namespace)
std::string extract_display_name(const std::string& tbc_path) {
    std::filesystem::path p(tbc_path);
    std::string name = p.stem().string();
    
    // Remove .tbc extension if present in stem
    if (name.size() > 4 && name.substr(name.size() - 4) == ".tbc") {
        name = name.substr(0, name.size() - 4);
    }
    
    return name;
}

Project load_project(const std::string& filename) {
    YAML::Node root = YAML::LoadFile(filename);
    
    Project project;
    
    // Load project metadata
    if (root["project"]) {
        project.name = root["project"]["name"].as<std::string>("");
        project.version = root["project"]["version"].as<std::string>("1.0");
    }
    
    // Load sources
    if (root["sources"] && root["sources"].IsSequence()) {
        for (const auto& source_node : root["sources"]) {
            ProjectSource source;
            source.source_id = source_node["id"].as<int>(-1);
            source.tbc_path = source_node["path"].as<std::string>("");
            source.display_name = source_node["name"].as<std::string>("");
            project.sources.push_back(source);
        }
    }
    
    // Load DAG nodes
    if (root["dag"] && root["dag"]["nodes"] && root["dag"]["nodes"].IsSequence()) {
        for (const auto& node_yaml : root["dag"]["nodes"]) {
            ProjectDAGNode node;
            node.node_id = node_yaml["id"].as<std::string>("");
            node.stage_name = node_yaml["stage"].as<std::string>("");
            node.display_name = node_yaml["display_name"].as<std::string>("");
            node.x_position = node_yaml["x"].as<double>(0.0);
            node.y_position = node_yaml["y"].as<double>(0.0);
            node.source_id = node_yaml["source_id"].as<int>(-1);
            
            // Parse node_type if present
            if (node_yaml["node_type"]) {
                node.node_type = string_to_node_type(node_yaml["node_type"].as<std::string>("TRANSFORM"));
            } else {
                // Backward compatibility: infer from stage_name
                node.node_type = NodeType::TRANSFORM;
                if (node.stage_name == "Source") {
                    node.node_type = NodeType::SOURCE;
                }
            }
            
            // TODO: Parse parameters if needed
            
            project.nodes.push_back(node);
        }
    }
    
    // Load DAG edges
    if (root["dag"] && root["dag"]["edges"] && root["dag"]["edges"].IsSequence()) {
        for (const auto& edge_yaml : root["dag"]["edges"]) {
            ProjectDAGEdge edge;
            edge.source_node_id = edge_yaml["from"].as<std::string>("");
            edge.target_node_id = edge_yaml["to"].as<std::string>("");
            project.edges.push_back(edge);
        }
    }
    
    // Clear modification flag - project is freshly loaded
    project.clear_modified_flag();
    
    return project;
}

void save_project(const Project& project, const std::string& filename) {
    YAML::Emitter out;
    out << YAML::BeginMap;
    
    // Project metadata
    out << YAML::Key << "project";
    out << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << project.name;
    out << YAML::Key << "version" << YAML::Value << project.version;
    out << YAML::EndMap;
    
    // Sources
    out << YAML::Key << "sources";
    out << YAML::Value << YAML::BeginSeq;
    for (const auto& source : project.sources) {
        out << YAML::BeginMap;
        out << YAML::Key << "id" << YAML::Value << source.source_id;
        out << YAML::Key << "path" << YAML::Value << source.tbc_path;
        out << YAML::Key << "name" << YAML::Value << source.display_name;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    
    // DAG
    out << YAML::Key << "dag";
    out << YAML::Value << YAML::BeginMap;
    
    // Nodes
    out << YAML::Key << "nodes";
    out << YAML::Value << YAML::BeginSeq;
    for (const auto& node : project.nodes) {
        out << YAML::BeginMap;
        out << YAML::Key << "id" << YAML::Value << node.node_id;
        out << YAML::Key << "stage" << YAML::Value << node.stage_name;
        out << YAML::Key << "node_type" << YAML::Value << node_type_to_string(node.node_type);
        if (!node.display_name.empty()) {
            out << YAML::Key << "display_name" << YAML::Value << node.display_name;
        }
        out << YAML::Key << "x" << YAML::Value << node.x_position;
        out << YAML::Key << "y" << YAML::Value << node.y_position;
        if (node.source_id >= 0) {
            out << YAML::Key << "source_id" << YAML::Value << node.source_id;
        }
        
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
                    out << YAML::Key << "type" << YAML::Value << "string";
                    out << YAML::Key << "value" << YAML::Value << std::get<std::string>(param_value);
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
    for (const auto& edge : project.edges) {
        out << YAML::BeginMap;
        out << YAML::Key << "from" << YAML::Value << edge.source_node_id;
        out << YAML::Key << "to" << YAML::Value << edge.target_node_id;
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
    file << "# Version: " << project.version << "\n\n";
    file << out.c_str();
    file.close();
    
    // Clear modification flag - project has been saved
    project.clear_modified_flag();
}

Project create_single_source_project(const std::string& tbc_path, const std::string& project_name) {
    Project project;
    
    // Set project name
    if (project_name.empty()) {
        project.name = extract_display_name(tbc_path);
    } else {
        project.name = project_name;
    }
    project.version = "1.0";
    
    // Add single source with ID 0
    ProjectSource source;
    source.source_id = 0;
    source.tbc_path = tbc_path;
    source.display_name = extract_display_name(tbc_path);
    project.sources.push_back(source);
    
    // Create START node for this source
    ProjectDAGNode start_node;
    start_node.node_id = "start_0";
    start_node.stage_name = "Source";
    start_node.node_type = NodeType::SOURCE;
    start_node.display_name = "Source: " + source.display_name;
    start_node.x_position = -450.0;
    start_node.y_position = 0.0;
    start_node.source_id = 0;
    project.nodes.push_back(start_node);
    
    return project;
}

Project create_empty_project(const std::string& project_name) {
    Project project;
    project.name = project_name;
    project.version = "1.0";
    // Empty sources, nodes, and edges
    return project;
}

void add_source_to_project(Project& project, const std::string& tbc_path) {
    // Check for duplicate paths
    for (const auto& existing_source : project.sources) {
        if (existing_source.tbc_path == tbc_path) {
            throw std::runtime_error("Source already exists in project: " + tbc_path);
        }
    }
    
    // Find next available source ID
    int next_id = 0;
    for (const auto& source : project.sources) {
        if (source.source_id >= next_id) {
            next_id = source.source_id + 1;
        }
    }
    
    // Create new source
    ProjectSource source;
    source.source_id = next_id;
    source.tbc_path = tbc_path;
    source.display_name = extract_display_name(tbc_path);
    project.sources.push_back(source);
    
    // Create START node for this source
    ProjectDAGNode start_node;
    start_node.node_id = "start_" + std::to_string(next_id);
    start_node.stage_name = "Source";
    start_node.node_type = NodeType::SOURCE;
    start_node.display_name = "Source: " + source.display_name;
    start_node.source_id = next_id;
    start_node.x_position = -450.0;
    start_node.y_position = next_id * 100.0;  // Offset Y position for multiple sources
    project.nodes.push_back(start_node);
    project.is_modified = true;
}

void remove_source_from_project(Project& project, int source_id) {
    // Find and remove the source
    auto source_it = std::find_if(
        project.sources.begin(),
        project.sources.end(),
        [source_id](const ProjectSource& s) { return s.source_id == source_id; }
    );
    
    if (source_it == project.sources.end()) {
        throw std::runtime_error("Source ID not found: " + std::to_string(source_id));
    }
    
    project.sources.erase(source_it);
    
    // Remove the START node for this source
    std::string start_node_id = "start_" + std::to_string(source_id);
    project.nodes.erase(
        std::remove_if(
            project.nodes.begin(),
            project.nodes.end(),
            [&start_node_id](const ProjectDAGNode& n) { 
                return n.node_id == start_node_id; 
            }
        ),
        project.nodes.end()
    );
    
    // Remove any edges connected to the START node
    project.edges.erase(
        std::remove_if(
            project.edges.begin(),
            project.edges.end(),
            [&start_node_id](const ProjectDAGEdge& e) {
                return e.source_node_id == start_node_id || e.target_node_id == start_node_id;
            }
        ),
        project.edges.end()
    );
    
    // If no sources remain, clear the entire DAG
    if (project.sources.empty()) {
        project.nodes.clear();
        project.edges.clear();
    }
    
    project.is_modified = true;
}

void update_project_dag(
    Project& project,
    const std::vector<ProjectDAGNode>& nodes,
    const std::vector<ProjectDAGEdge>& edges
) {
    // Preserve START nodes (they belong to sources)
    std::vector<ProjectDAGNode> start_nodes;
    for (const auto& node : project.nodes) {
        if (node.stage_name == "Source") {
            start_nodes.push_back(node);
        }
    }
    
    // Clear all nodes and edges
    project.nodes.clear();
    project.edges.clear();
    
    // Restore START nodes
    for (const auto& start_node : start_nodes) {
        project.nodes.push_back(start_node);
    }
    
    // Add new nodes (should not include START nodes)
    for (const auto& node : nodes) {
        if (node.stage_name != "Source") {
            project.nodes.push_back(node);
        }
    }
    
    // Add new edges
    for (const auto& edge : edges) {
    
    project.is_modified = true;
        project.edges.push_back(edge);
    }
}

std::string generate_unique_node_id(const Project& project) {
    int max_id = 0;
    
    // Scan all existing nodes to find the highest node_X ID
    for (const auto& node : project.nodes) {
        // Check if node_id follows "node_N" pattern
        if (node.node_id.find("node_") == 0) {
            try {
                int id_num = std::stoi(node.node_id.substr(5));
                if (id_num > max_id) {
                    max_id = id_num;
                }
            } catch (...) {
                // Not a valid node_N format, skip
            }
        }
    }
    
    // Return next available ID
    return "node_" + std::to_string(max_id + 1);
}

std::string add_node(Project& project, const std::string& stage_name, double x_position, double y_position) {
    // Validate stage name
    const NodeTypeInfo* type_info = get_node_type_info(stage_name);
    if (!type_info) {
        throw std::runtime_error("Invalid stage name: " + stage_name);
    }
    
    // Don't allow manual creation of source nodes
    if (type_info->type == NodeType::SOURCE) {
        throw std::runtime_error("Source nodes are created automatically. Use add_source_to_project() instead.");
    }
    
    // Generate unique node ID
    std::string node_id = generate_unique_node_id(project);
    
    // Create node
    ProjectDAGNode node;
    node.node_id = node_id;
    node.stage_name = stage_name;
    node.node_type = type_info->type;
    node.display_name = type_info->display_name;
    node.x_position = x_position;
    node.y_position = y_position;
    node.source_id = -1;
    
    project.nodes.push_back(node);
    project.is_modified = true;
    return node_id;
}

void remove_node(Project& project, const std::string& node_id) {
    // Find the node
    auto node_it = std::find_if(project.nodes.begin(), project.nodes.end(),
        [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == project.nodes.end()) {
        throw std::runtime_error("Node not found: " + node_id);
    }
    
    // Don't allow removal of source nodes
    if (node_it->node_type == NodeType::SOURCE) {
        throw std::runtime_error("Cannot remove source node. Use remove_source_from_project() instead.");
    }
    
    // Remove all edges connected to this node
    project.edges.erase(
        std::remove_if(project.edges.begin(), project.edges.end(),
            [&node_id](const ProjectDAGEdge& e) {
                return e.source_node_id == node_id || e.target_node_id == node_id;
            }),
        project.edges.end()
    );
    
    // Remove the node
    project.nodes.erase(node_it);    project.is_modified = true;}

void change_node_type(Project& project, const std::string& node_id, const std::string& new_stage_name) {
    // Validate new stage name
    const NodeTypeInfo* type_info = get_node_type_info(new_stage_name);
    if (!type_info) {
        throw std::runtime_error("Invalid stage name: " + new_stage_name);
    }
    
    // Find the node
    auto node_it = std::find_if(project.nodes.begin(), project.nodes.end(),
        [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == project.nodes.end()) {
        throw std::runtime_error("Node not found: " + node_id);
    }
    
    // Don't allow changing source nodes
    if (node_it->node_type == NodeType::SOURCE) {
        throw std::runtime_error("Cannot change type of source node");
    }
    
    // Check if node has any connections - if so, prevent type change
    // This prevents breaking the DAG by switching to incompatible types
    bool has_connections = false;
    for (const auto& edge : project.edges) {
        if (edge.source_node_id == node_id || edge.target_node_id == node_id) {
            has_connections = true;
            break;
        }
    }
    
    if (has_connections) {
        throw std::runtime_error("Cannot change type of node with connections. Disconnect all edges first.");
    }
    
    // Update node
    node_it->stage_name = new_stage_name;
    node_it->node_type = type_info->type;
    node_it->display_name = type_info->display_name;
    // Clear parameters when changing type
    node_it->parameters.clear();
    project.is_modified = true;
}

bool can_change_node_type(const Project& project, const std::string& node_id, std::string* reason) {
    // Find the node
    auto node_it = std::find_if(project.nodes.begin(), project.nodes.end(),
        [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == project.nodes.end()) {
        if (reason) *reason = "Node not found";
        return false;
    }
    
    // Cannot change source nodes
    if (node_it->node_type == NodeType::SOURCE) {
        if (reason) *reason = "Cannot change type of source node";
        return false;
    }
    
    // Check if node has any connections
    for (const auto& edge : project.edges) {
        if (edge.source_node_id == node_id || edge.target_node_id == node_id) {
            if (reason) *reason = "Node has connections - disconnect all edges first";
            return false;
        }
    }
    
    // Node type can be changed
    if (reason) *reason = "";
    return true;
}

void set_node_parameters(Project& project, const std::string& node_id, 
                        const std::map<std::string, ParameterValue>& parameters) {
    // Find the node
    auto node_it = std::find_if(project.nodes.begin(), project.nodes.end(),
        [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == project.nodes.end()) {
        throw std::runtime_error("Node not found: " + node_id);
    }
    
    node_it->parameters = parameters;
    project.is_modified = true;
}

void set_node_position(Project& project, const std::string& node_id, double x_position, double y_position) {
    // Find the node
    auto node_it = std::find_if(project.nodes.begin(), project.nodes.end(),
        [&node_id](const ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == project.nodes.end()) {
        throw std::runtime_error("Node not found: " + node_id);
    }
    
    node_it->x_position = x_position;
    node_it->y_position = y_position;
    project.is_modified = true;
}

void add_edge(Project& project, const std::string& source_node_id, const std::string& target_node_id) {
    // Find source and target nodes
    auto source_it = std::find_if(project.nodes.begin(), project.nodes.end(),
        [&source_node_id](const ProjectDAGNode& n) { return n.node_id == source_node_id; });
    auto target_it = std::find_if(project.nodes.begin(), project.nodes.end(),
        [&target_node_id](const ProjectDAGNode& n) { return n.node_id == target_node_id; });
    
    if (source_it == project.nodes.end()) {
        throw std::runtime_error("Source node not found: " + source_node_id);
    }
    if (target_it == project.nodes.end()) {
        throw std::runtime_error("Target node not found: " + target_node_id);
    }
    
    // Validate connection
    if (!is_connection_valid(source_it->stage_name, target_it->stage_name)) {
        throw std::runtime_error("Invalid connection between " + source_it->stage_name + 
                               " and " + target_it->stage_name);
    }
    
    // Check if edge already exists
    for (const auto& edge : project.edges) {
        if (edge.source_node_id == source_node_id && edge.target_node_id == target_node_id) {
            throw std::runtime_error("Edge already exists");
        }
    }
    
    // Count existing connections to check limits
    uint32_t source_output_count = 0;
    uint32_t target_input_count = 0;
    for (const auto& edge : project.edges) {
        if (edge.source_node_id == source_node_id) source_output_count++;
        if (edge.target_node_id == target_node_id) target_input_count++;
    }
    
    // Get node type info to check limits
    const NodeTypeInfo* source_info = get_node_type_info(source_it->stage_name);
    const NodeTypeInfo* target_info = get_node_type_info(target_it->stage_name);
    
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
    project.edges.push_back(edge);
    project.is_modified = true;
}

void remove_edge(Project& project, const std::string& source_node_id, const std::string& target_node_id) {
    // Find and remove the edge
    auto edge_it = std::find_if(project.edges.begin(), project.edges.end(),
        [&source_node_id, &target_node_id](const ProjectDAGEdge& e) {
            return e.source_node_id == source_node_id && e.target_node_id == target_node_id;
        });
    
    if (edge_it == project.edges.end()) {
        throw std::runtime_error("Edge not found");
    }
    
    project.edges.erase(edge_it);
    project.is_modified = true;
}

void clear_project(Project& project) {
    project.name.clear();
    project.version.clear();
    project.sources.clear();
    project.nodes.clear();
    project.edges.clear();
    project.clear_modified_flag();
}

} // namespace project_io
} // namespace orc
