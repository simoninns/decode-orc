/*
 * File:        vbi_decoder.h
 * Module:      orc-core
 * Purpose:     VBI decoding API for GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_VBI_DECODER_H
#define ORC_CORE_VBI_DECODER_H

#include "biphase_observer.h"
#include "field_id.h"
#include "node_id.h"
#include "lru_cache.h"
#include <memory>
#include <optional>
#include <string>

namespace orc {

// Forward declarations
class DAG;
class VideoFieldRepresentation;

/**
 * @brief Complete VBI information for a field
 * 
 * This structure contains all decoded VBI data for display in the GUI,
 * matching the information shown in ld-analyse's VBI dialog.
 */
struct VBIFieldInfo {
    FieldID field_id;
    
    // Raw VBI data (3 lines: 16, 17, 18)
    std::array<int32_t, 3> vbi_data;
    
    // Frame/timecode information
    std::optional<int32_t> picture_number;       // CAV frame number
    std::optional<CLVTimecode> clv_timecode;     // CLV timecode
    std::optional<int32_t> chapter_number;       // Chapter marker
    
    // Control codes
    bool stop_code_present = false;              // Picture stop code
    bool lead_in = false;                        // Lead-in code
    bool lead_out = false;                       // Lead-out code
    std::optional<std::string> user_code;        // User code string
    
    // Programme status (original specification)
    std::optional<ProgrammeStatus> programme_status;
    
    // Amendment 2 status
    std::optional<Amendment2Status> amendment2_status;
    
    // Validity flags
    bool has_vbi_data = false;                   // True if VBI was successfully decoded
    std::string error_message;                    // Error description if decoding failed
};

/**
 * @brief VBI decoder for extracting VBI information from DAG nodes
 * 
 * This class provides the business logic for VBI decoding, allowing the GUI
 * to remain a thin display layer. It extracts BiphaseObservation data from
 * the DAG and formats it for display.
 */
class VBIDecoder {
public:
    /**
     * @brief Construct a VBI decoder
     * @param dag The DAG to extract VBI data from
     */
    explicit VBIDecoder(std::shared_ptr<const DAG> dag);
    
    ~VBIDecoder() = default;
    
    /**
     * @brief Get VBI information for a specific field at a node
     * 
     * @param node_id The node to query
     * @param field_id The field to get VBI data for
     * @return VBI information, or empty optional if not available
     * 
     * This method:
     * 1. Renders the field at the specified node
     * 2. Extracts BiphaseObservation from the field's observations
     * 3. Formats the data into VBIFieldInfo for display
     */
    std::optional<VBIFieldInfo> get_vbi_for_field(
        const NodeID& node_id,
        FieldID field_id);
    
    /**
     * @brief Update the DAG reference
     * @param dag New DAG to use for decoding
     */
    void update_dag(std::shared_ptr<const DAG> dag);

private:
    /**
     * @brief Extract VBI info from a VideoFieldRepresentation
     * @param representation The field representation to extract from
     * @param field_id The field ID
     * @return VBI information, or empty optional if no BiphaseObservation found
     */
    std::optional<VBIFieldInfo> extract_vbi_from_representation(
        const VideoFieldRepresentation* representation,
        FieldID field_id);
    
    // Cache key for VBI results: (node_id, field_id, dag_version)
    struct VBICacheKey {
        NodeID node_id;
        uint64_t field_id_value;
        uint64_t dag_version;
        
        bool operator==(const VBICacheKey& other) const {
            return node_id == other.node_id && 
                   field_id_value == other.field_id_value &&
                   dag_version == other.dag_version;
        }
    };
    
    struct VBICacheKeyHash {
        size_t operator()(const VBICacheKey& key) const {
            return std::hash<NodeID>()(key.node_id) ^ 
                   (std::hash<uint64_t>()(key.field_id_value) << 1) ^
                   (std::hash<uint64_t>()(key.dag_version) << 2);
        }
    };
    
    std::shared_ptr<const DAG> dag_;
    uint64_t dag_version_ = 0;
    std::unique_ptr<class DAGFieldRenderer> renderer_;
    LRUCache<VBICacheKey, VBIFieldInfo, VBICacheKeyHash> vbi_cache_{32};
};

} // namespace orc

#endif // ORC_CORE_VBI_DECODER_H
