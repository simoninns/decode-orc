/*
 * File:        stacker_stage.h
 * Module:      orc-core
 * Purpose:     Multi-source TBC stacking stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "video_field_representation.h"
#include "stage_parameter.h"
#include "dag_executor.h"
#include "previewable_stage.h"
#include "lru_cache.h"
#include <memory>
#include <vector>
#include <thread>
#include <mutex>

namespace orc {

// Forward declarations
class StackerStage;

/**
 * @brief Stacked video field representation
 * 
 * This wraps multiple source field representations and stacks them on-demand.
 * Field alignment is expected to be done by field_map stages before the stacker -
 * the stacker simply stacks field N from all sources together.
 */
class StackedVideoFieldRepresentation : public VideoFieldRepresentationWrapper {
public:
    StackedVideoFieldRepresentation(
        const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources,
        StackerStage* stage);
    
    ~StackedVideoFieldRepresentation() = default;
    
    // Override field_range to report combined range from all sources
    FieldIDRange field_range() const override;
    
    // Only override methods that are actually modified by this stage
    const uint16_t* get_line(FieldID id, size_t line) const override;
    std::vector<uint16_t> get_field(FieldID id) const override;
    
    // Dual-channel support for YC sources
    bool has_separate_channels() const override;
    const uint16_t* get_line_luma(FieldID id, size_t line) const override;
    const uint16_t* get_line_chroma(FieldID id, size_t line) const override;
    std::vector<uint16_t> get_field_luma(FieldID id) const override;
    std::vector<uint16_t> get_field_chroma(FieldID id) const override;
    
    // Override dropout hints - after stacking, dropouts are the ones that remain
    std::vector<DropoutRegion> get_dropout_hints(FieldID id) const override;
    
    // Get number of sources available for a specific frame
    size_t get_source_count(FieldID id) const;
    
    // Allow stage to access private members
    // Override audio methods to provide stacked audio
    uint32_t get_audio_sample_count(FieldID id) const override;
    std::vector<int16_t> get_audio_samples(FieldID id) const override;
    bool has_audio() const override;
    
    // Override EFM methods to provide stacked EFM
    uint32_t get_efm_sample_count(FieldID id) const override;
    std::vector<uint8_t> get_efm_samples(FieldID id) const override;
    bool has_efm() const override;
    
    friend class StackerStage;
    
private:
    std::vector<std::shared_ptr<const VideoFieldRepresentation>> sources_;
    StackerStage* stage_;  // Non-owning pointer to stage for lazy stacking
    
    // Stacked field data - LRU cache of whole fields for fast access
    // Cache size: 600 fields × ~1.4MB/field = ~840MB max (composite)
    //           : 600 fields × ~2.8MB/field = ~1680MB max (YC - dual channels)
    mutable LRUCache<FieldID, std::vector<uint16_t>> stacked_fields_;
    static constexpr size_t MAX_CACHED_FIELDS = 600;
    
    // Dual-channel caches for YC sources
    mutable LRUCache<FieldID, std::vector<uint16_t>> stacked_luma_fields_;
    mutable LRUCache<FieldID, std::vector<uint16_t>> stacked_chroma_fields_;
    
    // Dropout regions for stacked fields
    mutable LRUCache<FieldID, std::vector<DropoutRegion>> stacked_dropouts_;
    
    // Stacked audio data cache
    mutable LRUCache<FieldID, std::vector<int16_t>> stacked_audio_;
    
    // Stacked EFM data cache
    mutable LRUCache<FieldID, std::vector<uint8_t>> stacked_efm_;
    
    // Best field index for each field (for video stacking quality tracking)
    mutable LRUCache<FieldID, size_t> best_field_index_;
    
    // Mutex to protect cache operations from concurrent access
    mutable std::mutex cache_mutex_;
    
    // Ensure field is stacked (lazy)
    void ensure_field_stacked(FieldID field_id) const;
    
    // Get index of best source field (fewest dropouts)
    size_t get_best_source_index(FieldID field_id) const;
};

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
        const std::map<std::string, ParameterValue>& parameters, ObservationContext& observation_context) override;
    
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
    std::vector<ParameterDescriptor> get_parameter_descriptors(VideoSystem project_format = VideoSystem::Unknown, SourceType source_type = SourceType::Unknown) const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;

    // Allow StackedVideoFieldRepresentation to access stack_field
    friend class StackedVideoFieldRepresentation;

public:
    /**
     * @brief Audio stacking mode
     */
    enum class AudioStackingMode {
        DISABLED,  // No audio stacking - use best field's audio
        MEAN,      // Mean averaging of audio samples
        MEDIAN     // Median averaging of audio samples
    };
    
    /**
     * @brief EFM stacking mode
     */
    enum class EFMStackingMode {
        DISABLED,  // No EFM stacking - use best field's EFM
        MEAN,      // Mean averaging of EFM t-values
        MEDIAN     // Median averaging of EFM t-values
    };
    
private:
    // Stacking parameters
    int32_t m_mode;              // Stacking mode (-1=Auto, 0=Mean, 1=Median, 2=Smart Mean, 3=Smart Neighbor, 4=Neighbor)
    int32_t m_smart_threshold;   // Threshold for smart modes (0-128, default 15)
    bool m_no_diff_dod;          // Disable differential dropout detection
    bool m_passthrough;          // Pass through dropouts present on all sources
    int32_t m_thread_count;      // Number of threads for parallel processing (0=auto, max=hardware concurrency)
    AudioStackingMode m_audio_stacking_mode;  // Audio stacking mode (default: mean)
    EFMStackingMode m_efm_stacking_mode;      // EFM stacking mode (default: mean)
    
    // Store parameters for inspection
    std::map<std::string, ParameterValue> parameters_;
    
    // Cache the stacked representation to preserve LRU caches across execute() calls
    mutable std::shared_ptr<const VideoFieldRepresentation> cached_output_;
    mutable std::vector<std::shared_ptr<const VideoFieldRepresentation>> cached_sources_;
    
    /**
     * @brief Stack a single field from multiple sources
     * 
     * @param field_id Field ID to stack (same ID used for all sources - alignment done by field_map stages)
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
     * @brief Stack a single YC field from multiple sources (separate Y and C channels)
     * 
     * @param field_id Field ID to stack
     * @param sources Input field representations (must all have separate channels)
     * @param output_luma Output buffer for stacked luma samples
     * @param output_chroma Output buffer for stacked chroma samples
     * @param output_dropouts Output dropout regions (applies to both Y and C)
     */
    void stack_field_yc(
        FieldID field_id,
        const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources,
        std::vector<uint16_t>& output_luma,
        std::vector<uint16_t>& output_chroma,
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
     * @brief Process a range of lines (for multi-threading)
     * 
     * @param start_line Starting line (inclusive)
     * @param end_line Ending line (exclusive)
     * @param width Field width in samples
     * @param all_fields Pre-loaded field data from all sources
     * @param field_valid Validity flags for each source
     * @param all_dropouts Dropout regions for each source
     * @param num_sources Number of sources
     * @param video_params Video parameters
     * @param output_samples Output sample buffer (shared, each thread writes to its own lines)
     * @param output_dropouts Output dropout regions (per-thread)
     * @param total_dropouts Dropout counter (per-thread)
     * @param total_diff_dod_recoveries Recovery counter (per-thread)
     * @param total_stacked_pixels Stacked pixel counter (per-thread)
     */
    void process_lines_range(
        size_t start_line,
        size_t end_line,
        size_t width,
        const std::vector<std::vector<uint16_t>>& all_fields,
        const std::vector<bool>& field_valid,
        const std::vector<std::vector<DropoutRegion>>& all_dropouts,
        size_t num_sources,
        const VideoParameters& video_params,
        std::vector<uint16_t>& output_samples,
        std::vector<DropoutRegion>& output_dropouts,
        size_t& total_dropouts,
        size_t& total_diff_dod_recoveries,
        size_t& total_stacked_pixels) const;
    
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
    
    /**
     * @brief Stack audio samples from multiple sources
     * 
     * @param field_id Field ID
     * @param sources Input field representations
     * @param best_source_index Index of best source (used when audio stacking disabled)
     * @return Stacked audio samples (interleaved stereo)
     */
    std::vector<int16_t> stack_audio(
        FieldID field_id,
        const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources,
        size_t best_source_index) const;
    
    /**
     * @brief Calculate mean of audio sample values
     */
    int16_t audio_mean(const std::vector<int16_t>& values) const;
    
    /**
     * @brief Calculate median of audio sample values
     */
    int16_t audio_median(std::vector<int16_t> values) const;
    
    /**
     * @brief Stack EFM t-values from multiple sources
     * 
     * @param field_id Field ID
     * @param sources Input field representations
     * @param best_source_index Index of best source (used when EFM stacking disabled)
     * @return Stacked EFM t-values
     */
    std::vector<uint8_t> stack_efm(
        FieldID field_id,
        const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources,
        size_t best_source_index) const;
    
    /**
     * @brief Calculate mean of EFM t-value samples
     */
    uint8_t efm_mean(const std::vector<uint8_t>& values) const;
    
    /**
     * @brief Calculate median of EFM t-value samples
     */
    uint8_t efm_median(std::vector<uint8_t> values) const;
};

} // namespace orc
