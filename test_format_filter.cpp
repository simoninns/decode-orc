// Quick test to verify video format filtering works correctly
#include "orc/core/include/node_type.h"
#include "orc/core/include/tbc_metadata.h"
#include <iostream>

int main() {
    using namespace orc;
    
    std::cout << "Testing video format compatibility filtering:\n\n";
    
    // Test NTSC project
    std::cout << "NTSC Project:\n";
    std::cout << "  LDNTSCSource compatible: " << is_stage_compatible_with_format("LDNTSCSource", VideoSystem::NTSC) << "\n";
    std::cout << "  LDPALSource compatible: " << is_stage_compatible_with_format("LDPALSource", VideoSystem::NTSC) << "\n";
    std::cout << "  dropout_correct compatible: " << is_stage_compatible_with_format("dropout_correct", VideoSystem::NTSC) << "\n";
    std::cout << "  stacker compatible: " << is_stage_compatible_with_format("stacker", VideoSystem::NTSC) << "\n";
    
    std::cout << "\nPAL Project:\n";
    std::cout << "  LDNTSCSource compatible: " << is_stage_compatible_with_format("LDNTSCSource", VideoSystem::PAL) << "\n";
    std::cout << "  LDPALSource compatible: " << is_stage_compatible_with_format("LDPALSource", VideoSystem::PAL) << "\n";
    std::cout << "  dropout_correct compatible: " << is_stage_compatible_with_format("dropout_correct", VideoSystem::PAL) << "\n";
    std::cout << "  stacker compatible: " << is_stage_compatible_with_format("stacker", VideoSystem::PAL) << "\n";
    
    return 0;
}
