/*
 * File:        render_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     Rendering presenter implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "render_presenter.h"
#include "metrics_presenter.h"
#include <cstdio>
#include "../core/include/project.h"
#include "../core/include/project_to_dag.h"
#include "../core/include/preview_renderer.h"
#include "../core/include/dag_field_renderer.h"
#include "../core/include/dag_executor.h"
#include "../core/include/vbi_decoder.h"
#include "../core/include/observation_context.h"
#include "../core/include/observation_cache.h"
#include "../core/stages/ld_sink/ld_sink_stage.h"
#include "../core/stages/dropout_analysis_sink/dropout_analysis_sink_stage.h"
#include "../core/stages/snr_analysis_sink/snr_analysis_sink_stage.h"
#include "../core/stages/burst_level_analysis_sink/burst_level_analysis_sink_stage.h"
#include "vbi_presenter.h"
#include "../core/include/logging.h"
#include <stdexcept>
#include <fstream>

namespace orc::presenters {

class RenderPresenter::Impl {
public:
    explicit Impl(void* project_handle)
        : project_(static_cast<orc::Project*>(project_handle))
        , trigger_cancel_requested_(false)
        , trigger_active_(false)
        , next_request_id_(1)
    {
        if (!project_) {
            throw std::invalid_argument("Project cannot be null");
        }
    }
    
    // Helper to get concrete DAG from opaque handle
    std::shared_ptr<const orc::DAG> getConcreteDAG() const {
        return std::static_pointer_cast<const orc::DAG>(dag_void_);
    }
    
    orc::Project* project_;
    std::shared_ptr<void> dag_void_;  // Opaque DAG handle
    std::unique_ptr<orc::PreviewRenderer> preview_renderer_;
    std::unique_ptr<orc::DAGFieldRenderer> field_renderer_;
    std::unique_ptr<orc::VBIDecoder> vbi_decoder_;
    std::shared_ptr<orc::ObservationCache> obs_cache_;
    bool trigger_cancel_requested_;
    bool trigger_active_;
    uint64_t next_request_id_;
    orc::TriggerableStage* current_trigger_stage_ = nullptr;
    
    void rebuildRenderersFromDAG() {
        auto dag = getConcreteDAG();
        if (!dag) {
            // Clear all renderers for null DAG
            preview_renderer_.reset();
            field_renderer_.reset();
            vbi_decoder_.reset();
            obs_cache_.reset();
            return;
        }
        
        // Save dropout state if exists
        bool show_dropouts = false;
        if (preview_renderer_) {
            show_dropouts = preview_renderer_->get_show_dropouts();
        }
        
        // Rebuild renderers
        obs_cache_ = std::make_shared<orc::ObservationCache>(dag);
        preview_renderer_ = std::make_unique<orc::PreviewRenderer>(dag);
        field_renderer_ = std::make_unique<orc::DAGFieldRenderer>(dag);
        vbi_decoder_ = std::make_unique<orc::VBIDecoder>();
        
        // Restore dropout state
        if (preview_renderer_) {
            preview_renderer_->set_show_dropouts(show_dropouts);
        }
    }
};

RenderPresenter::RenderPresenter(void* project_handle)
    : impl_(std::make_unique<Impl>(project_handle))
{
}

RenderPresenter::~RenderPresenter() = default;

RenderPresenter::RenderPresenter(RenderPresenter&&) noexcept = default;
RenderPresenter& RenderPresenter::operator=(RenderPresenter&&) noexcept = default;

bool RenderPresenter::updateDAG()
{
    try {
        impl_->getConcreteDAG() = orc::project_to_dag(*impl_->project_);
        impl_->rebuildRenderersFromDAG();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void RenderPresenter::setDAG(std::shared_ptr<void> dag_handle)
{
    impl_->dag_void_ = std::move(dag_handle);
    impl_->rebuildRenderersFromDAG();
}

orc::PreviewRenderResult RenderPresenter::renderPreview(
    NodeID node_id,
    orc::PreviewOutputType output_type,
    uint64_t output_index,
    const std::string& option_id)
{
    if (!impl_->preview_renderer_) {
        return orc::PreviewRenderResult{
            {}, false, "Preview renderer not initialized", node_id, output_type, output_index
        };
    }
    
    try {
        // Call core preview renderer
        auto core_result = impl_->preview_renderer_->render_output(
            node_id, output_type, output_index, option_id
        );
        
        // Populate observation cache for the rendered field(s)
        if (impl_->obs_cache_) {
            if (output_type == orc::PreviewOutputType::Field || 
                output_type == orc::PreviewOutputType::Luma) {
                impl_->obs_cache_->get_field(node_id, orc::FieldID(output_index));
            } else if (output_type == orc::PreviewOutputType::Frame ||
                       output_type == orc::PreviewOutputType::Frame_Reversed ||
                       output_type == orc::PreviewOutputType::Split) {
                uint64_t first_field = output_index * 2;
                impl_->obs_cache_->get_field(node_id, orc::FieldID(first_field));
                impl_->obs_cache_->get_field(node_id, orc::FieldID(first_field + 1));
            }
        }
        
        // Convert core result to public API result
        orc::PreviewRenderResult result;
        result.image.width = core_result.image.width;
        result.image.height = core_result.image.height;
        result.image.rgb_data = std::move(core_result.image.rgb_data);
        result.image.dropout_regions = std::move(core_result.image.dropout_regions);
        // Propagate vectorscope data from core image to public API result
        // This enables the GUI vectorscope to render UV samples and graticules
        result.vectorscope_data = std::move(core_result.image.vectorscope_data);
        result.success = core_result.success;
        result.error_message = std::move(core_result.error_message);
        result.node_id = core_result.node_id;
        result.output_type = core_result.output_type;
        result.output_index = core_result.output_index;
        
        return result;
        
    } catch (const std::exception& e) {
        return orc::PreviewRenderResult{
            {}, false, e.what(), node_id, output_type, output_index
        };
    }
}

std::vector<orc::PreviewOutputInfo> RenderPresenter::getAvailableOutputs(NodeID node_id)
{
    if (!impl_->preview_renderer_) {
        return {};
    }
    
    auto core_outputs = impl_->preview_renderer_->get_available_outputs(node_id);
    
    // Convert to public API types
    std::vector<orc::PreviewOutputInfo> result;
    result.reserve(core_outputs.size());
    
    for (const auto& core_out : core_outputs) {
        orc::PreviewOutputInfo info;
        info.type = core_out.type;
        info.display_name = core_out.display_name;
        info.count = core_out.count;
        info.is_available = core_out.is_available;
        info.dar_aspect_correction = core_out.dar_aspect_correction;
        info.option_id = core_out.option_id;
        info.dropouts_available = core_out.dropouts_available;
        info.has_separate_channels = core_out.has_separate_channels;
        info.first_field_offset = core_out.first_field_offset;
        result.push_back(std::move(info));
    }
    
    return result;
}

uint64_t RenderPresenter::getOutputCount(NodeID node_id, orc::PreviewOutputType output_type)
{
    if (!impl_->preview_renderer_) {
        return 0;
    }
    
    return impl_->preview_renderer_->get_output_count(node_id, output_type);
}

bool RenderPresenter::savePNG(
    NodeID node_id,
    orc::PreviewOutputType output_type,
    uint64_t output_index,
    const std::string& filename,
    const std::string& option_id)
{
    if (!impl_->preview_renderer_) {
        return false;
    }
    
    try {
        return impl_->preview_renderer_->save_png(
            node_id, output_type, output_index, filename, option_id
        );
    } catch (const std::exception&) {
        return false;
    }
}

std::optional<VBIFieldInfoView> RenderPresenter::getVBIData(NodeID node_id, FieldID field_id)
{
    if (!impl_->obs_cache_) {
        return std::nullopt;
    }
    
    try {
        // Render the field to populate observations
        auto field_opt = impl_->obs_cache_->get_field(node_id, field_id);
        if (!field_opt) {
            return std::nullopt;
        }
        
        // Get observations and decode VBI
        const auto& obs_context = impl_->obs_cache_->get_observation_context();
        auto vbi_info_opt = VbiPresenter::decodeVbiFromObservation(&obs_context, field_id);
        
        if (!vbi_info_opt.has_value()) {
            return std::nullopt;
        }
        
        // Return the fully decoded VBI information
        return vbi_info_opt.value();
        
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool RenderPresenter::requestDropoutData(
    NodeID node_id,
    uint64_t request_id,
    std::function<void(uint64_t, bool, const std::string&)> callback)
{
    // This is a synchronous operation - find the node and return data immediately
    if (!impl_->getConcreteDAG()) {
        if (callback) callback(request_id, false, "DAG not initialized");
        return false;
    }
    
    const orc::DAGNode* target_node = nullptr;
    for (const auto& node : impl_->getConcreteDAG()->nodes()) {
        if (node.node_id == node_id) {
            target_node = &node;
            break;
        }
    }
    
    if (!target_node) {
        if (callback) callback(request_id, false, "Node not found");
        return false;
    }
    
    auto* sink = dynamic_cast<orc::DropoutAnalysisSinkStage*>(target_node->stage.get());
    if (!sink) {
        if (callback) callback(request_id, false, "Node is not a DropoutAnalysisSinkStage");
        return false;
    }
    
    if (!sink->has_results()) {
        if (callback) callback(request_id, false, "No data available - trigger the sink first");
        return false;
    }
    
    // Data is available - signal success
    if (callback) callback(request_id, true, "");
    return true;
}

bool RenderPresenter::requestSNRData(
    NodeID node_id,
    uint64_t request_id,
    std::function<void(uint64_t, bool, const std::string&)> callback)
{
    if (!impl_->getConcreteDAG()) {
        if (callback) callback(request_id, false, "DAG not initialized");
        return false;
    }
    
    const orc::DAGNode* target_node = nullptr;
    for (const auto& node : impl_->getConcreteDAG()->nodes()) {
        if (node.node_id == node_id) {
            target_node = &node;
            break;
        }
    }
    
    if (!target_node) {
        if (callback) callback(request_id, false, "Node not found");
        return false;
    }
    
    auto* sink = dynamic_cast<orc::SNRAnalysisSinkStage*>(target_node->stage.get());
    if (!sink) {
        if (callback) callback(request_id, false, "Node is not a SNRAnalysisSinkStage");
        return false;
    }
    
    if (!sink->has_results()) {
        if (callback) callback(request_id, false, "No data available - trigger the sink first");
        return false;
    }
    
    if (callback) callback(request_id, true, "");
    return true;
}

bool RenderPresenter::requestBurstLevelData(
    NodeID node_id,
    uint64_t request_id,
    std::function<void(uint64_t, bool, const std::string&)> callback)
{
    if (!impl_->getConcreteDAG()) {
        if (callback) callback(request_id, false, "DAG not initialized");
        return false;
    }
    
    const orc::DAGNode* target_node = nullptr;
    for (const auto& node : impl_->getConcreteDAG()->nodes()) {
        if (node.node_id == node_id) {
            target_node = &node;
            break;
        }
    }
    
    if (!target_node) {
        if (callback) callback(request_id, false, "Node not found");
        return false;
    }
    
    auto* sink = dynamic_cast<orc::BurstLevelAnalysisSinkStage*>(target_node->stage.get());
    if (!sink) {
        if (callback) callback(request_id, false, "Node is not a BurstLevelAnalysisSinkStage");
        return false;
    }
    
    if (!sink->has_results()) {
        if (callback) callback(request_id, false, "No data available - trigger the sink first");
        return false;
    }
    
    if (callback) callback(request_id, true, "");
    return true;
}

uint64_t RenderPresenter::triggerStage(NodeID node_id, ProgressCallback callback)
{
    if (!impl_->getConcreteDAG()) {
        throw std::runtime_error("DAG not initialized");
    }
    
    impl_->trigger_cancel_requested_ = false;
    impl_->trigger_active_ = true;
    uint64_t request_id = impl_->next_request_id_++;
    
    try {
        // Find the target node in the DAG
        const orc::DAGNode* target_node = nullptr;
        for (const auto& node : impl_->getConcreteDAG()->nodes()) {
            if (node.node_id == node_id) {
                target_node = &node;
                break;
            }
        }
        
        if (!target_node) {
            impl_->trigger_active_ = false;
            throw std::runtime_error("Node '" + node_id.to_string() + "' not found in DAG");
        }
        
        auto trigger_stage = dynamic_cast<orc::TriggerableStage*>(target_node->stage.get());
        if (!trigger_stage) {
            impl_->trigger_active_ = false;
            throw std::runtime_error("Stage '" + node_id.to_string() + "' is not triggerable");
        }
        
        // Build executor to get inputs for this node
        auto executor = std::make_shared<orc::DAGExecutor>();
        
        // Execute DAG up to (but not including) the target node to get its inputs
        std::vector<orc::ArtifactPtr> inputs;
        
        if (!target_node->input_node_ids.empty()) {
            // Execute predecessor nodes to get inputs
            auto node_outputs = executor->execute_to_node(*impl_->getConcreteDAG(), target_node->input_node_ids[0]);
            
            // Collect inputs from predecessor nodes
            for (size_t i = 0; i < target_node->input_node_ids.size(); ++i) {
                const auto& input_node_id = target_node->input_node_ids[i];
                size_t input_index = (i < target_node->input_indices.size()) ? target_node->input_indices[i] : 0;
                
                auto it = node_outputs.find(input_node_id);
                if (it != node_outputs.end() && input_index < it->second.size()) {
                    inputs.push_back(it->second[input_index]);
                }
            }
        }
        
        // Store pointer to current trigger stage for cancellation
        impl_->current_trigger_stage_ = trigger_stage;
        
        // Set up progress callback
        trigger_stage->set_progress_callback([this, trigger_stage, callback](
            size_t current, size_t total, const std::string& message) {
            
            // Check for cancellation
            if (impl_->trigger_cancel_requested_) {
                trigger_stage->cancel_trigger();
            }
            
            // Call user callback
            if (callback) {
                callback(static_cast<int>(current), static_cast<int>(total), message);
            }
        });
        
        // Execute trigger using the observation context populated during execute_to_node
        orc::ObservationContext& obs_context = executor->get_observation_context();
        bool success = trigger_stage->trigger(inputs, target_node->parameters, obs_context);
        
        // Clear current trigger stage pointer
        impl_->current_trigger_stage_ = nullptr;
        impl_->trigger_active_ = false;
        
        if (!success) {
            std::string status = trigger_stage->get_trigger_status();
            throw std::runtime_error("Trigger failed: " + status);
        }
        
    } catch (...) {
        impl_->current_trigger_stage_ = nullptr;
        impl_->trigger_active_ = false;
        throw;
    }
    
    return request_id;
}

void RenderPresenter::cancelTrigger()
{
    impl_->trigger_cancel_requested_ = true;
    if (impl_->current_trigger_stage_) {
        impl_->current_trigger_stage_->cancel_trigger();
    }
}

bool RenderPresenter::isTriggerActive() const
{
    return impl_->trigger_active_;
}

void RenderPresenter::setShowDropouts(bool show)
{
    if (impl_->preview_renderer_) {
        impl_->preview_renderer_->set_show_dropouts(show);
    }
}

bool RenderPresenter::getShowDropouts() const
{
    if (impl_->preview_renderer_) {
        return impl_->preview_renderer_->get_show_dropouts();
    }
    return false;
}

RenderPresenter::ImageToFieldMapping RenderPresenter::mapImageToField(
    NodeID node_id,
    orc::PreviewOutputType output_type,
    uint64_t output_index,
    int image_y,
    int image_height)
{
    if (!impl_->preview_renderer_) {
        return {false, 0, 0};
    }
    
    auto result = impl_->preview_renderer_->map_image_to_field(
        node_id, output_type, output_index, image_y, image_height
    );
    
    return {result.is_valid, result.field_index, result.field_line};
}

RenderPresenter::FieldToImageMapping RenderPresenter::mapFieldToImage(
    NodeID node_id,
    orc::PreviewOutputType output_type,
    uint64_t output_index,
    uint64_t field_index,
    int field_line,
    int image_height)
{
    if (!impl_->preview_renderer_) {
        return {false, 0};
    }
    
    auto result = impl_->preview_renderer_->map_field_to_image(
        node_id, output_type, output_index, field_index, field_line, image_height
    );
    
    return {result.is_valid, result.image_y};
}

RenderPresenter::FrameFields RenderPresenter::getFrameFields(NodeID node_id, uint64_t frame_index)
{
    if (!impl_->preview_renderer_) {
        return {false, 0, 0};
    }
    
    auto result = impl_->preview_renderer_->get_frame_fields(node_id, frame_index);
    return {result.is_valid, result.first_field, result.second_field};
}

RenderPresenter::FrameLineNavigation RenderPresenter::navigateFrameLine(
    NodeID node_id,
    orc::PreviewOutputType output_type,
    uint64_t current_field,
    int current_line,
    int direction,
    int field_height)
{
    if (!impl_->preview_renderer_) {
        return {false, 0, 0};
    }
    
    auto result = impl_->preview_renderer_->navigate_frame_line(
        node_id, output_type, current_field, current_line, direction, field_height
    );
    
    return {result.is_valid, result.new_field_index, result.new_line_number};
}

std::vector<uint16_t> RenderPresenter::getLineSamples(
    NodeID node_id,
    orc::PreviewOutputType output_type,
    uint64_t output_index,
    int line_number,
    int sample_x,
    int preview_width)
{
    if (!impl_->preview_renderer_) {
        return {};
    }
    
    try {
        // Get the representation at this node
        auto repr = impl_->preview_renderer_->get_representation_at_node(node_id);
        if (!repr) {
            return {};
        }
        
        // Determine which field to get samples from
        orc::FieldID field_id;
        if (output_type == orc::PreviewOutputType::Field) {
            field_id = orc::FieldID(output_index);
        } else {
            // For frames, use first field (GUI should call mapImageToField first)
            return {};
        }
        
        // Get descriptor to know field dimensions
        auto descriptor = repr->get_descriptor(field_id);
        if (!descriptor) {
            return {};
        }
        
        // Validate line number is within bounds
        if (line_number < 0 || static_cast<size_t>(line_number) >= descriptor->height) {
            return {};
        }
        
        // Get line data
        const uint16_t* line_data = repr->get_line(field_id, line_number);
        if (!line_data) {
            return {};
        }
        
        // Return as uint16_t vector (no conversion needed)
        std::vector<uint16_t> result(line_data, line_data + descriptor->width);
        
        return result;
        
    } catch (const std::exception&) {
        return {};
    }
}

RenderPresenter::LineSampleData RenderPresenter::getLineSamplesWithYC(
    NodeID node_id,
    orc::PreviewOutputType output_type,
    uint64_t output_index,
    int line_number,
    int sample_x,
    int preview_width)
{
    LineSampleData result;
    result.has_separate_channels = false;
    
    if (!impl_->preview_renderer_) {
        return result;
    }
    
    try {
        // Get the representation at this node
        auto repr = impl_->preview_renderer_->get_representation_at_node(node_id);
        if (!repr) {
            return result;
        }
        
        // Determine which field to get samples from
        orc::FieldID field_id;
        if (output_type == orc::PreviewOutputType::Field) {
            field_id = orc::FieldID(output_index);
        } else {
            // For frames, use first field (GUI should call mapImageToField first)
            return result;
        }
        
        // Get descriptor to know field dimensions
        auto descriptor = repr->get_descriptor(field_id);
        if (!descriptor) {
            return result;
        }
        
        // Validate line number is within bounds
        if (line_number < 0 || static_cast<size_t>(line_number) >= descriptor->height) {
            return result;
        }
        
        // Check if this is a Y/C source with separate channels
        result.has_separate_channels = repr->has_separate_channels();
        
        if (result.has_separate_channels) {
            // Y/C source - get Y and C separately (no composite available)
            const uint16_t* y_data = repr->get_line_luma(field_id, line_number);
            if (y_data) {
                result.y_samples.assign(y_data, y_data + descriptor->width);
            }
            
            const uint16_t* c_data = repr->get_line_chroma(field_id, line_number);
            if (c_data) {
                result.c_samples.assign(c_data, c_data + descriptor->width);
            }
            
            // For Y/C sources, use Y as composite for compatibility
            result.composite_samples = result.y_samples;
        } else {
            // Composite source - get composite line data
            const uint16_t* line_data = repr->get_line(field_id, line_number);
            if (!line_data) {
                return result;
            }
            
            result.composite_samples.assign(line_data, line_data + descriptor->width);
        }
        
        return result;
        
    } catch (const std::exception&) {
        return result;
    }
}

RenderPresenter::LineSampleData RenderPresenter::getFieldSamplesForTiming(
    NodeID node_id,
    orc::PreviewOutputType output_type,
    uint64_t output_index)
{
    LineSampleData result;
    result.has_separate_channels = false;
    
    if (!impl_->preview_renderer_) {
        return result;
    }
    
    try {
        // Get the representation at this node
        auto repr = impl_->preview_renderer_->get_representation_at_node(node_id);
        if (!repr) {
            return result;
        }
        
        // Check if this is a Y/C source with separate channels
        result.has_separate_channels = repr->has_separate_channels();
        
        // Determine which field(s) to get samples from
        if (output_type == orc::PreviewOutputType::Field || 
            output_type == orc::PreviewOutputType::Luma) {
            // Single field
            orc::FieldID field_id(output_index);
            
            if (result.has_separate_channels) {
                // Y/C source - get entire field for Y and C
                result.y_samples = repr->get_field_luma(field_id);
                result.c_samples = repr->get_field_chroma(field_id);
                result.composite_samples = result.y_samples;  // Use Y as composite
            } else {
                // Composite source
                result.composite_samples = repr->get_field(field_id);
            }
        } else if (output_type == orc::PreviewOutputType::Frame ||
                   output_type == orc::PreviewOutputType::Frame_Reversed ||
                   output_type == orc::PreviewOutputType::Split) {
            // Two fields - convert frame index to field indices
            // Frame N consists of fields (N*2) and (N*2 + 1)
            uint64_t first_field = output_index * 2;
            
            orc::FieldID field1_id(first_field);
            orc::FieldID field2_id(first_field + 1);
            
            // Handle reversed field order
            if (output_type == orc::PreviewOutputType::Frame_Reversed) {
                std::swap(field1_id, field2_id);
            }
            
            if (result.has_separate_channels) {
                // Y/C source - get both fields for Y and C
                auto y1 = repr->get_field_luma(field1_id);
                auto c1 = repr->get_field_chroma(field1_id);
                auto y2 = repr->get_field_luma(field2_id);
                auto c2 = repr->get_field_chroma(field2_id);
                
                // Concatenate field 1 and field 2
                result.y_samples.reserve(y1.size() + y2.size());
                result.y_samples.insert(result.y_samples.end(), y1.begin(), y1.end());
                result.y_samples.insert(result.y_samples.end(), y2.begin(), y2.end());
                
                result.c_samples.reserve(c1.size() + c2.size());
                result.c_samples.insert(result.c_samples.end(), c1.begin(), c1.end());
                result.c_samples.insert(result.c_samples.end(), c2.begin(), c2.end());
                
                result.composite_samples = result.y_samples;  // Use Y as composite
            } else {
                // Composite source - get both fields and concatenate
                auto f1 = repr->get_field(field1_id);
                auto f2 = repr->get_field(field2_id);
                
                result.composite_samples.reserve(f1.size() + f2.size());
                result.composite_samples.insert(result.composite_samples.end(), f1.begin(), f1.end());
                result.composite_samples.insert(result.composite_samples.end(), f2.begin(), f2.end());
            }
        }
        
        return result;
        
    } catch (const std::exception&) {
        return result;
    }
}

std::optional<orc::VideoParameters> RenderPresenter::getVideoParameters(NodeID node_id)
{
    if (!impl_->preview_renderer_) {
        return std::nullopt;
    }
    
    try {
        auto repr = impl_->preview_renderer_->get_representation_at_node(node_id);
        if (!repr) {
            return std::nullopt;
        }
        
        return repr->get_video_parameters();
        
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

ObservationData RenderPresenter::getObservations(NodeID node_id, FieldID field_id)
{
    ObservationData result{false, ""};
    
    if (!impl_->obs_cache_) {
        return result;
    }
    
    try {
        auto field_opt = impl_->obs_cache_->get_field(node_id, field_id);
        if (!field_opt) {
            return result;
        }
        
        // TODO: Serialize observations to JSON
        result.is_valid = true;
        result.json_data = "{}";  // Placeholder
        
        return result;
    } catch (const std::exception&) {
        return result;
    }
}

void RenderPresenter::clearCache()
{
    if (impl_->obs_cache_) {
        impl_->obs_cache_.reset();
        impl_->obs_cache_ = std::make_shared<orc::ObservationCache>(impl_->getConcreteDAG());
    }
}

std::string RenderPresenter::getCacheStats() const
{
    // TODO: Implement cache stats
    return "Cache: active";
}

// === Analysis Data Access (Phase 2.4) ===

bool RenderPresenter::getDropoutAnalysisData(
    NodeID node_id,
    std::vector<void*>& frame_stats,
    int32_t& total_frames)
{
    if (!impl_->getConcreteDAG()) {
        return false;
    }
    
    // Find the node in the DAG
    const orc::DAGNode* target_node = nullptr;
    for (const auto& node : impl_->getConcreteDAG()->nodes()) {
        if (node.node_id == node_id) {
            target_node = &node;
            break;
        }
    }
    
    if (!target_node) {
        return false;
    }
    
    // Cast to DropoutAnalysisSinkStage
    auto* sink = dynamic_cast<orc::DropoutAnalysisSinkStage*>(target_node->stage.get());
    if (!sink || !sink->has_results()) {
        return false;
    }
    
    // Get the data (this is a hack - we're storing pointers as void* to avoid exposing the type)
    // The caller (render_coordinator) knows the actual type
    auto& stats = sink->frame_stats();
    total_frames = sink->total_frames();
    
    // Store address of the vector - caller will cast back
    frame_stats.clear();
    frame_stats.push_back(const_cast<void*>(static_cast<const void*>(&stats)));
    
    return true;
}

bool RenderPresenter::getSNRAnalysisData(
    NodeID node_id,
    std::vector<void*>& frame_stats,
    int32_t& total_frames)
{
    if (!impl_->getConcreteDAG()) {
        return false;
    }
    
    // Find the node in the DAG
    const orc::DAGNode* target_node = nullptr;
    for (const auto& node : impl_->getConcreteDAG()->nodes()) {
        if (node.node_id == node_id) {
            target_node = &node;
            break;
        }
    }
    
    if (!target_node) {
        return false;
    }
    
    // Cast to SNRAnalysisSinkStage
    auto* sink = dynamic_cast<orc::SNRAnalysisSinkStage*>(target_node->stage.get());
    if (!sink || !sink->has_results()) {
        return false;
    }
    
    auto& stats = sink->frame_stats();
    total_frames = sink->total_frames();
    
    frame_stats.clear();
    frame_stats.push_back(const_cast<void*>(static_cast<const void*>(&stats)));
    
    return true;
}

bool RenderPresenter::getBurstLevelAnalysisData(
    NodeID node_id,
    std::vector<void*>& frame_stats,
    int32_t& total_frames)
{
    if (!impl_->getConcreteDAG()) {
        return false;
    }
    
    // Find the node in the DAG
    const orc::DAGNode* target_node = nullptr;
    for (const auto& node : impl_->getConcreteDAG()->nodes()) {
        if (node.node_id == node_id) {
            target_node = &node;
            break;
        }
    }
    
    if (!target_node) {
        return false;
    }
    
    // Cast to BurstLevelAnalysisSinkStage
    auto* sink = dynamic_cast<orc::BurstLevelAnalysisSinkStage*>(target_node->stage.get());
    if (!sink || !sink->has_results()) {
        return false;
    }
    
    auto& stats = sink->frame_stats();
    total_frames = sink->total_frames();
    
    frame_stats.clear();
    frame_stats.push_back(const_cast<void*>(static_cast<const void*>(&stats)));
    
    return true;
}

QualityMetrics RenderPresenter::getFieldQualityMetrics(NodeID node_id, FieldID field_id)
{
    if (!impl_->field_renderer_) {
        return QualityMetrics{};  // Empty metrics
    }
    
    // Render the field to populate observation context
    auto render_result = impl_->field_renderer_->render_field_at_node(node_id, field_id);
    
    if (!render_result.is_valid) {
        return QualityMetrics{};
    }
    
    // Extract metrics from observation context
    const auto& obs_context = impl_->field_renderer_->get_observation_context();
    return MetricsPresenter::extractFieldMetrics(field_id, const_cast<void*>(static_cast<const void*>(&obs_context)));
}

QualityMetrics RenderPresenter::getFrameQualityMetrics(NodeID node_id, FieldID field1_id, FieldID field2_id)
{
    if (!impl_->field_renderer_) {
        return QualityMetrics{};
    }
    
    // Render both fields to populate observation context
    auto render_result1 = impl_->field_renderer_->render_field_at_node(node_id, field1_id);
    auto render_result2 = impl_->field_renderer_->render_field_at_node(node_id, field2_id);
    
    if (!render_result1.is_valid || !render_result2.is_valid) {
        return QualityMetrics{};
    }
    
    // Extract and average metrics from observation context
    const auto& obs_context = impl_->field_renderer_->get_observation_context();
    return MetricsPresenter::extractFrameMetrics(field1_id, field2_id, const_cast<void*>(static_cast<const void*>(&obs_context)));
}

std::shared_ptr<const void> RenderPresenter::executeToNode(NodeID node_id)
{
    if (!impl_->getConcreteDAG()) {
        return nullptr;
    }
    
    try {
        orc::DAGExecutor executor;
        auto node_outputs = executor.execute_to_node(*impl_->getConcreteDAG(), node_id);
        
        auto it = node_outputs.find(node_id);
        if (it != node_outputs.end() && !it->second.empty()) {
            // Return the first output (typically VideoFieldRepresentation)
            return std::static_pointer_cast<const void>(it->second[0]);
        }
    } catch (const std::exception&) {
        return nullptr;
    }
    
    return nullptr;
}

const void* RenderPresenter::getObservationContext(NodeID node_id, FieldID field_id)
{
    if (!impl_->field_renderer_) {
        return nullptr;
    }
    
    // Render the field to populate observation context
    impl_->field_renderer_->render_field_at_node(node_id, field_id);
    
    // Return pointer to observation context
    return &impl_->field_renderer_->get_observation_context();
}

} // namespace orc::presenters
