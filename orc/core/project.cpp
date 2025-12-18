// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include "project.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace orc {
namespace project_io {

namespace {

// Escape string for YAML
std::string escape_yaml_string(const std::string& str) {
    // Simple escaping: quote strings with special characters
    if (str.find_first_of(":\"'#") != std::string::npos) {
        return "\"" + str + "\"";
    }
    return str;
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
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open project file: " + filename);
    }
    
    Project project;
    std::string line;
    std::string current_section;
    
    ProjectSource current_source;
    ProjectDAGNode current_node;
    ProjectDAGEdge current_edge;
    bool in_source = false;
    bool in_node = false;
    bool in_edge = false;
    
    // Simple YAML parser for our project format
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        size_t indent = start;
        line = line.substr(start);
        
        // Check for section headers (indent 0)
        if (indent == 0) {
            if (line.find("project:") == 0) {
                current_section = "project";
            } else if (line.find("sources:") == 0) {
                current_section = "sources";
            } else if (line.find("dag:") == 0) {
                current_section = "dag";
            }
            continue;
        }
        
        // Parse based on section and indent level
        if (current_section == "project" && indent == 2) {
            if (line.find("name:") == 0) {
                project.name = line.substr(6);
            } else if (line.find("version:") == 0) {
                project.version = line.substr(9);
            }
        }
        else if (current_section == "sources") {
            if (indent == 2 && line[0] == '-') {
                // New source
                if (in_source) {
                    project.sources.push_back(current_source);
                }
                current_source = ProjectSource();
                in_source = true;
            } else if (indent == 4 && in_source) {
                if (line.find("id:") == 0) {
                    current_source.source_id = std::stoi(line.substr(4));
                } else if (line.find("path:") == 0) {
                    current_source.tbc_path = line.substr(6);
                } else if (line.find("name:") == 0) {
                    current_source.display_name = line.substr(6);
                }
            }
        }
        else if (current_section == "dag") {
            if (indent == 2 && line.find("nodes:") == 0) {
                current_section = "dag_nodes";
                if (in_source) {
                    project.sources.push_back(current_source);
                    in_source = false;
                }
            } else if (indent == 2 && line.find("edges:") == 0) {
                current_section = "dag_edges";
                if (in_node) {
                    project.nodes.push_back(current_node);
                    in_node = false;
                }
            }
        }
        else if (current_section == "dag_nodes") {
            if (indent == 4 && line[0] == '-') {
                // New node
                if (in_node) {
                    project.nodes.push_back(current_node);
                }
                current_node = ProjectDAGNode();
                current_node.source_id = -1;  // Default
                in_node = true;
            } else if (indent == 6 && in_node) {
                if (line.find("id:") == 0) {
                    current_node.node_id = line.substr(4);
                } else if (line.find("stage:") == 0) {
                    current_node.stage_name = line.substr(7);
                } else if (line.find("display_name:") == 0) {
                    current_node.display_name = line.substr(14);
                } else if (line.find("x:") == 0) {
                    current_node.x_position = std::stod(line.substr(3));
                } else if (line.find("y:") == 0) {
                    current_node.y_position = std::stod(line.substr(3));
                } else if (line.find("source_id:") == 0) {
                    current_node.source_id = std::stoi(line.substr(11));
                }
                // TODO: Parse parameters if needed
            }
        }
        else if (current_section == "dag_edges") {
            if (indent == 4 && line[0] == '-') {
                // New edge
                if (in_edge) {
                    project.edges.push_back(current_edge);
                }
                current_edge = ProjectDAGEdge();
                in_edge = true;
            } else if (indent == 6 && in_edge) {
                if (line.find("from:") == 0) {
                    current_edge.source_node_id = line.substr(6);
                } else if (line.find("to:") == 0) {
                    current_edge.target_node_id = line.substr(4);
                }
            }
        }
    }
    
    // Add last items if any
    if (in_source) {
        project.sources.push_back(current_source);
    }
    if (in_node) {
        project.nodes.push_back(current_node);
    }
    if (in_edge) {
        project.edges.push_back(current_edge);
    }
    
    return project;
}

void save_project(const Project& project, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + filename);
    }
    
    // Write YAML format manually (simple implementation)
    file << "# ORC Project File\n";
    file << "# Version: " << project.version << "\n\n";
    
    // Project metadata
    file << "project:\n";
    file << "  name: " << escape_yaml_string(project.name) << "\n";
    file << "  version: " << escape_yaml_string(project.version) << "\n\n";
    
    // Sources
    file << "sources:\n";
    for (const auto& source : project.sources) {
        file << "  - id: " << source.source_id << "\n";
        file << "    path: " << escape_yaml_string(source.tbc_path) << "\n";
        file << "    name: " << escape_yaml_string(source.display_name) << "\n";
    }
    file << "\n";
    
    // DAG
    file << "dag:\n";
    file << "  nodes:\n";
    for (const auto& node : project.nodes) {
        file << "    - id: " << escape_yaml_string(node.node_id) << "\n";
        file << "      stage: " << escape_yaml_string(node.stage_name) << "\n";
        if (!node.display_name.empty()) {
            file << "      display_name: " << escape_yaml_string(node.display_name) << "\n";
        }
        file << "      x: " << node.x_position << "\n";
        file << "      y: " << node.y_position << "\n";
        if (node.source_id >= 0) {
            file << "      source_id: " << node.source_id << "\n";
        }
        
        // Parameters (if any)
        if (!node.parameters.empty()) {
            file << "      parameters:\n";
            for (const auto& [param_name, param_value] : node.parameters) {
                file << "        " << escape_yaml_string(param_name) << ":\n";
                
                // Write parameter value based on type
                if (std::holds_alternative<int32_t>(param_value)) {
                    file << "          type: int32\n";
                    file << "          value: " << std::get<int32_t>(param_value) << "\n";
                } else if (std::holds_alternative<uint32_t>(param_value)) {
                    file << "          type: uint32\n";
                    file << "          value: " << std::get<uint32_t>(param_value) << "\n";
                } else if (std::holds_alternative<double>(param_value)) {
                    file << "          type: double\n";
                    file << "          value: " << std::get<double>(param_value) << "\n";
                } else if (std::holds_alternative<bool>(param_value)) {
                    file << "          type: bool\n";
                    file << "          value: " << (std::get<bool>(param_value) ? "true" : "false") << "\n";
                } else if (std::holds_alternative<std::string>(param_value)) {
                    file << "          type: string\n";
                    file << "          value: " << escape_yaml_string(std::get<std::string>(param_value)) << "\n";
                }
            }
        }
    }
    
    file << "  edges:\n";
    for (const auto& edge : project.edges) {
        file << "    - from: " << escape_yaml_string(edge.source_node_id) << "\n";
        file << "      to: " << escape_yaml_string(edge.target_node_id) << "\n";
    }
    
    file.close();
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
    start_node.display_name = "Source: " + source.display_name;
    start_node.source_id = next_id;
    start_node.x_position = -450.0;
    start_node.y_position = next_id * 100.0;  // Offset Y position for multiple sources
    project.nodes.push_back(start_node);
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
        project.edges.push_back(edge);
    }
}

} // namespace project_io
} // namespace orc
