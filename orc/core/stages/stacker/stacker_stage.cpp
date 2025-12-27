/*
 * File:        stacker_stage.cpp
 * Module:      orc-core
 * Purpose:     Multi-source TBC stacking stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include <stacker_stage.h>
#include <stage_registry.h>
#include <preview_helpers.h>
#include <logging.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace orc {

// Register the stage
static StageRegistration stacker_registration([]() {
    return std::make_shared<StackerStage>();
});

StackerStage::StackerStage()
    : m_mode(-1)              // Auto mode
    , m_smart_threshold(15)   // Default threshold
    , m_no_diff_dod(false)
    , m_passthrough(false)
    , m_reverse(false)
{
}

std::vector<ArtifactPtr> StackerStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters)
{
    if (inputs.empty()) {
        throw DAGExecutionError("StackerStage requires at least 1 input");
    }
    
    if (inputs.size() > 16) {
        throw DAGExecutionError("StackerStage supports maximum 16 inputs");
    }
    
    ORC_LOG_DEBUG("StackerStage: Processing {} input source(s)", inputs.size());
    
    // Update parameters
    if (!parameters.empty()) {
        set_parameters(parameters);
        ORC_LOG_DEBUG("StackerStage: Parameters updated - mode={}, smart_threshold={}, no_diff_dod={}, passthrough={}, reverse={}",
                     m_mode, m_smart_threshold, m_no_diff_dod, m_passthrough, m_reverse);
    }
    
    // Convert inputs to VideoFieldRepresentation
    std::vector<std::shared_ptr<const VideoFieldRepresentation>> sources;
    for (const auto& input : inputs) {
        auto field_rep = std::dynamic_pointer_cast<const VideoFieldRepresentation>(input);
        if (!field_rep) {
            throw DAGExecutionError("StackerStage input is not a VideoFieldRepresentation");
        }
        sources.push_back(field_rep);
    }
    
    // Process the fields
    auto result = process(sources);
    
    cached_output_ = result;
    // Return as artifact
    return {std::const_pointer_cast<VideoFieldRepresentation>(
        std::const_pointer_cast<const VideoFieldRepresentation>(result))};
}

std::shared_ptr<const VideoFieldRepresentation> 
StackerStage::process(
    const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources) const
{
    if (sources.empty()) {
        ORC_LOG_DEBUG("StackerStage::process - No sources provided");
        return nullptr;
    }
    
    // Passthrough mode: single input
    if (sources.size() == 1) {
        ORC_LOG_DEBUG("StackerStage::process - Passthrough mode (single source)");
        return sources[0];
    }
    
    // For now, return a simple implementation that returns the first source
    // TODO: Implement full stacking logic based on legacy ld-disc-stacker
    
    // Verify all sources have the same field range
    auto reference_range = sources[0]->field_range();
    ORC_LOG_DEBUG("StackerStage::process - Reference field range: {} to {}", 
                 reference_range.start.value(), reference_range.end.value());
    
    for (size_t i = 1; i < sources.size(); ++i) {
        auto range = sources[i]->field_range();
        if (range.start != reference_range.start || range.end != reference_range.end) {
            ORC_LOG_ERROR("StackerStage: Field range mismatch - Source 0: [{}, {}], Source {}: [{}, {}]",
                         reference_range.start.value(), reference_range.end.value(), i, range.start.value(), range.end.value());
            throw DAGExecutionError("StackerStage: All sources must have the same field range");
        }
    }
    
    // TODO: Implement actual stacking algorithm
    // This is a placeholder that returns the first source
    // Full implementation would:
    // 1. For each field in range
    // 2. Get field data from all sources
    // 3. Apply stacking mode pixel-by-pixel
    // 4. Handle dropouts appropriately
    // 5. Create new VideoFieldRepresentation with stacked data
    
    return sources[0];
}

void StackerStage::stack_field(
    FieldID field_id,
    const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources,
    std::vector<uint16_t>& output_samples,
    std::vector<DropoutRegion>& /*output_dropouts*/) const  // TODO: Implement dropout region tracking
{
    ORC_LOG_DEBUG("StackerStage::stack_field - Processing field_id={} with {} sources", 
                 field_id.value(), sources.size());
    
    // Get descriptor from first source
    auto descriptor = sources[0]->get_descriptor(field_id);
    if (!descriptor) {
        ORC_LOG_ERROR("StackerStage: Field descriptor not available for field_id={}", field_id.value());
        throw DAGExecutionError("StackerStage: Field descriptor not available");
    }
    
    size_t width = descriptor->width;
    size_t height = descriptor->height;
    
    ORC_LOG_DEBUG("StackerStage::stack_field - Field dimensions: {}x{}", width, height);
    
    // Get video parameters for black level
    auto video_params = sources[0]->get_video_parameters();
    if (!video_params) {
        ORC_LOG_ERROR("StackerStage: Video parameters not available for field_id={}", field_id.value());
        throw DAGExecutionError("StackerStage: Video parameters not available");
    }
    
    // Resize output
    output_samples.resize(width * height);
    
    size_t total_dropouts = 0;
    size_t total_diff_dod_recoveries = 0;
    size_t total_stacked_pixels = 0;
    
    // Process each pixel
    for (size_t y = 0; y < height; ++y) {
        size_t line_dropouts = 0;
        size_t line_recoveries = 0;
        size_t line_stacked = 0;
        
        for (size_t x = 0; x < width; ++x) {
            std::vector<uint16_t> values;
            std::vector<bool> is_dropout(sources.size());
            
            // Collect values from all sources
            for (size_t src_idx = 0; src_idx < sources.size(); ++src_idx) {
                const auto* line = sources[src_idx]->get_line(field_id, y);
                if (!line) {
                    continue;
                }
                
                uint16_t pixel_value = line[x];
                
                // Check dropout status
                auto dropouts = sources[src_idx]->get_dropout_hints(field_id);
                bool pixel_is_dropout = false;
                for (const auto& region : dropouts) {
                    if (y == region.line && x >= region.start_sample && x < region.end_sample) {
                        pixel_is_dropout = true;
                        break;
                    }
                }
                
                is_dropout[src_idx] = pixel_is_dropout;
                
                // Include non-dropout pixels or apply diff_dod if enabled
                if (!pixel_is_dropout) {
                    values.push_back(pixel_value);
                } else if (!m_no_diff_dod && pixel_value > 0) {
                    values.push_back(pixel_value);
                }
            }
            
            // Apply differential dropout detection if all values are dropouts
            bool all_dropouts = std::all_of(is_dropout.begin(), is_dropout.end(), 
                                           [](bool b) { return b; });
            if (all_dropouts && sources.size() >= 3 && !m_no_diff_dod && !values.empty()) {
                size_t before_count = values.size();
                values = diff_dod(values, *video_params);
                if (values.size() > 0 && values.size() < before_count) {
                    line_recoveries++;
                    total_diff_dod_recoveries++;
                }
            }
            
            // Calculate stacked value
            uint16_t stacked_value;
            if (values.empty()) {
                // No valid values - use black level
                stacked_value = video_params->black_16b_ire;
                line_dropouts++;
                total_dropouts++;
                // Mark as dropout in output
                // TODO: Add to output_dropouts
            } else {
                // For simple modes (no neighbor checking)
                std::vector<uint16_t> dummy;
                std::vector<bool> dropout_flags(5, false);
                dropout_flags[0] = all_dropouts;
                stacked_value = stack_mode(values, dummy, dummy, dummy, dummy, dropout_flags);
                line_stacked++;
                total_stacked_pixels++;
            }
            
            output_samples[y * width + x] = stacked_value;
        }
        
        // Trace logging per line
        if (line_dropouts > 0 || line_recoveries > 0) {
            ORC_LOG_TRACE("StackerStage: Line {}: stacked={}, dropouts={}, diff_dod_recoveries={}",
                         y, line_stacked, line_dropouts, line_recoveries);
        }
    }
    
    // Debug summary for field
    ORC_LOG_DEBUG("StackerStage::stack_field - Field {} complete: total_stacked={}, total_dropouts={}, diff_dod_recoveries={}",
                 field_id.value(), total_stacked_pixels, total_dropouts, total_diff_dod_recoveries);
}

uint16_t StackerStage::stack_mode(
    const std::vector<uint16_t>& values,
    const std::vector<uint16_t>& /*values_n*/,  // TODO: Implement neighbor modes (3, 4)
    const std::vector<uint16_t>& /*values_s*/,
    const std::vector<uint16_t>& /*values_e*/,
    const std::vector<uint16_t>& /*values_w*/,
    const std::vector<bool>& /*all_dropout*/) const
{
    if (values.empty()) {
        return 0;
    }
    
    const size_t num_elements = values.size();
    int32_t mode = m_mode;
    
    // Auto mode: select based on number of sources
    if (mode == -1) {
        if (num_elements >= 3) {
            mode = 2; // Smart mean for 3+ sources
        } else {
            mode = 0; // Mean for 2 sources
        }
        static bool auto_mode_logged = false;
        if (!auto_mode_logged) {
            ORC_LOG_DEBUG("StackerStage: Auto mode selected mode {} for {} elements", mode, num_elements);
            auto_mode_logged = true;
        }
    }
    
    switch (mode) {
        case 0: // Mean
            return static_cast<uint16_t>(mean(values));
            
        case 1: // Median
            return median(values);
            
        case 2: { // Smart Mean
            const int32_t med = median(values);
            int32_t sum = 0;
            size_t count = 0;
            
            // Sum values within threshold of median
            for (const auto& val : values) {
                if (val < static_cast<uint32_t>(med + m_smart_threshold) && 
                    val > static_cast<uint32_t>(med - m_smart_threshold)) {
                    sum += val;
                    count++;
                }
            }
            
            static size_t smart_mean_calls = 0;
            if ((smart_mean_calls++ % 10000) == 0) {
                ORC_LOG_TRACE("StackerStage: Smart Mean - median={}, selected {}/{} values within threshold {}",
                             med, count, num_elements, m_smart_threshold);
            }
            
            if (count == 0) {
                return med;
            }
            return static_cast<uint16_t>(sum / count);
        }
        
        case 3: // Smart Neighbor
        case 4: { // Neighbor
            // For now, fall back to median when neighbor data not available
            // Full implementation would use values_n, values_s, values_e, values_w
            return median(values);
        }
        
        default:
            return median(values);
    }
}

uint16_t StackerStage::median(std::vector<uint16_t> values) const
{
    if (values.empty()) {
        return 0;
    }
    
    const size_t n = values.size();
    
    if (n % 2 == 0) {
        // Even number of elements
        std::nth_element(values.begin(), values.begin() + n / 2, values.end());
        std::nth_element(values.begin(), values.begin() + (n - 1) / 2, values.end());
        return static_cast<uint16_t>((values[(n - 1) / 2] + values[n / 2]) / 2.0);
    } else {
        // Odd number of elements
        std::nth_element(values.begin(), values.begin() + n / 2, values.end());
        return values[n / 2];
    }
}

int32_t StackerStage::mean(const std::vector<uint16_t>& values) const
{
    if (values.empty()) {
        return 0;
    }
    
    uint32_t sum = 0;
    for (const auto& val : values) {
        sum += val;
    }
    
    return static_cast<int32_t>(sum / values.size());
}

uint16_t StackerStage::closest(const std::vector<uint16_t>& values, int32_t target) const
{
    if (values.empty()) {
        return 0;
    }
    
    uint16_t result = values[0];
    int32_t min_diff = std::abs(target - static_cast<int32_t>(values[0]));
    
    for (size_t i = 1; i < values.size(); ++i) {
        int32_t diff = std::abs(target - static_cast<int32_t>(values[i]));
        if (diff < min_diff) {
            min_diff = diff;
            result = values[i];
        }
    }
    
    return result;
}

std::vector<uint16_t> StackerStage::diff_dod(
    const std::vector<uint16_t>& input_values,
    const VideoParameters& /*video_params*/) const
{
    std::vector<uint16_t> result;
    
    if (input_values.size() < 3) {
        return result;
    }
    
    // Differential dropout detection logic
    // Check if values are similar enough to be considered valid
    const int32_t med = median(const_cast<std::vector<uint16_t>&>(
        const_cast<std::vector<uint16_t>&>(input_values)));
    
    const int32_t threshold = 500; // Threshold for diff_dod
    
    for (const auto& val : input_values) {
        if (std::abs(static_cast<int32_t>(val) - med) < threshold) {
            result.push_back(val);
        }
    }
    
    static size_t diff_dod_calls = 0;
    static size_t diff_dod_recoveries = 0;
    if (result.size() > 0) {
        diff_dod_recoveries++;
    }
    if ((++diff_dod_calls % 1000) == 0) {
        ORC_LOG_DEBUG("StackerStage: Differential DOD stats - calls={}, recoveries={} ({:.1f}%)",
                     diff_dod_calls, diff_dod_recoveries, 
                     (100.0 * diff_dod_recoveries) / diff_dod_calls);
    }
    
    return result;
}

std::vector<ParameterDescriptor> StackerStage::get_parameter_descriptors(VideoSystem project_format) const
{
    (void)project_format;  // Unused - stacker works with all formats
    std::vector<ParameterDescriptor> descriptors;
    
    // Stacking mode
    ParameterDescriptor mode_desc;
    mode_desc.name = "mode";
    mode_desc.display_name = "Stacking Mode";
    mode_desc.description = "Algorithm for combining multiple sources";
    mode_desc.type = ParameterType::STRING;
    mode_desc.constraints.allowed_strings = {
        "Auto",
        "Mean",
        "Median",
        "Smart Mean",
        "Smart Neighbor",
        "Neighbor"
    };
    mode_desc.constraints.default_value = std::string("Auto");
    mode_desc.constraints.required = false;
    descriptors.push_back(mode_desc);
    
    // Smart threshold
    ParameterDescriptor threshold_desc;
    threshold_desc.name = "smart_threshold";
    threshold_desc.display_name = "Smart Threshold";
    threshold_desc.description = "Range threshold for smart modes (0-128, default 15)\n"
                                "Lower values are more selective (fewer sources included in averaging)\n"
                                "Higher values are more inclusive (more sources included)\n"
                                "Only used when mode is 2 (Smart Mean) or 3 (Smart Neighbor)";
    threshold_desc.type = ParameterType::INT32;
    threshold_desc.constraints.min_value = static_cast<int32_t>(0);
    threshold_desc.constraints.max_value = static_cast<int32_t>(128);
    threshold_desc.constraints.default_value = static_cast<int32_t>(15);
    threshold_desc.constraints.required = false;
    descriptors.push_back(threshold_desc);
    
    // No differential dropout detection
    ParameterDescriptor no_diff_dod_desc;
    no_diff_dod_desc.name = "no_diff_dod";
    no_diff_dod_desc.display_name = "Disable Differential Dropout Detection";
    no_diff_dod_desc.description = "When disabled (false), allows recovery of pixels incorrectly marked as dropouts\n"
                                   "by comparing values across sources (requires 3+ sources)\n"
                                   "Enable (true) if you want to strictly trust dropout markings";
    no_diff_dod_desc.type = ParameterType::BOOL;
    no_diff_dod_desc.constraints.default_value = false;
    no_diff_dod_desc.constraints.required = false;
    descriptors.push_back(no_diff_dod_desc);
    
    // Passthrough
    ParameterDescriptor passthrough_desc;
    passthrough_desc.name = "passthrough";
    passthrough_desc.display_name = "Passthrough Universal Dropouts";
    passthrough_desc.description = "When enabled (true), preserves dropout regions that appear in ALL sources\n"
                                   "Useful when every capture has the same physical damage\n"
                                   "When disabled (false), attempts to stack even universal dropouts";
    passthrough_desc.type = ParameterType::BOOL;
    passthrough_desc.constraints.default_value = false;
    passthrough_desc.constraints.required = false;
    descriptors.push_back(passthrough_desc);
    
    // Reverse field order
    ParameterDescriptor reverse_desc;
    reverse_desc.name = "reverse";
    reverse_desc.display_name = "Reverse Field Order";
    reverse_desc.description = "Reverse the field order to second/first (default first/second)\n"
                               "Enable if source captures have different field ordering\n"
                               "Usually not needed unless sources were captured with different settings";
    reverse_desc.type = ParameterType::BOOL;
    reverse_desc.constraints.default_value = false;
    reverse_desc.constraints.required = false;
    descriptors.push_back(reverse_desc);
    
    return descriptors;
}

std::map<std::string, ParameterValue> StackerStage::get_parameters() const
{
    return parameters_;
}

bool StackerStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    // Store parameters
    parameters_ = params;
    
    bool any_changed = false;
    int32_t old_mode = m_mode;
    int32_t old_threshold = m_smart_threshold;
    
    for (const auto& [key, value] : params) {
        if (key == "mode") {
            // Accept both string and int32 for compatibility
            if (auto* val = std::get_if<std::string>(&value)) {
                if (*val == "Auto") {
                    m_mode = -1;
                } else if (*val == "Mean") {
                    m_mode = 0;
                } else if (*val == "Median") {
                    m_mode = 1;
                } else if (*val == "Smart Mean") {
                    m_mode = 2;
                } else if (*val == "Smart Neighbor") {
                    m_mode = 3;
                } else if (*val == "Neighbor") {
                    m_mode = 4;
                } else {
                    ORC_LOG_WARN("StackerStage: Invalid mode value '{}'", *val);
                    return false;
                }
                if (m_mode != old_mode) {
                    any_changed = true;
                }
            } else if (auto* val = std::get_if<int32_t>(&value)) {
                // Support legacy integer values
                if (*val < -1 || *val > 4) {
                    ORC_LOG_WARN("StackerStage: Invalid mode value {} (must be -1 to 4)", *val);
                    return false;
                }
                if (m_mode != *val) {
                    any_changed = true;
                }
                m_mode = *val;
            } else {
                return false;
            }
        } else if (key == "smart_threshold") {
            if (auto* val = std::get_if<int32_t>(&value)) {
                if (*val < 0 || *val > 128) {
                    return false;
                }
                m_smart_threshold = *val;
            } else {
                return false;
            }
        } else if (key == "no_diff_dod") {
            if (auto* val = std::get_if<bool>(&value)) {
                m_no_diff_dod = *val;
            } else {
                return false;
            }
        } else if (key == "passthrough") {
            if (auto* val = std::get_if<bool>(&value)) {
                m_passthrough = *val;
            } else {
                return false;
            }
        } else if (key == "reverse") {
            if (auto* val = std::get_if<bool>(&value)) {
                m_reverse = *val;
            } else {
                return false;
            }
        }
    }
    
    if (any_changed) {
        ORC_LOG_DEBUG("StackerStage: Parameters changed - mode: {} -> {}, threshold: {} -> {}",
                     old_mode, m_mode, old_threshold, m_smart_threshold);
    }
    
    return true;
}

std::optional<StageReport> StackerStage::generate_report() const {
    StageReport report;
    
    // Summary
    const char* mode_names[] = {"Auto", "Mean", "Median", "Smart Mean", "Smart Neighbor", "Neighbor"};
    int mode_index = m_mode + 1;  // -1 (Auto) becomes 0
    if (mode_index < 0 || mode_index >= 6) mode_index = 0;
    
    report.summary = "Stacker Configuration";
    
    // Configuration items
    report.items.push_back({"Stacking Mode", mode_names[mode_index]});
    report.items.push_back({"Smart Threshold", std::to_string(m_smart_threshold)});
    report.items.push_back({"Differential Dropout Detection", m_no_diff_dod ? "Disabled" : "Enabled"});
    report.items.push_back({"Dropout Passthrough", m_passthrough ? "Enabled" : "Disabled"});
    report.items.push_back({"Reverse Field Order", m_reverse ? "Yes" : "No"});
    
    // Metrics
    report.metrics["mode"] = static_cast<int64_t>(m_mode);
    report.metrics["smart_threshold"] = static_cast<int64_t>(m_smart_threshold);
    
    return report;
}
std::vector<PreviewOption> StackerStage::get_preview_options() const
{
    return PreviewHelpers::get_standard_preview_options(cached_output_);
}

PreviewImage StackerStage::render_preview(const std::string& option_id, uint64_t index,
                                            PreviewNavigationHint hint) const
{
    auto start_time = std::chrono::high_resolution_clock::now();
    auto result = PreviewHelpers::render_standard_preview(cached_output_, option_id, index, hint);
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    ORC_LOG_INFO("Stacker PREVIEW: option '{}' index {} rendered in {} ms (hint={})",
                 option_id, index, duration_ms, hint == PreviewNavigationHint::Sequential ? "Sequential" : "Random");
    return result;
}
} // namespace orc
