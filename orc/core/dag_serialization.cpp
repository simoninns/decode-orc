// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include "dag_serialization.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iomanip>

namespace orc {
namespace dag_serialization {

GUIDAG load_dag_from_yaml(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open DAG file: " + filename);
    }
    
    GUIDAG dag;
    std::string line;
    bool in_nodes = false;
    bool in_edges = false;
    GUIDAGNode current_node;
    bool has_node_id = false;
    
    while (std::getline(file, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        
        // Skip comments
        if (line[0] == '#') continue;
        
        // Parse top-level fields
        if (line.find("name:") == 0) {
            size_t quote1 = line.find('"');
            size_t quote2 = line.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                dag.name = line.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        } else if (line.find("version:") == 0) {
            size_t quote1 = line.find('"');
            size_t quote2 = line.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                dag.version = line.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        } else if (line.find("nodes:") == 0) {
            if (has_node_id) {
                dag.nodes.push_back(current_node);
                has_node_id = false;
            }
            in_nodes = true;
            in_edges = false;
        } else if (line.find("edges:") == 0) {
            if (has_node_id) {
                dag.nodes.push_back(current_node);
                has_node_id = false;
            }
            in_nodes = false;
            in_edges = true;
        } else if (in_nodes) {
            if (line.find("- node_id:") == 0) {
                if (has_node_id) {
                    dag.nodes.push_back(current_node);
                }
                current_node = GUIDAGNode();
                size_t quote1 = line.find('"');
                size_t quote2 = line.find('"', quote1 + 1);
                if (quote1 != std::string::npos && quote2 != std::string::npos) {
                    current_node.node_id = line.substr(quote1 + 1, quote2 - quote1 - 1);
                }
                has_node_id = true;
            } else if (has_node_id) {
                size_t colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string key = line.substr(0, colon);
                    std::string value = line.substr(colon + 1);
                    
                    // Trim whitespace
                    size_t k_start = key.find_first_not_of(" \t");
                    size_t v_start = value.find_first_not_of(" \t");
                    if (k_start != std::string::npos && v_start != std::string::npos) {
                        key = key.substr(k_start);
                        value = value.substr(v_start);
                        
                        // Remove quotes
                        if (!value.empty() && value[0] == '"') {
                            size_t end_quote = value.find('"', 1);
                            if (end_quote != std::string::npos) {
                                value = value.substr(1, end_quote - 1);
                            }
                        }
                        
                        if (key == "stage_name") {
                            current_node.stage_name = value;
                        } else if (key == "x_position") {
                            current_node.x_position = std::stod(value);
                        } else if (key == "y_position") {
                            current_node.y_position = std::stod(value);
                        } else {
                            // It's a parameter - try to parse as different types
                            if (value == "true") {
                                current_node.parameters[key] = true;
                            } else if (value == "false") {
                                current_node.parameters[key] = false;
                            } else {
                                // Try as number
                                try {
                                    if (value.find('.') != std::string::npos) {
                                        current_node.parameters[key] = std::stod(value);
                                    } else {
                                        current_node.parameters[key] = static_cast<uint32_t>(std::stoul(value));
                                    }
                                } catch (...) {
                                    // Use as string
                                    current_node.parameters[key] = value;
                                }
                            }
                        }
                    }
                }
            }
        } else if (in_edges) {
            if (line.find("- source:") == 0) {
                GUIDAGEdge edge;
                size_t quote1 = line.find('"');
                size_t quote2 = line.find('"', quote1 + 1);
                if (quote1 != std::string::npos && quote2 != std::string::npos) {
                    edge.source_node_id = line.substr(quote1 + 1, quote2 - quote1 - 1);
                }
                
                // Read target on next line
                if (std::getline(file, line)) {
                    size_t target_start = line.find("target:");
                    if (target_start != std::string::npos) {
                        quote1 = line.find('"', target_start);
                        quote2 = line.find('"', quote1 + 1);
                        if (quote1 != std::string::npos && quote2 != std::string::npos) {
                            edge.target_node_id = line.substr(quote1 + 1, quote2 - quote1 - 1);
                        }
                    }
                }
                dag.edges.push_back(edge);
            }
        }
    }
    
    // Save last node
    if (has_node_id) {
        dag.nodes.push_back(current_node);
    }
    
    return dag;
}

void save_dag_to_yaml(const GUIDAG& dag, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file for writing: " + filename);
    }
    
    file << std::fixed << std::setprecision(2);
    
    // Write header
    file << "name: \"" << dag.name << "\"\n";
    file << "version: \"" << dag.version << "\"\n";
    file << "\n";
    
    // Write nodes
    file << "nodes:\n";
    for (const auto& node : dag.nodes) {
        file << "  - node_id: \"" << node.node_id << "\"\n";
        file << "    stage_name: \"" << node.stage_name << "\"\n";
        file << "    x_position: " << node.x_position << "\n";
        file << "    y_position: " << node.y_position << "\n";
        
        // Write parameters
        for (const auto& [key, value] : node.parameters) {
            file << "    " << key << ": ";
            std::visit([&file](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, bool>) {
                    file << (arg ? "true" : "false");
                } else if constexpr (std::is_same_v<T, std::string>) {
                    file << "\"" << arg << "\"";
                } else {
                    file << arg;
                }
            }, value);
            file << "\n";
        }
    }
    file << "\n";
    
    // Write edges
    file << "edges:\n";
    for (const auto& edge : dag.edges) {
        file << "  - source: \"" << edge.source_node_id << "\"\n";
        file << "    target: \"" << edge.target_node_id << "\"\n";
    }
}

} // namespace dag_serialization
} // namespace orc
