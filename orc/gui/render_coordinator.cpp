/*
 * File:        render_coordinator.cpp
 * Module:      orc-gui
 * Purpose:     Thread-safe coordinator for rendering operations using presenters
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "render_coordinator.h"
#include "logging.h"
#include "render_presenter.h"
#include "preview_renderer.h"  // For mapping result types (ImageToFieldMappingResult, etc.)
#include <common_types.h>  // For analysis result types

// Phase 2.4: Analysis sink stage headers removed - now using RenderPresenter abstraction
// Removed: #include "dropout_analysis_sink_stage.h"
// Removed: #include "snr_analysis_sink_stage.h"
// Removed: #include "burst_level_analysis_sink_stage.h"

// Phase 2.7: Trigger operations migrated to RenderPresenter
// Removed: #include "ld_sink_stage.h"

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

void RenderCoordinator::setProject(orc::Project* project)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    worker_project_ = project;
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
    // This is a synchronous call - safe to call render presenter directly
    // since it's just a calculation with no state changes
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!worker_render_presenter_) {
        return orc::ImageToFieldMappingResult{false, 0, 0};
    }
    auto result = worker_render_presenter_->mapImageToField(node_id, output_type, output_index, image_y, image_height);
    return orc::ImageToFieldMappingResult{result.is_valid, result.field_index, result.field_line};
}

orc::FieldToImageMappingResult RenderCoordinator::mapFieldToImage(const orc::NodeID& node_id,
                                                                  orc::PreviewOutputType output_type,
                                                                  uint64_t output_index,
                                                                  uint64_t field_index,
                                                                  int field_line,
                                                                  int image_height)
{
    // This is a synchronous call - safe to call render presenter directly
    // since it's just a calculation with no state changes
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!worker_render_presenter_) {
        return orc::FieldToImageMappingResult{false, 0};
    }
    auto result = worker_render_presenter_->mapFieldToImage(node_id, output_type, output_index, field_index, field_line, image_height);
    return orc::FieldToImageMappingResult{result.is_valid, result.image_y};
}

orc::FrameFieldsResult RenderCoordinator::getFrameFields(const orc::NodeID& node_id, uint64_t frame_index)
{
    // This is a synchronous call - safe to call render presenter directly
    // since it's just a calculation with no state changes
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!worker_render_presenter_) {
        return orc::FrameFieldsResult{false, 0, 0};
    }
    auto result = worker_render_presenter_->getFrameFields(node_id, frame_index);
    return orc::FrameFieldsResult{result.is_valid, result.first_field, result.second_field};
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
    // Call cancelTrigger on the presenter (thread-safe)
    // The presenter's implementation sets a flag that the trigger operation will check
    if (worker_render_presenter_) {
        worker_render_presenter_->cancelTrigger();
    }
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
        worker_render_presenter_.reset();
        
        ORC_LOG_DEBUG("RenderCoordinator: Cleared all rendering state for empty project");
        return;
    }
    
    // Save current show_dropouts state before recreating presenter
    bool show_dropouts = false;
    if (worker_render_presenter_) {
        show_dropouts = worker_render_presenter_->getShowDropouts();
        ORC_LOG_DEBUG("RenderCoordinator: Preserving show_dropouts={}", show_dropouts);
    }
    
    // Update DAG
    worker_dag_ = req.dag;
    
    // Create or update render presenter
    try {
        if (!worker_project_) {
            ORC_LOG_ERROR("RenderCoordinator: No project set for presenter");
            return;
        }
        
        if (!worker_render_presenter_) {
            worker_render_presenter_ = std::make_unique<orc::presenters::RenderPresenter>(worker_project_);
        }
        
        // Set the new DAG
        worker_render_presenter_->setDAG(worker_dag_);
        
        // Restore show_dropouts state
        worker_render_presenter_->setShowDropouts(show_dropouts);
        ORC_LOG_DEBUG("RenderCoordinator: Restored show_dropouts={}", show_dropouts);
        
        ORC_LOG_DEBUG("RenderCoordinator: DAG updated successfully");
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: Failed to create presenter: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
    }
}

void RenderCoordinator::handleRenderPreview(const RenderPreviewRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Rendering preview for node '{}', type {}, index {} (request {})",
                  req.node_id.to_string(), static_cast<int>(req.output_type), req.output_index, req.request_id);
    
    if (!worker_render_presenter_) {
        ORC_LOG_ERROR("RenderCoordinator: Render presenter not initialized");
        emit error(req.request_id, "Render presenter not initialized");
        return;
    }
    
    try {
        auto result = worker_render_presenter_->renderPreview(
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
    
    if (!worker_render_presenter_) {
        emit vbiDataReady(req.request_id, orc::presenters::VBIFieldInfoView{});
        return;
    }
    
    try {
        auto vbi_data = worker_render_presenter_->getVBIData(req.node_id, req.field_id);
        
        // Convert presenter VBI data to view model
        orc::presenters::VBIFieldInfoView view;
        view.has_vbi_data = vbi_data.has_vbi;
        view.field_id = static_cast<int>(req.field_id.value());
        
        if (vbi_data.has_vbi) {
            if (!vbi_data.picture_number.empty()) {
                view.picture_number = std::stoi(vbi_data.picture_number);
            }
            if (!vbi_data.chapter_number.empty()) {
                view.chapter_number = std::stoi(vbi_data.chapter_number);
            }
            if (!vbi_data.user_code.empty()) {
                view.user_code = vbi_data.user_code;
            }
            view.stop_code_present = !vbi_data.picture_stop_code.empty();
        }
        
        emit vbiDataReady(req.request_id, view);
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: VBI decode failed: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
    }
}

void RenderCoordinator::handleGetDropoutData(const GetDropoutDataRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Getting dropout analysis data for node '{}', mode {} (request {})",
                  req.node_id.to_string(), static_cast<int>(req.mode), req.request_id);

    try {
        if (!worker_render_presenter_) {
            emit error(req.request_id, "Render presenter not initialized");
            return;
        }

        // Phase 2.4: Use RenderPresenter abstraction instead of direct DAG access
        std::vector<void*> data_ptr;
        int32_t total_frames = 0;
        
        if (!worker_render_presenter_->getDropoutAnalysisData(req.node_id, data_ptr, total_frames)) {
            emit error(req.request_id, "Failed to get dropout data - node may not be a DropoutAnalysisSinkStage or has no results");
            return;
        }

        if (data_ptr.empty()) {
            emit error(req.request_id, "No dropout dataset available");
            return;
        }

        // Cast back to the actual type
        auto* stats_vec = static_cast<const std::vector<orc::FrameDropoutStats>*>(data_ptr[0]);
        auto data = *stats_vec;  // Copy the data

        ORC_LOG_DEBUG("RenderCoordinator: Served dropout dataset from sink ({} buckets, {} frames total)",
                  data.size(), total_frames);
        emit dropoutDataReady(req.request_id, data, total_frames);

    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: Dropout analysis failed: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
    }
}

void RenderCoordinator::handleGetSNRData(const GetSNRDataRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Getting SNR analysis data for node '{}', mode {} (request {})",
                  req.node_id.to_string(), static_cast<int>(req.mode), req.request_id);

    try {
        if (!worker_render_presenter_) {
            emit error(req.request_id, "Render presenter not initialized");
            return;
        }

        // Phase 2.4: Use RenderPresenter abstraction instead of direct DAG access
        std::vector<void*> data_ptr;
        int32_t total_frames = 0;
        
        if (!worker_render_presenter_->getSNRAnalysisData(req.node_id, data_ptr, total_frames)) {
            emit error(req.request_id, "Failed to get SNR data - node may not be a SNRAnalysisSinkStage or has no results");
            return;
        }

        if (data_ptr.empty()) {
            emit error(req.request_id, "No SNR dataset available");
            return;
        }

        // Cast back to the actual type
        auto* stats_vec = static_cast<const std::vector<orc::FrameSNRStats>*>(data_ptr[0]);
        auto data = *stats_vec;  // Copy the data

        ORC_LOG_DEBUG("RenderCoordinator: Served SNR dataset from sink ({} frames)", data.size());
        emit snrDataReady(req.request_id, data, total_frames);

    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: SNR analysis failed: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
    }
}

void RenderCoordinator::handleGetBurstLevelData(const GetBurstLevelDataRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Getting burst level analysis data for node '{}' (request {})",
                  req.node_id.to_string(), req.request_id);

    try {
        if (!worker_render_presenter_) {
            emit error(req.request_id, "Render presenter not initialized");
            return;
        }

        // Phase 2.4: Use RenderPresenter abstraction instead of direct DAG access
        std::vector<void*> data_ptr;
        int32_t total_frames = 0;
        
        if (!worker_render_presenter_->getBurstLevelAnalysisData(req.node_id, data_ptr, total_frames)) {
            emit error(req.request_id, "Failed to get burst data - node may not be a BurstLevelAnalysisSinkStage or has no results");
            return;
        }

        if (data_ptr.empty()) {
            emit error(req.request_id, "No burst level dataset available");
            return;
        }

        // Cast back to the actual type
        auto* stats_vec = static_cast<const std::vector<orc::FrameBurstLevelStats>*>(data_ptr[0]);
        auto data = *stats_vec;  // Copy the data

        ORC_LOG_DEBUG("RenderCoordinator: Served burst dataset from sink ({} frames)", data.size());
        emit burstLevelDataReady(req.request_id, data, total_frames);

    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: Burst level analysis failed: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
    }
}

void RenderCoordinator::handleGetAvailableOutputs(const GetAvailableOutputsRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Getting available outputs for node '{}' (request {})",
                  req.node_id.to_string(), req.request_id);
    
    if (!worker_render_presenter_) {
        ORC_LOG_ERROR("RenderCoordinator: Render presenter not initialized");
        emit error(req.request_id, "Render presenter not initialized");
        return;
    }
    
    try {
        auto outputs = worker_render_presenter_->getAvailableOutputs(req.node_id);
        
        ORC_LOG_DEBUG("RenderCoordinator: Found {} available outputs", outputs.size());
        
        // Emit result on GUI thread (using public_api types directly)
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
    
    if (!worker_render_presenter_) {
        ORC_LOG_ERROR("RenderCoordinator: Render presenter not initialized");
        emit error(req.request_id, "Render presenter not initialized");
        return;
    }
    
    try {
        auto samples = worker_render_presenter_->getLineSamples(
            req.node_id, req.output_type, req.output_index,
            req.line_number, req.sample_x, req.preview_image_width
        );
        
        if (samples.empty()) {
            throw std::runtime_error("Line data not available");
        }
        
        // Get video parameters from the representation
        auto video_params = worker_render_presenter_->getVideoParameters(req.node_id);
        
        // For now, emit simple samples without Y/C separation
        // TODO: Extend RenderPresenter to provide Y/C samples separately
        emit lineSamplesReady(req.request_id, req.output_index, req.line_number, req.sample_x,
                            std::move(samples), video_params, {}, {});
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: Get line samples failed: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
    }
}

void RenderCoordinator::handleNavigateFrameLine(const NavigateFrameLineRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Navigating frame line for node '{}', field {}, line {}, direction {} (request {})",
                  req.node_id.to_string(), req.current_field, req.current_line, req.direction, req.request_id);
    
    if (!worker_render_presenter_) {
        ORC_LOG_ERROR("RenderCoordinator: Render presenter not initialized");
        emit error(req.request_id, "Render presenter not initialized");
        return;
    }
    
    try {
        // Use the render presenter's method to navigate
        auto nav_result = worker_render_presenter_->navigateFrameLine(
            req.node_id,
            req.output_type,
            req.current_field,
            req.current_line,
            req.direction,
            req.field_height
        );
        
        // Convert to public_api type for signal
        orc::public_api::FrameLineNavigationResult result;
        result.is_valid = nav_result.is_valid;
        result.new_field_index = nav_result.new_field_index;
        result.new_line_number = nav_result.new_line_number;
        
        // Emit result on GUI thread (using public_api types)
        emit frameLineNavigationReady(req.request_id, result);
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: Frame line navigation failed: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
    }
}

void RenderCoordinator::handleTriggerStage(const TriggerStageRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Triggering stage '{}' (request {})", req.node_id.to_string(), req.request_id);
    
    if (!worker_render_presenter_) {
        ORC_LOG_ERROR("RenderCoordinator: Render presenter not initialized");
        emit error(req.request_id, "Render presenter not initialized");
        emit triggerComplete(req.request_id, false, "Render presenter not initialized");
        return;
    }
    
    try {
        // Use RenderPresenter to handle triggering
        // The presenter abstracts all DAG access and stage interaction
        worker_render_presenter_->triggerStage(req.node_id, 
            [this](int current, int total, const std::string& message) {
                // Emit progress updates (Qt will queue to GUI thread)
                emit triggerProgress(current, total, QString::fromStdString(message));
            });
        
        ORC_LOG_DEBUG("RenderCoordinator: Trigger complete successfully");
        emit triggerComplete(req.request_id, true, "Trigger completed successfully");
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("RenderCoordinator: Trigger failed: {}", e.what());
        emit error(req.request_id, QString::fromStdString(e.what()));
        emit triggerComplete(req.request_id, false, QString::fromStdString(e.what()));
    }
}

void RenderCoordinator::setAspectRatioMode(orc::AspectRatioMode mode)
{
    // TODO: Add aspect ratio mode support to RenderPresenter
    ORC_LOG_DEBUG("RenderCoordinator: Aspect ratio mode set requested (not yet implemented in presenter)");
}

void RenderCoordinator::setShowDropouts(bool show)
{
    if (worker_render_presenter_) {
        worker_render_presenter_->setShowDropouts(show);
        ORC_LOG_DEBUG("RenderCoordinator: Show dropouts set to {}", show);
    }
}

void RenderCoordinator::handleSavePNG(const SavePNGRequest& req)
{
    ORC_LOG_DEBUG("RenderCoordinator: Saving PNG for node '{}', type {}, index {} to '{}'",
                 req.node_id.to_string(), static_cast<int>(req.output_type), req.output_index, req.filename);
    
    if (!worker_render_presenter_) {
        ORC_LOG_ERROR("RenderCoordinator: Render presenter not initialized");
        emit error(req.request_id, "Render presenter not initialized");
        return;
    }
    
    try {
        // Use presenter's PNG save functionality
        bool success = worker_render_presenter_->savePNG(
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
