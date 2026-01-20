/*
 * File:        cc_sink_stage.h
 * Module:      orc-core
 * Purpose:     Closed Caption Sink Stage - exports CC data to SCC or plain text
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_CC_SINK_STAGE_H
#define ORC_CORE_CC_SINK_STAGE_H

#include "dag_executor.h"
#include "stage_parameter.h"
#include "node_type.h"
#include "video_field_representation.h"
#include "../ld_sink/ld_sink_stage.h"  // For TriggerableStage interface
#include <string>
#include <memory>
#include <functional>
#include <atomic>

namespace orc {

/**
 * @brief Closed Caption output format
 */
enum class CCExportFormat {
    SCC,        ///< Scenarist SCC V1.0 format (industry standard)
    PLAIN_TEXT  ///< Plain text with control codes stripped
};

/**
 * @brief Closed Caption Sink Stage
 * 
 * Extracts closed caption data from TBC metadata and exports it in either:
 * - Scenarist SCC V1.0 format: Industry standard subtitle format with timing
 * - Plain text format: Human-readable text with control codes stripped
 * 
 * This is a SINK stage - it has inputs but no outputs.
 * 
 * The closed caption data is extracted from line 21 (NTSC) or line 22 (PAL)
 * of the VBI area. Each caption consists of two bytes of data, which can be
 * either command bytes (0x10-0x1F) or character bytes (0x20-0x7E).
 * 
 * SCC Format:
 * - Header: "Scenarist_SCC V1.0"
 * - Timestamps in format HH:MM:SS:FF (non-drop frame)
 * - Hex byte pairs (e.g., "1441" for bytes 0x14 and 0x41)
 * - Captions separated by blank lines
 * 
 * Plain Text Format:
 * - Only printable ASCII characters (0x20-0x7E)
 * - Control codes (0x10-0x1F) are stripped out
 * - Preserves caption timing boundaries with blank lines
 * 
 * Parameters:
 * - output_path: Output file path (.scc or .txt)
 * - format: Export format (SCC or PLAIN_TEXT)
 */
class CCSinkStage : public DAGStage, 
                    public ParameterizedStage, 
                    public TriggerableStage {
public:
    CCSinkStage();
    ~CCSinkStage() override = default;
    
    // DAGStage interface
    std::string version() const override { return "1.0"; }
    NodeTypeInfo get_node_type_info() const override;
    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters,
        ObservationContext& observation_context) override;
    
    size_t required_input_count() const override { return 1; }
    size_t output_count() const override { return 0; }  // Sink has no outputs
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors(VideoSystem project_format = VideoSystem::Unknown, SourceType source_type = SourceType::Unknown) const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
    
    // TriggerableStage interface
    bool trigger(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters,
        ObservationContext& observation_context
    ) override;
    
    std::string get_trigger_status() const override;
    
    void set_progress_callback(TriggerProgressCallback callback) override {
        progress_callback_ = callback;
    }
    
    bool is_trigger_in_progress() const override {
        return is_processing_.load();
    }

private:
    // Export functions
    bool export_scc(const VideoFieldRepresentation* vfr, 
                    const std::string& output_path,
                    VideoFormat format);
    
    bool export_plain_text(const VideoFieldRepresentation* vfr,
                          const std::string& output_path,
                          VideoFormat format);
    
    // Helper functions
    std::string generate_timestamp(int32_t field_index, VideoFormat format) const;
    int32_t sanity_check_data(int32_t data_byte) const;
    bool is_control_code(uint8_t byte) const;
    bool is_printable_char(uint8_t byte) const;
    
    // Member variables
    std::map<std::string, ParameterValue> parameters_;
    TriggerProgressCallback progress_callback_;
    std::atomic<bool> is_processing_{false};
    std::atomic<bool> cancel_requested_{false};
};

} // namespace orc

#endif // ORC_CORE_CC_SINK_STAGE_H
