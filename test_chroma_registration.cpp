#include "stage_registry.h"
#include "stages/chroma_sink/chroma_sink_stage.h"
#include <iostream>

int main() {
    auto& registry = orc::StageRegistry::instance();
    
    std::cout << "Registered stages:\n";
    for (const auto& name : registry.get_registered_stages()) {
        std::cout << "  - " << name << "\n";
    }
    
    std::cout << "\nChecking for chroma_sink: " 
              << (registry.has_stage("chroma_sink") ? "FOUND" : "NOT FOUND") 
              << "\n";
    
    if (registry.has_stage("chroma_sink")) {
        auto stage = registry.create_stage("chroma_sink");
        auto info = stage->get_node_type_info();
        std::cout << "\nChroma Sink Info:\n";
        std::cout << "  Display Name: " << info.display_name << "\n";
        std::cout << "  Description: " << info.description << "\n";
        std::cout << "  Node Type: " << static_cast<int>(info.type) << "\n";
    }
    
    return 0;
}
