/*
 * File:        stacker_stage.h
 * Module:      orc-core
 * Purpose:     Multi-source TBC stacking stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include "video_field_representation.h"
#include "stage_parameter.h"
#include "dag_executor.h"
#include "previewable_stage.h"
#include <memory>
#include <vector>

namespace orc {

/**
 * @brief Stacker stage - combines multiple TBC sources into one superior output
 * 
 * This stage implements the functionality of the legacy ld-disc-stacker tool.
 * It analyzes corresponding fields from multiple TBC captures of the same
 * LaserDisc and selects the best data for each field, effectively reducing
 * dropouts and improving overall signal quality.
 * 
 * Stacking Modes:
 * - Mean (0): Simple averaging of all sources
 * - Median (1): Median value of all sources
 * - Smart Mean (2): Mean of values within threshold distance from median
 * - Smart Neighbor (3): Use neighboring pixels to guide selection
 * - Neighbor (4): Use neighboring pixels for context-aware selection
 * 
 * Use cases:
 * - Combining multiple captures of the same disc to reduce dropouts
 * - Improving signal quality by selecting best source per pixel
 * - Reducing noise through intelligent multi-source processing
 */
class StackerStage : public DAGStage, public ParameterizedStage, public PreviewableStage {
public:
    StackerStage();
    
    // DAGStage interface
    std::string version() const override { return "1.0"; }    
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::MERGER,
            "stacker",
            "Stacker",
            "Combine multiple TBC sources by stacking fields for superior output quality (1 input = passthrough)",
            1, 16,  // 1 to 16 inputs
            1, UINT32_MAX,  // Many outputs
            VideoFormatCompatibility::ALL
        };
    }    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters) override;
    
    size_t required_input_count() const override { return 1; }  // At least 1 input (passthrough mode)
    
    // PreviewableStage interface
    bool supports_preview() const override { return true; }
    std::vector<PreviewOption> get_preview_options() const override;
    PreviewImage render_preview(const std::string& option_id, uint64_t index,
                               PreviewNavigationHint hint = PreviewNavigationHint::Random) const override;
    size_t output_count() const override { return 1; }
    
    // Stage inspection
    std::optional<StageReport> generate_report() const override;
    
    /**
     * @brief Stack multiple fields into one output field
     * 
     * @param sources Vector of input field representations (2-8 sources)
     * @return Stacked output field representation
     */
    std::shared_ptr<const VideoFieldRepresentation> process(
        const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources) const;
    
    /**
     * @brief Get minimum number of inputs required
     */
    static size_t min_input_count() { return 1; }
    
    /**
     * @brief Get maximum number of inputs allowed
     */
    static size_t max_input_count() { return 16; }
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors(VideoSystem project_format = VideoSystem::Unknown) const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;

private:
    // Stacking parameters
    int32_t m_mode;              // Stacking mode (-1=Auto, 0=Mean, 1=Median, 2=Smart Mean, 3=Smart Neighbor, 4=Neighbor)
    int32_t m_smart_threshold;   // Threshold for smart modes (0-128, default 15)
    bool m_no_diff_dod;          // Disable differential dropout detection
    bool m_passthrough;          // Pass through dropouts present on all sources
    bool m_reverse;              // Reverse field order
    
    // Store parameters for inspection
    std::map<std::string, ParameterValue> parameters_;
    
    /**
     * @brief Stack a single field from multiple sources
     * 
     * @param field_id Field ID to process
     * @param sources Input field representations
     * @param output_samples Output buffer for stacked samples
     * @param output_dropouts Output dropout regions
     */
    void stack_field(
        FieldID field_id,
        const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources,
        std::vector<uint16_t>& output_samples,
        std::vector<DropoutRegion>& output_dropouts) const;
    
    /**
     * @brief Apply stacking mode to pixel values
     * 
     * @param values Pixel values from all sources
     * @param values_n North neighbor values
     * @param values_s South neighbor values
     * @param values_e East neighbor values
     * @param values_w West neighbor values
     * @param all_dropout Flags for dropout status
     * @return Stacked pixel value
     */
    uint16_t stack_mode(
        const std::vector<uint16_t>& values,
        const std::vector<uint16_t>& values_n,
        const std::vector<uint16_t>& values_s,
        const std::vector<uint16_t>& values_e,
        const std::vector<uint16_t>& values_w,
        const std::vector<bool>& all_dropout) const;
    
    /**
     * @brief Calculate median of values
     */
    uint16_t median(std::vector<uint16_t> values) const;
    
    /**
     * @brief Calculate mean of values
     */
    int32_t mean(const std::vector<uint16_t>& values) const;
    
    /**
     * @brief Find value closest to target
     */
    uint16_t closest(const std::vector<uint16_t>& values, int32_t target) const;
    
    /**
     * @brief Perform differential dropout detection
     * 
     * @param input_values Values marked as dropouts
     * @param video_params Video parameters for black level
     * @return Recovered values (if any)
     */
    std::vector<uint16_t> diff_dod(
        const std::vector<uint16_t>& input_values,
        const VideoParameters& video_params) const;
    
    // Cached output for preview rendering
    mutable std::shared_ptr<const VideoFieldRepresentation> cached_output_;
};

} // namespace orc
