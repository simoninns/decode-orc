/*
 * File:        render_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     Rendering presenter implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "render_presenter.h"
#include "../core/include/project.h"
#include "../core/include/project_to_dag.h"
#include "../core/include/preview_renderer.h"
#include "../core/include/dag_field_renderer.h"
#include "../core/include/vbi_decoder.h"
#include "../core/include/observation_context.h"
#include "../core/include/observation_cache.h"
#include "../core/stages/ld_sink/ld_sink_stage.h"
#include "../core/stages/dropout_analysis_sink/dropout_analysis_sink_stage.h"
#include "../core/stages/snr_analysis_sink/snr_analysis_sink_stage.h"
#include "../core/stages/burst_level_analysis_sink/burst_level_analysis_sink_stage.h"
#include "vbi_presenter.h"
#include <stdexcept>
#include <fstream>

namespace orc::presenters {

class RenderPresenter::Impl {
public:
    explicit Impl(orc::Project* project)
        : project_(project)
        , trigger_cancel_requested_(false)
    {
        if (!project_) {
            throw std::invalid_argument("Project cannot be null");
        }
    }
    
    orc::Project* project_;
    std::shared_ptr<const orc::DAG> dag_;
    std::unique_ptr<orc::PreviewRenderer> preview_renderer_;
    std::unique_ptr<orc::DAGFieldRenderer> field_renderer_;
    std::unique_ptr<orc::VBIDecoder> vbi_decoder_;
    std::shared_ptr<orc::ObservationCache> obs_cache_;
    bool trigger_cancel_requested_;
    
    void rebuildRenderersFromDAG() {
        if (!dag_) {
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
        obs_cache_ = std::make_shared<orc::ObservationCache>(dag_);
        preview_renderer_ = std::make_unique<orc::PreviewRenderer>(dag_);
        field_renderer_ = std::make_unique<orc::DAGFieldRenderer>(dag_);
        vbi_decoder_ = std::make_unique<orc::VBIDecoder>();
        
        // Restore dropout state
        if (preview_renderer_) {
            preview_renderer_->set_show_dropouts(show_dropouts);
        }
    }
};

RenderPresenter::RenderPresenter(orc::Project* project)
    : impl_(std::make_unique<Impl>(project))
{
}

RenderPresenter::~RenderPresenter() = default;

RenderPresenter::RenderPresenter(RenderPresenter&&) noexcept = default;
RenderPresenter& RenderPresenter::operator=(RenderPresenter&&) noexcept = default;

bool RenderPresenter::updateDAG()
{
    try {
        impl_->dag_ = orc::project_to_dag(*impl_->project_);
        impl_->rebuildRenderersFromDAG();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void RenderPresenter::setDAG(std::shared_ptr<const orc::DAG> dag)
{
    impl_->dag_ = std::move(dag);
    impl_->rebuildRenderersFromDAG();
}

orc::public_api::PreviewRenderResult RenderPresenter::renderPreview(
    NodeID node_id,
    orc::PreviewOutputType output_type,
    uint64_t output_index,
    const std::string& option_id)
{
    if (!impl_->preview_renderer_) {
        return orc::public_api::PreviewRenderResult{
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
        orc::public_api::PreviewRenderResult result;
        result.image.width = core_result.image.width;
        result.image.height = core_result.image.height;
        result.image.rgb_data = std::move(core_result.image.rgb_data);
        result.success = core_result.success;
        result.error_message = std::move(core_result.error_message);
        result.node_id = core_result.node_id;
        result.output_type = core_result.output_type;
        result.output_index = core_result.output_index;
        
        return result;
        
    } catch (const std::exception& e) {
        return orc::public_api::PreviewRenderResult{
            {}, false, e.what(), node_id, output_type, output_index
        };
    }
}

std::vector<orc::public_api::PreviewOutputInfo> RenderPresenter::getAvailableOutputs(NodeID node_id)
{
    if (!impl_->preview_renderer_) {
        return {};
    }
    
    auto core_outputs = impl_->preview_renderer_->get_available_outputs(node_id);
    
    // Convert to public API types
    std::vector<orc::public_api::PreviewOutputInfo> result;
    result.reserve(core_outputs.size());
    
    for (const auto& core_out : core_outputs) {
        orc::public_api::PreviewOutputInfo info;
        info.type = core_out.type;
        info.display_name = core_out.display_name;
        info.count = core_out.count;
        info.is_available = core_out.is_available;
        info.dar_aspect_correction = core_out.dar_aspect_correction;
        info.option_id = core_out.option_id;
        info.dropouts_available = core_out.dropouts_available;
        info.has_separate_channels = core_out.has_separate_channels;
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

VBIData RenderPresenter::getVBIData(NodeID node_id, FieldID field_id)
{
    VBIData result{false, false, "", "", "", "", "", {}};
    
    if (!impl_->obs_cache_) {
        return result;
    }
    
    try {
        // Render the field to populate observations
        auto field_opt = impl_->obs_cache_->get_field(node_id, field_id);
        if (!field_opt) {
            return result;
        }
        
        // Get observations and decode VBI
        const auto& obs_context = impl_->obs_cache_->get_observation_context();
        auto vbi_info_opt = VbiPresenter::decodeVbiFromObservation(&obs_context, field_id);
        
        if (vbi_info_opt.has_value() && vbi_info_opt->has_vbi_data) {
            const auto& info = vbi_info_opt.value();
            result.has_vbi = true;
            result.is_clv = (info.clv_timecode.has_value());
            
            // Extract chapter number
            if (info.chapter_number.has_value()) {
                result.chapter_number = std::to_string(info.chapter_number.value());
            }
            
            // Extract frame or picture number
            if (info.picture_number.has_value()) {
                result.picture_number = std::to_string(info.picture_number.value());
                result.frame_number = result.picture_number;  // For compatibility
            }
            
            // Extract stop code
            if (info.stop_code_present) {
                result.picture_stop_code = "present";
            }
            
            // Extract user code
            if (info.user_code.has_value()) {
                result.user_code = info.user_code.value();
            }
            
            // TODO: Extract raw VBI lines if available
        }
        
        return result;
        
    } catch (const std::exception&) {
        return result;
    }
}

bool RenderPresenter::requestDropoutData(
    NodeID node_id,
    uint64_t request_id,
    std::function<void(uint64_t, bool, const std::string&)> callback)
{
    // This is a synchronous operation - find the node and return data immediately
    if (!impl_->dag_) {
        if (callback) callback(request_id, false, "DAG not initialized");
        return false;
    }
    
    const orc::DAGNode* target_node = nullptr;
    for (const auto& node : impl_->dag_->nodes()) {
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
    if (!impl_->dag_) {
        if (callback) callback(request_id, false, "DAG not initialized");
        return false;
    }
    
    const orc::DAGNode* target_node = nullptr;
    for (const auto& node : impl_->dag_->nodes()) {
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
    if (!impl_->dag_) {
        if (callback) callback(request_id, false, "DAG not initialized");
        return false;
    }
    
    const orc::DAGNode* target_node = nullptr;
    for (const auto& node : impl_->dag_->nodes()) {
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
    // This is a placeholder - actual triggering would be async
    // For now, return a dummy request ID
    // TODO: Implement async triggering with progress callbacks
    static uint64_t next_id = 1;
    return next_id++;
}

void RenderPresenter::cancelTrigger()
{
    impl_->trigger_cancel_requested_ = true;
}

bool RenderPresenter::isTriggerActive() const
{
    return false;  // TODO: Track trigger state
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

std::vector<int16_t> RenderPresenter::getLineSamples(
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
        
        // Convert to int16_t and return
        std::vector<int16_t> result(descriptor->width);
        for (size_t i = 0; i < descriptor->width; ++i) {
            result[i] = static_cast<int16_t>(line_data[i]);
        }
        
        return result;
        
    } catch (const std::exception&) {
        return {};
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
        impl_->obs_cache_ = std::make_shared<orc::ObservationCache>(impl_->dag_);
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
    if (!impl_->dag_) {
        return false;
    }
    
    // Find the node in the DAG
    const orc::DAGNode* target_node = nullptr;
    for (const auto& node : impl_->dag_->nodes()) {
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
    if (!impl_->dag_) {
        return false;
    }
    
    // Find the node in the DAG
    const orc::DAGNode* target_node = nullptr;
    for (const auto& node : impl_->dag_->nodes()) {
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
    if (!impl_->dag_) {
        return false;
    }
    
    // Find the node in the DAG
    const orc::DAGNode* target_node = nullptr;
    for (const auto& node : impl_->dag_->nodes()) {
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

} // namespace orc::presenters
