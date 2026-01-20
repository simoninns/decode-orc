/*
 * File:        render_coordinator.cpp
 * Module:      orc-gui
 * Purpose:     Thread-safe coordinator for core rendering operations
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "render_coordinator.h"
#include "logging.h"
#include "dag_executor.h"
#include "project_to_dag.h"
#include "project.h"
#include "ld_sink_stage.h"  // For TriggerableStage
#include "observation_context.h"
#include "../core/include/vbi_decoder.h"
#include "../core/include/tbc_video_field_representation.h"

RenderCoordinator::RenderCoordinator(QObject* parent)
    : QObject(parent)
{
}

RenderCoordinator::~RenderCoordinator()
{
    stop();
}

void RenderCoordinator::start()
{
    if (worker_thread_.joinable()) {
        ORC_LOG_WARN("RenderCoordinator: Worker thread already running");
        return;
    }
    
    shutdown_requested_ = false;
    worker_thread_ = std::thread(&RenderCoordinator::workerLoop, this);
    
    ORC_LOG_DEBUG("RenderCoordinator: Worker thread started");
}

void RenderCoordinator::stop()
{
    if (!worker_thread_.joinable()) {
        return;
    }
    
    ORC_LOG_DEBUG("RenderCoordinator: Requesting shutdown...");
    
    // Send shutdown request
    shutdown_requested_ = true;
    
    // Wake up worker if waiting
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_cv_.notify_one();
    }
    
    // Wait for worker to finish
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    ORC_LOG_DEBUG("RenderCoordinator: Worker thread stopped");
}

uint64_t RenderCoordinator::nextRequestId()
{
    return next_request_id_.fetch_add(1);
}

void RenderCoordinator::enqueueRequest(std::unique_ptr<RenderRequest> request)
{
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        request_queue_.push(std::move(request));
    }
    queue_cv_.notify_one();
}

void RenderCoordinator::updateDAG(std::shared_ptr<const orc::DAG> dag)
{
    auto req = std::make_unique<UpdateDAGRequest>(nextRequestId(), std::move(dag));
    enqueueRequest(std::move(req));
}

uint64_t RenderCoordinator::requestPreview(const orc::NodeID& node_id,
                                          orc::PreviewOutputType output_type,
                                          uint64_t output_index,
                                          const std::string& option_id)
{
    uint64_t id = nextRequestId();
    auto req = std::make_unique<RenderPreviewRequest>(id, node_id, output_type, output_index, option_id);
    enqueueRequest(std::move(req));
    return id;
}

uint64_t RenderCoordinator::requestVBIData(const orc::NodeID& node_id, orc::FieldID field_id)
{
    uint64_t id = nextRequestId();
    auto req = std::make_unique<GetVBIDataRequest>(id, node_id, field_id);
    enqueueRequest(std::move(req));
    return id;
}

uint64_t RenderCoordinator::requestDropoutData(const orc::NodeID& node_id, orc::DropoutAnalysisMode mode)
{
    uint64_t id = nextRequestId();
    auto req = std::make_unique<GetDropoutDataRequest>(id, node_id, mode);
    enqueueRequest(std::move(req));
    return id;
}

uint64_t RenderCoordinator::requestSNRData(const orc::NodeID& node_id, orc::SNRAnalysisMode mode)
{
    uint64_t id = nextRequestId();
    auto req = std::make_unique<GetSNRDataRequest>(id, node_id, mode);
    enqueueRequest(std::move(req));
    return id;
}

uint64_t RenderCoordinator::requestBurstLevelData(const orc::NodeID& node_id)
{
    uint64_t id = nextRequestId();
    auto req = std::make_unique<GetBurstLevelDataRequest>(id, node_id);
    enqueueRequest(std::move(req));
    return id;
}

uint64_t RenderCoordinator::requestAvailableOutputs(const orc::NodeID& node_id)
{
    uint64_t id = nextRequestId();
    auto req = std::make_unique<GetAvailableOutputsRequest>(id, node_id);
    enqueueRequest(std::move(req));
    return id;
}

uint64_t RenderCoordinator::requestLineSamples(const orc::NodeID& node_id,
                                              orc::PreviewOutputType output_type,
                                              uint64_t output_index,
                                              int line_number,
                                              int sample_x,
                                              int preview_image_width)
{
    uint64_t id = nextRequestId();
    auto req = std::make_unique<GetLineSamplesRequest>(id, node_id, output_type, 
                                                        output_index, line_number, sample_x, preview_image_width);
    enqueueRequest(std::move(req));
    return id;
}

uint64_t RenderCoordinator::requestSavePNG(const orc::NodeID& node_id, 
                                          orc::PreviewOutputType output_type,
                                          uint64_t output_index,
                                          const std::string& filename,
                                          const std::string& option_id)
{
    uint64_t id = nextRequestId();
    auto req = std::make_unique<SavePNGRequest>(id, node_id, output_type, 
                                                 output_index, filename, option_id);
    enqueueRequest(std::move(req));
    return id;
}

uint64_t RenderCoordinator::requestFrameLineNavigation(const orc::NodeID& node_id,
                                                      orc::PreviewOutputType output_type,
                                                      uint64_t current_field,
                                                      int current_line,
                                                      int direction,
                                                      int field_height)
{
    uint64_t id = nextRequestId();
    auto req = std::make_unique<NavigateFrameLineRequest>(id, node_id, output_type,
                                                          current_field, current_line,
                                                          direction, field_height);
    enqueueRequest(std::move(req));
    return id;
}

orc::ImageToFieldMappingResult RenderCoordinator::mapImageToField(const orc::NodeID& node_id,
                                                                  orc::PreviewOutputType output_type,
                                                                  uint64_t output_index,
                                                                  int image_y,
                                                                  int image_height)
{
    // This is a synchronous call - safe to call worker_preview_renderer_ directly
    // since it's just a calculation with no state changes
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!worker_preview_renderer_) {
        return orc::ImageToFieldMappingResult{false, 0, 0};
    }
    return worker_preview_renderer_->map_image_to_field(node_id, output_type, output_index, image_y, image_height);
}

orc::FieldToImageMappingResult RenderCoordinator::mapFieldToImage(const orc::NodeID& node_id,
                                                                  orc::PreviewOutputType output_type,
                                                                  uint64_t output_index,
                                                                  uint64_t field_index,
                                                                  int field_line,
                                                                  int image_height)
{
    // This is a synchronous call - safe to call worker_preview_renderer_ directly
    // since it's just a calculation with no state changes
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!worker_preview_renderer_) {
        return orc::FieldToImageMappingResult{false, 0};
    }
    return worker_preview_renderer_->map_field_to_image(node_id, output_type, output_index, field_index, field_line, image_height);
}

orc::FrameFieldsResult RenderCoordinator::getFrameFields(const orc::NodeID& node_id, uint64_t frame_index)
{
    // This is a synchronous call - safe to call worker_preview_renderer_ directly
    // since it's just a calculation with no state changes
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!worker_preview_renderer_) {
        return orc::FrameFieldsResult{false, 0, 0};
    }
    return worker_preview_renderer_->get_frame_fields(node_id, frame_index);
}

uint64_t RenderCoordinator::requestTrigger(const orc::NodeID& node_id)
{
    uint64_t id = nextRequestId();
    auto req = std::make_unique<TriggerStageRequest>(id, node_id);
    enqueueRequest(std::move(req));
    return id;
}

void RenderCoordinator::cancelTrigger()
{
    trigger_cancel_requested_ = true;
    ORC_LOG_DEBUG("RenderCoordinator: Trigger cancellation requested");
}

// ============================================================================
// Worker Thread Implementation
// ============================================================================

void RenderCoordinator::workerLoop()
{
    ORC_LOG_DEBUG("RenderCoordinator: Worker thread loop started");
    
    while (!shutdown_requested_) {
        std::unique_ptr<RenderRequest> request;
        
        // Wait for a request
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !request_queue_.empty() || shutdown_requested_;
            });
            
            if (shutdown_requested_) {
                break;
            }
            
            if (!request_queue_.empty()) {
                request = std::move(request_queue_.front());
                request_queue_.pop();
            }
        }
        
        // Process the request
        if (request) {
            try {
                processRequest(std::move(request));
            } catch (const std::exception& e) {
                ORC_LOG_ERROR("RenderCoordinator: Exception processing request: {}", e.what());
                emit error(request->request_id, QString::fromStdString(e.what()));
            } catch (...) {
                ORC_LOG_ERROR("RenderCoordinator: Unknown exception processing request");
                emit error(request->request_id, "Unknown error");
            }
        }
    }
    
    ORC_LOG_DEBUG("RenderCoordinator: Worker thread loop exiting");
}

void RenderCoordinator::processRequest(std::unique_ptr<RenderRequest> request)
{
    switch (request->type) {
        case RenderRequestType::UpdateDAG:
            handleUpdateDAG(*static_cast<UpdateDAGRequest*>(request.get()));
            break;
            
        case RenderRequestType::RenderPreview:
            handleRenderPreview(*static_cast<RenderPreviewRequest*>(request.get()));
            break;
            
        case RenderRequestType::GetVBIData:
            handleGetVBIData(*static_cast<GetVBIDataRequest*>(request.get()));
            break;
            
        case RenderRequestType::GetDropoutData:
            handleGetDropoutData(*static_cast<GetDropoutDataRequest*>(request.get()));
            break;
            
        case RenderRequestType::GetSNRData:
            handleGetSNRData(*static_cast<GetSNRDataRequest*>(request.get()));
            break;
            
        case RenderRequestType::GetBurstLevelData:
            handleGetBurstLevelData(*static_cast<GetBurstLevelDataRequest*>(request.get()));
            break;
            
        case RenderRequestType::GetAvailableOutputs:
            handleGetAvailableOutputs(*static_cast<GetAvailableOutputsRequest*>(request.get()));
            break;
            
        case RenderRequestType::GetLineSamples:
            handleGetLineSamples(*static_cast<GetLineSamplesRequest*>(request.get()));
            break;
            
        case RenderRequestType::SavePNG:
            handleSavePNG(*static_cast<SavePNGRequest*>(request.get()));
            break;
            
        case RenderRequestType::NavigateFrameLine:
            handleNavigateFrameLine(*static_cast<NavigateFrameLineRequest*>(request.get()));
            break;
            
        case RenderRequestType::TriggerStage:
            handleTriggerStage(*static_cast<TriggerStageRequest*>(request.get()));
            break;
            
        case RenderRequestType::Shutdown:
            shutdown_requested_ = true;
            break;
            
        default:
            ORC_LOG_WARN("RenderCoordinator: Unknown request type: {}", static_cast<int>(request->type));
            break;
    }
}

void RenderCoordinator::handleUpdateDAG(const UpdateDAGRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Updating DAG (request {})", req.request_id);
    
    if (!req.dag) {
        // Null DAG is valid - happens with empty projects or projects with no stages
        ORC_LOG_WARN("RenderCoordinator: Received null DAG (empty project with no stages)");
        
        // Clear all worker state
        worker_dag_.reset();
        worker_obs_cache_.reset();
        worker_preview_renderer_.reset();
        worker_field_renderer_.reset();
        worker_vbi_decoder_.reset();
        worker_dropout_decoder_.reset();
        worker_snr_decoder_.reset();
        worker_burst_level_decoder_.reset();
        
        ORC_LOG_DEBUG("RenderCoordinator: Cleared all rendering state for empty project");
        return;
    }
    
    // Save current show_dropouts state before recreating preview renderer
    bool show_dropouts = false;
    if (worker_preview_renderer_) {
        show_dropouts = worker_preview_renderer_->get_show_dropouts();
        ORC_LOG_DEBUG("RenderCoordinator: Preserving show_dropouts={}", show_dropouts);
    }
    
    // Update DAG
    worker_dag_ = req.dag;
    
    // Create shared observation cache for all decoders
    worker_obs_cache_ = std::make_shared<orc::ObservationCache>(worker_dag_);
    
    // Recreate all renderers/decoders with new DAG
    try {
        worker_preview_renderer_ = std::make_unique<orc::PreviewRenderer>(worker_dag_);
        
        // Restore show_dropouts state
        worker_preview_renderer_->set_show_dropouts(show_dropouts);
        ORC_LOG_DEBUG("RenderCoordinator: Restored show_dropouts={}", show_dropouts);
        
        worker_field_renderer_ = std::make_unique<orc::DAGFieldRenderer>(worker_dag_);
        worker_vbi_decoder_ = std::make_unique<orc::VBIDecoder>();
        worker_dropout_decoder_ = std::make_unique<orc::DropoutAnalysisDecoder>(worker_dag_);
        worker_snr_decoder_ = std::make_unique<orc::SNRAnalysisDecoder>(worker_dag_);
        worker_burst_level_decoder_ = std::make_unique<orc::BurstLevelAnalysisDecoder>(worker_dag_);
        
        // Share the observation cache across analysis decoders
        // This prevents re-rendering fields that were already rendered by other components
        worker_dropout_decoder_->set_observation_cache(worker_obs_cache_);
        worker_snr_decoder_->set_observation_cache(worker_obs_cache_);
        worker_burst_level_decoder_->set_observation_cache(worker_obs_cache_);
        worker_burst_level_decoder_->set_observation_cache(worker_obs_cache_);
        
        ORC_LOG_DEBUG("RenderCoordinator: DAG updated successfully with shared observation cache");
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: Failed to create renderers: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
    }
}

void RenderCoordinator::handleRenderPreview(const RenderPreviewRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Rendering preview for node '{}', type {}, index {} (request {})",
                  req.node_id.to_string(), static_cast<int>(req.output_type), req.output_index, req.request_id);
    
    if (!worker_preview_renderer_) {
        ORC_LOG_ERROR("RenderCoordinator: Preview renderer not initialized");
        emit error(req.request_id, "Preview renderer not initialized");
        return;
    }
    
    try {
        auto result = worker_preview_renderer_->render_output(
            req.node_id,
            req.output_type,
            req.output_index,
            req.option_id
        );
        
        ORC_LOG_DEBUG("RenderCoordinator: Preview render complete, success={}", result.success);
        
        // Emit result on GUI thread
        emit previewReady(req.request_id, std::move(result));
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: Preview render failed: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
    }
}

void RenderCoordinator::handleGetVBIData(const GetVBIDataRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Getting VBI data for node '{}', field {} (request {})",
                  req.node_id.to_string(), req.field_id.value(), req.request_id);
    
    try {
        // Prefer VBI hints from source metadata if available
        if (!worker_obs_cache_) {
            ORC_LOG_WARN("RenderCoordinator: Observation cache not initialized; cannot access VBI hints");
        } else {
            auto field_opt = worker_obs_cache_->get_field(req.node_id, req.field_id);
            if (field_opt && *field_opt) {
                auto vfr = *field_opt;
                // Try dynamic cast to TBC source to access VBI metadata hint
                auto tbc_vfr = std::dynamic_pointer_cast<const orc::TBCVideoFieldRepresentation>(vfr);
                if (tbc_vfr) {
                    auto vbi_hint = tbc_vfr->get_vbi_hint(req.field_id);
                    if (vbi_hint && vbi_hint->in_use) {
                        orc::VBIFieldInfo info;
                        info.field_id = req.field_id;
                        info.has_vbi_data = true;
                        info.vbi_data = vbi_hint->vbi_data;
                        ORC_LOG_DEBUG("RenderCoordinator: VBI hint delivered from metadata for field {}", req.field_id.value());
                        emit vbiDataReady(req.request_id, info);
                        return;
                    }
                }
            }
        }

        // Fallback: ObservationContext-based decoding (requires observers to populate)
        orc::ObservationContext obs_context;
        auto vbi_info_opt = orc::VBIDecoder::decode_vbi(obs_context, req.field_id);
        if (vbi_info_opt.has_value() && vbi_info_opt->has_vbi_data) {
            ORC_LOG_DEBUG("RenderCoordinator: VBI decode complete from ObservationContext");
            emit vbiDataReady(req.request_id, vbi_info_opt.value());
        } else {
            ORC_LOG_DEBUG("VBIDecoder: No VBI data found for field {}", req.field_id.value());
            emit vbiDataReady(req.request_id, orc::VBIFieldInfo{});
        }
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: VBI decode failed: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
    }
}

void RenderCoordinator::handleGetDropoutData(const GetDropoutDataRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Getting dropout analysis data for node '{}', mode {} (request {})",
                  req.node_id.to_string(), static_cast<int>(req.mode), req.request_id);
    
    if (!worker_dropout_decoder_) {
        ORC_LOG_ERROR("RenderCoordinator: Dropout decoder not initialized");
        emit error(req.request_id, "Dropout decoder not initialized");
        return;
    }
    
    try {
        // Get dropout stats for all frames
        // Fields are cached in shared ObservationCache, so this is fast
        auto frame_stats = worker_dropout_decoder_->get_dropout_by_frames(
            req.node_id,
            req.mode,
            0,  // 0 = process all frames
            [this](size_t current, size_t total, const std::string& message) {
                emit dropoutProgress(current, total, QString::fromStdString(message));
            }
        );
        
        // Count total frames (max frame number in stats)
        int32_t total_frames = 0;
        for (const auto& stats : frame_stats) {
            if (stats.frame_number > total_frames) {
                total_frames = stats.frame_number;
            }
        }
        
        ORC_LOG_DEBUG("RenderCoordinator: Dropout analysis complete, {} frames processed", frame_stats.size());
        
        emit dropoutDataReady(req.request_id, std::move(frame_stats), total_frames);
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: Dropout analysis failed: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
    }
}

void RenderCoordinator::handleGetSNRData(const GetSNRDataRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Getting SNR analysis data for node '{}', mode {} (request {})",
                  req.node_id.to_string(), static_cast<int>(req.mode), req.request_id);
    
    if (!worker_snr_decoder_) {
        ORC_LOG_ERROR("RenderCoordinator: SNR decoder not initialized");
        emit error(req.request_id, "SNR decoder not initialized");
        return;
    }
    
    try {
        // Get SNR stats for all frames  
        // Fields are cached in shared ObservationCache, so this is fast
        auto frame_stats = worker_snr_decoder_->get_snr_by_frames(
            req.node_id,
            req.mode,
            0,  // 0 = process all frames
            [this](size_t current, size_t total, const std::string& message) {
                emit snrProgress(current, total, QString::fromStdString(message));
            }
        );
        
        // Count total frames (max frame number in stats)
        int32_t total_frames = 0;
        for (const auto& stats : frame_stats) {
            if (stats.frame_number > total_frames) {
                total_frames = stats.frame_number;
            }
        }
        
        ORC_LOG_DEBUG("RenderCoordinator: SNR analysis complete, {} frames processed", frame_stats.size());
        
        emit snrDataReady(req.request_id, std::move(frame_stats), total_frames);
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: SNR analysis failed: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
    }
}

void RenderCoordinator::handleGetBurstLevelData(const GetBurstLevelDataRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Getting burst level analysis data for node '{}' (request {})",
                  req.node_id.to_string(), req.request_id);
    
    if (!worker_burst_level_decoder_) {
        ORC_LOG_ERROR("RenderCoordinator: Burst level decoder not initialized");
        emit error(req.request_id, "Burst level decoder not initialized");
        return;
    }
    
    try {
        // Get burst level stats for all frames  
        // Fields are cached in shared ObservationCache, so this is fast
        auto frame_stats = worker_burst_level_decoder_->get_burst_level_by_frames(
            req.node_id,
            0,  // 0 = process all frames
            [this](size_t current, size_t total, const std::string& message) {
                emit burstLevelProgress(current, total, QString::fromStdString(message));
            }
        );
        
        // Count total frames (max frame number in stats)
        int32_t total_frames = 0;
        for (const auto& stats : frame_stats) {
            if (stats.frame_number > total_frames) {
                total_frames = stats.frame_number;
            }
        }
        
        ORC_LOG_DEBUG("RenderCoordinator: Burst level analysis complete, {} frames processed", frame_stats.size());
        
        emit burstLevelDataReady(req.request_id, std::move(frame_stats), total_frames);
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: Burst level analysis failed: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
    }
}

void RenderCoordinator::handleGetAvailableOutputs(const GetAvailableOutputsRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Getting available outputs for node '{}' (request {})",
                  req.node_id.to_string(), req.request_id);
    
    if (!worker_preview_renderer_) {
        ORC_LOG_ERROR("RenderCoordinator: Preview renderer not initialized");
        emit error(req.request_id, "Preview renderer not initialized");
        return;
    }
    
    try {
        auto outputs = worker_preview_renderer_->get_available_outputs(req.node_id);
        
        ORC_LOG_DEBUG("RenderCoordinator: Found {} available outputs", outputs.size());
        
        // Emit result on GUI thread
        emit availableOutputsReady(req.request_id, std::move(outputs));
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: Get available outputs failed: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
    }
}

void RenderCoordinator::handleGetLineSamples(const GetLineSamplesRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Getting line samples for node '{}', line {} (request {})",
                  req.node_id.to_string(), req.line_number, req.request_id);
    
    if (!worker_preview_renderer_) {
        ORC_LOG_ERROR("RenderCoordinator: Preview renderer not initialized");
        emit error(req.request_id, "Preview renderer not initialized");
        return;
    }
    
    try {
        // Get the representation at this node
        auto repr = worker_preview_renderer_->get_representation_at_node(req.node_id);
        if (!repr) {
            throw std::runtime_error("No representation available at node");
        }
        
        // Determine which field to get samples from
        orc::FieldID field_id;
        if (req.output_type == orc::PreviewOutputType::Field) {
            // Field mode or post-mapping field index
            // When Frame mode is used, GUI should first call mapImageToField() to get the field index,
            // then send request with PreviewOutputType::Field.
            // This ensures all field ordering logic is in orc-core.
            field_id = orc::FieldID(req.output_index);
        } else {
            throw std::runtime_error("Unsupported output type for line scope - must be Field");
        }
        
        // Get descriptor to know field dimensions
        auto descriptor = repr->get_descriptor(field_id);
        if (!descriptor) {
            throw std::runtime_error("Field descriptor not available");
        }
        
        // Validate line number is within bounds
        if (req.line_number < 0 || static_cast<size_t>(req.line_number) >= descriptor->height) {
            throw std::runtime_error("Line number out of bounds");
        }
        
        // Check if this is a YC source
        bool is_yc_source = repr->has_separate_channels();
        
        std::vector<uint16_t> samples;
        std::vector<uint16_t> y_samples;
        std::vector<uint16_t> c_samples;
        
        if (is_yc_source) {
            // YC source - get Y and C separately
            ORC_LOG_DEBUG("Requesting Y line for field {} line {}", field_id.value(), req.line_number);
            const uint16_t* y_line_data = repr->get_line_luma(field_id, req.line_number);
            ORC_LOG_DEBUG("Y pointer: {}, first 5 values: {} {} {} {} {}", 
                          static_cast<const void*>(y_line_data),
                          y_line_data ? y_line_data[0] : 0,
                          y_line_data ? y_line_data[1] : 0,
                          y_line_data ? y_line_data[2] : 0,
                          y_line_data ? y_line_data[3] : 0,
                          y_line_data ? y_line_data[4] : 0);
            
            ORC_LOG_DEBUG("Requesting C line for field {} line {}", field_id.value(), req.line_number);
            const uint16_t* c_line_data = repr->get_line_chroma(field_id, req.line_number);
            ORC_LOG_DEBUG("C pointer: {}, first 5 values: {} {} {} {} {}", 
                          static_cast<const void*>(c_line_data),
                          c_line_data ? c_line_data[0] : 0,
                          c_line_data ? c_line_data[1] : 0,
                          c_line_data ? c_line_data[2] : 0,
                          c_line_data ? c_line_data[3] : 0,
                          c_line_data ? c_line_data[4] : 0);
            
            if (!y_line_data || !c_line_data) {
                throw std::runtime_error("Y or C line data not available");
            }
            
            // Copy the line samples
            y_samples.assign(y_line_data, y_line_data + descriptor->width);
            c_samples.assign(c_line_data, c_line_data + descriptor->width);
            
            // For composite view, also get composite if available
            const uint16_t* composite_line_data = repr->get_line(field_id, req.line_number);
            if (composite_line_data) {
                samples.assign(composite_line_data, composite_line_data + descriptor->width);
            }
            
            // Debug: Check if Y and C data are actually different
            bool are_different = false;
            if (y_samples.size() == c_samples.size() && !y_samples.empty()) {
                for (size_t i = 0; i < std::min(size_t(10), y_samples.size()); ++i) {
                    if (y_samples[i] != c_samples[i]) {
                        are_different = true;
                        break;
                    }
                }
                if (!are_different) {
                    ORC_LOG_WARN("RenderCoordinator: Y and C samples appear identical! First 10 Y: {} {} {} {} {}, C: {} {} {} {} {}",
                                 y_samples[0], y_samples[1], y_samples[2], y_samples[3], y_samples[4],
                                 c_samples[0], c_samples[1], c_samples[2], c_samples[3], c_samples[4]);
                }
            }
            
            ORC_LOG_DEBUG("RenderCoordinator: Retrieved {} Y samples and {} C samples from YC source (different: {})",
                          y_samples.size(), c_samples.size(), are_different);
        } else {
            // Composite source - get standard line
            const uint16_t* line_data = repr->get_line(field_id, req.line_number);
            if (!line_data) {
                throw std::runtime_error("Line data not available");
            }
            
            // Copy the line samples
            samples.assign(line_data, line_data + descriptor->width);
            
            ORC_LOG_DEBUG("RenderCoordinator: Retrieved {} composite samples", samples.size());
        }
        
        // Get video parameters for markers
        auto video_params = repr->get_video_parameters();
        
        // Map the image X coordinate from preview image space to field sample space
        // The preview image may have aspect ratio correction applied, so its width
        // differs from the field width. We need to scale the X coordinate accordingly.
        int actual_sample_x = req.sample_x;
        
        if (req.preview_image_width > 0) {
            // Map from preview image coordinates to field sample coordinates
            actual_sample_x = (req.sample_x * static_cast<int>(descriptor->width)) / req.preview_image_width;
        }
        
        // Clamp to valid sample range
        if (actual_sample_x < 0) actual_sample_x = 0;
        if (actual_sample_x >= static_cast<int>(descriptor->width)) {
            actual_sample_x = static_cast<int>(descriptor->width) - 1;
        }
        
        ORC_LOG_DEBUG("RenderCoordinator: sample_x {} (preview width {}, field width {}, mapped to {})", 
                      req.sample_x, req.preview_image_width, descriptor->width, actual_sample_x);
        
        // Emit result on GUI thread with Y and C samples
        emit lineSamplesReady(req.request_id, req.output_index, req.line_number, actual_sample_x, 
                            std::move(samples), video_params, std::move(y_samples), std::move(c_samples));
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: Get line samples failed: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
    }
}

void RenderCoordinator::handleNavigateFrameLine(const NavigateFrameLineRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Navigating frame line for node '{}', field {}, line {}, direction {} (request {})",
                  req.node_id.to_string(), req.current_field, req.current_line, req.direction, req.request_id);
    
    if (!worker_preview_renderer_) {
        ORC_LOG_ERROR("RenderCoordinator: Preview renderer not initialized");
        emit error(req.request_id, "Preview renderer not initialized");
        return;
    }
    
    try {
        // Use the preview renderer's method to navigate
        auto result = worker_preview_renderer_->navigate_frame_line(
            req.node_id,
            req.output_type,
            req.current_field,
            req.current_line,
            req.direction,
            req.field_height
        );
        
        // Emit result on GUI thread
        emit frameLineNavigationReady(req.request_id, result);
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: Frame line navigation failed: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
    }
}

void RenderCoordinator::handleTriggerStage(const TriggerStageRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Triggering stage '{}' (request {})", req.node_id.to_string(), req.request_id);
    
    if (!worker_dag_) {
        ORC_LOG_ERROR("RenderCoordinator: DAG not initialized");
        emit error(req.request_id, "DAG not initialized");
        return;
    }
    
    trigger_cancel_requested_ = false;
    
    try {
        // Find the target node in the DAG
        const orc::DAGNode* target_node = nullptr;
        for (const auto& node : worker_dag_->nodes()) {
            if (node.node_id == req.node_id) {
                target_node = &node;
                break;
            }
        }
        
        if (!target_node) {
            emit error(req.request_id, QString::fromStdString("Node '" + req.node_id.to_string() + "' not found in DAG"));
            emit triggerComplete(req.request_id, false, "Node not found");
            return;
        }
        
        auto trigger_stage = dynamic_cast<orc::TriggerableStage*>(target_node->stage.get());
        if (!trigger_stage) {
            emit error(req.request_id, QString::fromStdString("Stage '" + req.node_id.to_string() + "' is not triggerable"));
            emit triggerComplete(req.request_id, false, "Stage not triggerable");
            return;
        }
        
        // Build executor to get inputs for this node
        auto executor = std::make_shared<orc::DAGExecutor>();
        
        // Execute DAG up to (but not including) the target node to get its inputs
        std::vector<orc::ArtifactPtr> inputs;
        
        if (!target_node->input_node_ids.empty()) {
            // Execute predecessor nodes to get inputs
            auto node_outputs = executor->execute_to_node(*worker_dag_, target_node->input_node_ids[0]);
            
            // Collect inputs from predecessor nodes
            for (size_t i = 0; i < target_node->input_node_ids.size(); ++i) {
                const auto& input_node_id = target_node->input_node_ids[i];
                size_t input_index = (i < target_node->input_indices.size()) ? target_node->input_indices[i] : 0;
                
                auto it = node_outputs.find(input_node_id);
                if (it != node_outputs.end() && input_index < it->second.size()) {
                    inputs.push_back(it->second[input_index]);
                } else {
                    ORC_LOG_WARN("RenderCoordinator: Failed to get input {} from node '{}'",
                                input_index, input_node_id.to_string());
                }
            }
        }
        
        ORC_LOG_DEBUG("RenderCoordinator: Triggering with {} input(s)", inputs.size());
        
        // Store pointer to current trigger stage for cancellation
        current_trigger_stage_ = trigger_stage;
        
        // Set up progress callback (called from THIS worker thread, safe to emit signals)
        trigger_stage->set_progress_callback([this, trigger_stage, req_id = req.request_id](
            size_t current, size_t total, const std::string& message) {
            
            // Check for cancellation and call cancel_trigger() on the stage
            if (trigger_cancel_requested_) {
                ORC_LOG_DEBUG("RenderCoordinator: Trigger cancellation requested, calling cancel_trigger()");
                trigger_stage->cancel_trigger();
            }
            
            // Emit progress (Qt will queue this to GUI thread)
            emit triggerProgress(current, total, QString::fromStdString(message));
        });
        
        // Execute trigger
        orc::ObservationContext obs_context;
        bool success = trigger_stage->trigger(inputs, target_node->parameters, obs_context);
        std::string status = trigger_stage->get_trigger_status();
        
        ORC_LOG_DEBUG("RenderCoordinator: Trigger complete, success={}, status={}", success, status);
        
        // Clear current trigger stage pointer
        current_trigger_stage_ = nullptr;
        
        // Emit completion
        emit triggerComplete(req.request_id, success, QString::fromStdString(status));
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: Trigger failed: {}", e.what());
        current_trigger_stage_ = nullptr;  // Clear pointer on error too
        emit error(req.request_id, QString::fromStdString(e.what()));
        emit triggerComplete(req.request_id, false, QString::fromStdString("Exception: " + std::string(e.what())));
    }
    
    trigger_cancel_requested_ = false;
}

void RenderCoordinator::setAspectRatioMode(orc::AspectRatioMode mode)
{
    if (worker_preview_renderer_) {
        worker_preview_renderer_->set_aspect_ratio_mode(mode);
        ORC_LOG_DEBUG("RenderCoordinator: Aspect ratio mode set to {}", 
                      mode == orc::AspectRatioMode::SAR_1_1 ? "SAR 1:1" : "DAR 4:3");
    }
}

void RenderCoordinator::setShowDropouts(bool show)
{
    if (worker_preview_renderer_) {
        worker_preview_renderer_->set_show_dropouts(show);
        ORC_LOG_DEBUG("RenderCoordinator: Show dropouts set to {}", show);
    }
}

orc::DropoutAnalysisDecoder* RenderCoordinator::getDropoutAnalysisDecoder()
{
    return worker_dropout_decoder_ ? worker_dropout_decoder_.get() : nullptr;
}

orc::SNRAnalysisDecoder* RenderCoordinator::getSNRAnalysisDecoder()
{
    return worker_snr_decoder_ ? worker_snr_decoder_.get() : nullptr;
}

orc::BurstLevelAnalysisDecoder* RenderCoordinator::getBurstLevelAnalysisDecoder()
{
    return worker_burst_level_decoder_ ? worker_burst_level_decoder_.get() : nullptr;
}

void RenderCoordinator::handleSavePNG(const SavePNGRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Saving PNG for node '{}', type {}, index {} to '{}'",
                 req.node_id.to_string(), static_cast<int>(req.output_type), req.output_index, req.filename);
    
    if (!worker_preview_renderer_) {
        ORC_LOG_ERROR("RenderCoordinator: Preview renderer not initialized");
        emit error(req.request_id, "Preview renderer not initialized");
        return;
    }
    
    try {
        // Use core's native PNG save functionality (no GUI business logic)
        bool success = worker_preview_renderer_->save_png(
            req.node_id,
            req.output_type,
            req.output_index,
            req.filename,
            req.option_id
        );
        
        if (success) {
            ORC_LOG_DEBUG("RenderCoordinator: PNG saved successfully to '{}'", req.filename);
        } else {
            ORC_LOG_ERROR("RenderCoordinator: Failed to save PNG to '{}'", req.filename);
            emit error(req.request_id, QString::fromStdString("Failed to save PNG file: " + req.filename));
        }
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: PNG export failed: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
    }
}
