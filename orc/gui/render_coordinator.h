/*
 * File:        render_coordinator.h
 * Module:      orc-gui
 * Purpose:     Thread-safe coordinator for rendering operations using presenters
 *
 * This class implements an Actor Model pattern where rendering state
 * is owned by a single worker thread. The GUI thread sends requests via
 * a thread-safe queue and receives responses via Qt signals.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef RENDER_COORDINATOR_H
#define RENDER_COORDINATOR_H

#include <QObject>
#include <QString>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <functional>
#include <field_id.h>
#include <node_id.h>
#include <common_types.h>
#include <orc_rendering.h>  // Public API rendering types
#include "vbi_view_models.h"

// Temporary includes for types used in signals - to be migrated to public API
#include "preview_renderer.h"  // For PreviewOutputInfo, FrameLineNavigationResult

namespace orc {
    class DAG;
    class Project;
    class TriggerableStage;
}

namespace orc::presenters {
    class RenderPresenter;
}

// Forward declarations
class GUIProject;

/**
 * @brief Request types for the render coordinator
 */
enum class RenderRequestType {
    UpdateDAG,              // Update the DAG being rendered
    RenderPreview,          // Render a preview image
    GetVBIData,             // Decode VBI data for a field
    GetDropoutData,         // Get dropout analysis data
    GetSNRData,             // Get SNR analysis data
    GetBurstLevelData,      // Get burst level analysis data
    TriggerStage,           // Trigger a stage (batch processing)
    CancelTrigger,          // Cancel ongoing trigger
    GetAvailableOutputs,    // Query available preview outputs
    GetLineSamples,         // Get 16-bit samples for a line
    SavePNG,                // Save preview as PNG file
    NavigateFrameLine,      // Navigate to next/previous line in frame mode
    Shutdown                // Shutdown the worker thread
};

/**
 * @brief Base class for all requests
 */
struct RenderRequest {
    RenderRequestType type;
    uint64_t request_id;  // Unique ID to match responses
    
    virtual ~RenderRequest() = default;
    
protected:
    explicit RenderRequest(RenderRequestType t, uint64_t id) 
        : type(t), request_id(id) {}
};

/**
 * @brief Request to update the DAG
 */
struct UpdateDAGRequest : public RenderRequest {
    std::shared_ptr<const orc::DAG> dag;
    
    UpdateDAGRequest(uint64_t id, std::shared_ptr<const orc::DAG> d)
        : RenderRequest(RenderRequestType::UpdateDAG, id)
        , dag(std::move(d)) {}
};

/**
 * @brief Request to render a preview
 */
struct RenderPreviewRequest : public RenderRequest {
    orc::NodeID node_id;
    orc::PreviewOutputType output_type;
    uint64_t output_index;
    std::string option_id;
    
    RenderPreviewRequest(uint64_t id, orc::NodeID node, 
                        orc::PreviewOutputType type, uint64_t index,
                        std::string opt_id = "")
        : RenderRequest(RenderRequestType::RenderPreview, id)
        , node_id(std::move(node))
        , output_type(type)
        , output_index(index)
        , option_id(std::move(opt_id)) {}
};

/**
 * @brief Request to get VBI data
 */
struct GetVBIDataRequest : public RenderRequest {
    orc::NodeID node_id;
    orc::FieldID field_id;
    
    GetVBIDataRequest(uint64_t id, orc::NodeID node, orc::FieldID fid)
        : RenderRequest(RenderRequestType::GetVBIData, id)
        , node_id(std::move(node))
        , field_id(fid) {}
};

/**
 * @brief Request to get dropout analysis data for all fields
 */
struct GetDropoutDataRequest : public RenderRequest {
    orc::NodeID node_id;
    orc::DropoutAnalysisMode mode;
    
    GetDropoutDataRequest(uint64_t id, orc::NodeID node, orc::DropoutAnalysisMode m)
        : RenderRequest(RenderRequestType::GetDropoutData, id)
        , node_id(std::move(node))
        , mode(m) {}
};

/**
 * @brief Request to get SNR analysis data for all fields
 */
struct GetSNRDataRequest : public RenderRequest {
    orc::NodeID node_id;
    orc::SNRAnalysisMode mode;
    
    GetSNRDataRequest(uint64_t id, orc::NodeID node, orc::SNRAnalysisMode m)
        : RenderRequest(RenderRequestType::GetSNRData, id)
        , node_id(std::move(node))
        , mode(m) {}
};

/**
 * @brief Request to get burst level analysis data for all fields
 */
struct GetBurstLevelDataRequest : public RenderRequest {
    orc::NodeID node_id;
    
    GetBurstLevelDataRequest(uint64_t id, orc::NodeID node)
        : RenderRequest(RenderRequestType::GetBurstLevelData, id)
        , node_id(std::move(node)) {}
};

/**
 * @brief Request to trigger a stage
 */
struct TriggerStageRequest : public RenderRequest {
    orc::NodeID node_id;
    
    explicit TriggerStageRequest(uint64_t id, orc::NodeID node)
        : RenderRequest(RenderRequestType::TriggerStage, id)
        , node_id(std::move(node)) {}
};

/**
 * @brief Request to save preview as PNG
 */
struct SavePNGRequest : public RenderRequest {
    orc::NodeID node_id;
    orc::PreviewOutputType output_type;
    uint64_t output_index;
    std::string filename;
    std::string option_id;
    
    SavePNGRequest(uint64_t id, orc::NodeID node, 
                   orc::PreviewOutputType type, uint64_t index,
                   std::string file, std::string opt_id = "")
        : RenderRequest(RenderRequestType::SavePNG, id)
        , node_id(std::move(node))
        , output_type(type)
        , output_index(index)
        , filename(std::move(file))
        , option_id(std::move(opt_id)) {}
};

/**
 * @brief Request to get available outputs
 */
struct GetAvailableOutputsRequest : public RenderRequest {
    orc::NodeID node_id;
    
    GetAvailableOutputsRequest(uint64_t id, orc::NodeID node)
        : RenderRequest(RenderRequestType::GetAvailableOutputs, id)
        , node_id(std::move(node)) {}
};

/**
 * @brief Request to get line samples
 */
struct GetLineSamplesRequest : public RenderRequest {
    orc::NodeID node_id;
    orc::PreviewOutputType output_type;
    uint64_t output_index;
    int line_number;
    int sample_x;
    int preview_image_width;  // Width of the preview image for coordinate mapping
    
    GetLineSamplesRequest(uint64_t id, orc::NodeID node, orc::PreviewOutputType type,
                         uint64_t index, int line, int x, int img_width)
        : RenderRequest(RenderRequestType::GetLineSamples, id)
        , node_id(std::move(node))
        , output_type(type)
        , output_index(index)
        , line_number(line)
        , sample_x(x)
        , preview_image_width(img_width) {}
};

/**
 * @brief Request to navigate to next/previous line in frame mode
 */
struct NavigateFrameLineRequest : public RenderRequest {
    orc::NodeID node_id;
    orc::PreviewOutputType output_type;
    uint64_t current_field;
    int current_line;
    int direction;  // +1 for down, -1 for up
    int field_height;  // Height of a single field in lines
    
    NavigateFrameLineRequest(uint64_t id, orc::NodeID node, 
                            orc::PreviewOutputType type, uint64_t field,
                            int line, int dir, int height)
        : RenderRequest(RenderRequestType::NavigateFrameLine, id)
        , node_id(std::move(node))
        , output_type(type)
        , current_field(field)
        , current_line(line)
        , direction(dir)
        , field_height(height) {}
};

/**
 * @brief Base class for responses
 */
struct RenderResponse {
    uint64_t request_id;
    bool success;
    std::string error_message;
    
    virtual ~RenderResponse() = default;
    
protected:
    RenderResponse(uint64_t id, bool s, std::string err = "")
        : request_id(id), success(s), error_message(std::move(err)) {}
};

/**
 * @brief Response with preview render result
 */
struct PreviewRenderResponse : public RenderResponse {
    orc::public_api::PreviewRenderResult result;
    
    PreviewRenderResponse(uint64_t id, bool s, 
                         orc::public_api::PreviewRenderResult r, std::string err = "")
        : RenderResponse(id, s, std::move(err))
        , result(std::move(r)) {}
};

/**
 * @brief Response with VBI data
 */
struct VBIDataResponse : public RenderResponse {
    orc::presenters::VBIFieldInfoView vbi_info;
    
    VBIDataResponse(uint64_t id, bool s, 
                   orc::presenters::VBIFieldInfoView info, std::string err = "")
        : RenderResponse(id, s, std::move(err))
        , vbi_info(std::move(info)) {}
};

/**
 * @brief Response with dropout analysis data
 */
struct DropoutDataResponse : public RenderResponse {
    std::vector<orc::FrameDropoutStats> frame_stats;
    int32_t total_frames;
    
    DropoutDataResponse(uint64_t id, bool s,
                       std::vector<orc::FrameDropoutStats> stats,
                       int32_t total, std::string err = "")
        : RenderResponse(id, s, std::move(err))
        , frame_stats(std::move(stats))
        , total_frames(total) {}
};

/**
 * @brief Response with SNR analysis data
 */
struct SNRDataResponse : public RenderResponse {
    std::vector<orc::FrameSNRStats> frame_stats;
    int32_t total_frames;
    
    SNRDataResponse(uint64_t id, bool s,
                   std::vector<orc::FrameSNRStats> stats,
                   int32_t total, std::string err = "")
        : RenderResponse(id, s, std::move(err))
        , frame_stats(std::move(stats))
        , total_frames(total) {}
};

/**
 * @brief Response with burst level analysis data
 */
struct BurstLevelDataResponse : public RenderResponse {
    std::vector<orc::FrameBurstLevelStats> frame_stats;
    int32_t total_frames;
    
    BurstLevelDataResponse(uint64_t id, bool s,
                          std::vector<orc::FrameBurstLevelStats> stats,
                          int32_t total, std::string err = "")
        : RenderResponse(id, s, std::move(err))
        , frame_stats(std::move(stats))
        , total_frames(total) {}
};

/**
 * @brief Response with available outputs
 */
struct AvailableOutputsResponse : public RenderResponse {
    std::vector<orc::PreviewOutputInfo> outputs;
    
    AvailableOutputsResponse(uint64_t id, bool s,
                            std::vector<orc::PreviewOutputInfo> out, std::string err = "")
        : RenderResponse(id, s, std::move(err))
        , outputs(std::move(out)) {}
};

/**
 * @brief Response for trigger completion
 */
struct TriggerCompleteResponse : public RenderResponse {
    std::string status_message;
    
    TriggerCompleteResponse(uint64_t id, bool s, std::string status, std::string err = "")
        : RenderResponse(id, s, std::move(err))
        , status_message(std::move(status)) {}
};

/**
 * @brief Response for frame line navigation
 */
struct FrameLineNavigationResponse : public RenderResponse {
    orc::FrameLineNavigationResult result;
    
    FrameLineNavigationResponse(uint64_t id, bool s,
                               orc::FrameLineNavigationResult nav_result, std::string err = "")
        : RenderResponse(id, s, std::move(err))
        , result(nav_result) {}
};

/**
 * @brief Coordinator that owns all core rendering state in a worker thread
 * 
 * Architecture:
 * - Worker thread owns: DAG, PreviewRenderer, DAGFieldRenderer, all decoders
 * - GUI thread sends requests via thread-safe queue
 * - Worker thread processes requests serially (no races possible)
 * - Responses sent back via Qt signals (thread-safe)
 * 
 * Thread Safety:
 * - ALL public methods are thread-safe (called from GUI thread)
 * - Worker thread methods are private and run on worker thread only
 * - No shared mutable state between threads
 */
class RenderCoordinator : public QObject {
    Q_OBJECT
    
public:
    explicit RenderCoordinator(QObject* parent = nullptr);
    ~RenderCoordinator();
    
    // Prevent copying/moving
    RenderCoordinator(const RenderCoordinator&) = delete;
    RenderCoordinator& operator=(const RenderCoordinator&) = delete;
    
    // ========================================================================
    // Public API (thread-safe, called from GUI thread)
    // ========================================================================
    
    /**
     * @brief Start the worker thread
     * 
     * Must be called before any other operations.
     */
    void start();
    
    /**
     * @brief Stop the worker thread and wait for completion
     * 
     * Blocks until worker thread exits cleanly.
     */
    void stop();
    
    /**
     * @brief Update the DAG being rendered
     * 
     * This invalidates all caches and recreates renderers.
     * 
     * @param dag New DAG to use
     */
    void updateDAG(std::shared_ptr<const orc::DAG> dag);
    
    /**
     * @brief Set the project for rendering
     * 
     * Must be called before updateDAG to initialize the presenter.
     * 
     * @param project Project pointer (must outlive RenderCoordinator)
     */
    void setProject(orc::Project* project);
    
    /**
     * @brief Request a preview render (async)
     * 
     * Result will be emitted via previewReady signal.
     * 
     * @param node_id Node to render from
     * @param output_type Type of output (field/frame/etc)
     * @param output_index Which field/frame to render
     * @return Request ID for matching response
     */
    uint64_t requestPreview(const orc::NodeID& node_id,
                           orc::PreviewOutputType output_type,
                           uint64_t output_index,
                           const std::string& option_id = "");
    
    /**
     * @brief Request VBI data for a field (async)
     * 
     * Result will be emitted via vbiDataReady signal.
     * 
     * @param node_id Node to decode VBI from
     * @param field_id Field to decode
     * @return Request ID for matching response
     */
    uint64_t requestVBIData(const orc::NodeID& node_id, orc::FieldID field_id);
    
    /**
     * @brief Request dropout analysis data for all fields (async)
     * 
     * Result will be emitted via dropoutDataReady signal.
     * 
     * @param node_id Node to analyze dropout from
     * @param mode Analysis mode (full field or visible area)
     * @return Request ID for matching response
     */
    uint64_t requestDropoutData(const orc::NodeID& node_id, orc::DropoutAnalysisMode mode);
    
    /**
     * @brief Request SNR analysis data for all fields (async)
     * 
     * Result will be emitted via snrDataReady signal.
     * 
     * @param node_id Node to analyze SNR from
     * @param mode Analysis mode (white, black, or both)
     * @return Request ID for matching response
     */
    uint64_t requestSNRData(const orc::NodeID& node_id, orc::SNRAnalysisMode mode);
    
    /**
     * @brief Request burst level analysis data for all fields (async)
     * 
     * Result will be emitted via burstLevelDataReady signal.
     * 
     * @param node_id Node to analyze burst level from
     * @return Request ID for matching response
     */
    uint64_t requestBurstLevelData(const orc::NodeID& node_id);
    
    /**
     * @brief Request available outputs for a node (async)
     * 
     * Result will be emitted via availableOutputsReady signal.
     * 
     * @param node_id Node to query
     * @return Request ID for matching response
     */
    uint64_t requestAvailableOutputs(const orc::NodeID& node_id);
    
    /**
     * @brief Request line samples from a field (async)
     * 
     * Result will be emitted via lineSamplesReady signal.
     * 
     * @param node_id Node to get samples from
     * @param output_type Type of output (field/frame)
     * @param output_index Which field/frame
     * @param line_number Line number to retrieve (0-based)
     * @param sample_x Sample X position that was clicked (in preview image coordinates)
     * @param preview_image_width Width of the preview image for coordinate mapping
     * @return Request ID for matching response
     */
    uint64_t requestLineSamples(const orc::NodeID& node_id,
                               orc::PreviewOutputType output_type,
                               uint64_t output_index,
                               int line_number,
                               int sample_x,
                               int preview_image_width);
    
    /**
     * @brief Request frame line navigation (async)
     * 
     * Requests the core to calculate the next/previous line when navigating
     * in frame mode with interlaced fields. Handles complex field ordering.
     * 
     * Result emitted via frameLineNavigationReady signal.
     * 
     * @param node_id The node being displayed
     * @param output_type The current output type (Frame or Frame_Reversed)
     * @param current_field The current field index
     * @param current_line The current line within the field
     * @param direction +1 to go down, -1 to go up
     * @param field_height The height of a single field in lines
     * @return Request ID for matching response
     */
    uint64_t requestFrameLineNavigation(const orc::NodeID& node_id,
                                       orc::PreviewOutputType output_type,
                                       uint64_t current_field,
                                       int current_line,
                                       int direction,
                                       int field_height);
    
    /**
     * @brief Map preview image coordinates to field coordinates (synchronous)
     * 
     * This is a synchronous call that returns immediately with the mapping.
     * No async request needed since it's just a calculation.
     * 
     * @param node_id The node being displayed
     * @param output_type The output type (Field, Frame, etc.)
     * @param output_index The output index (0-based)
     * @param image_y Y coordinate in the preview image
     * @param image_height Total height of the image (for split mode)
     * @return Mapping result (field_index, field_line)
     */
    orc::ImageToFieldMappingResult mapImageToField(const orc::NodeID& node_id,
                                                   orc::PreviewOutputType output_type,
                                                   uint64_t output_index,
                                                   int image_y,
                                                   int image_height);
    
    /**
     * @brief Map field coordinates back to preview image coordinates (synchronous)
     * 
     * This is a synchronous call that returns immediately with the mapping.
     * No async request needed since it's just a calculation.
     * 
     * @param node_id The node being displayed
     * @param output_type The output type (Field, Frame, etc.)
     * @param output_index The output index (0-based)
     * @param field_index The field index
     * @param field_line The line within the field
     * @param image_height Total height of the image (for split mode)
     * @return Mapping result (image_y)
     */
    orc::FieldToImageMappingResult mapFieldToImage(const orc::NodeID& node_id,
                                                   orc::PreviewOutputType output_type,
                                                   uint64_t output_index,
                                                   uint64_t field_index,
                                                   int field_line,
                                                   int image_height);
    
    /**
     * @brief Get the field indices that make up a frame (synchronous)
     * 
     * Returns which two fields comprise the given frame, accounting for field ordering.
     * 
     * @param node_id The node being displayed
     * @param frame_index The frame index (0-based)
     * @return Result with first_field and second_field indices
     */
    orc::FrameFieldsResult getFrameFields(const orc::NodeID& node_id, uint64_t frame_index);
    
    uint64_t requestSavePNG(const orc::NodeID& node_id, 
                           orc::PreviewOutputType output_type,
                           uint64_t output_index,
                           const std::string& filename,
                           const std::string& option_id = "");

    /**
     * @brief Trigger a stage for batch processing (async)
     * 
     * Progress updates emitted via triggerProgress signal.
     * Completion emitted via triggerComplete signal.
     * 
     * @param node_id Node to trigger
     * @return Request ID for matching response
     */
    uint64_t requestTrigger(const orc::NodeID& node_id);
    
    /**
     * @brief Cancel ongoing trigger operation
     */
    void cancelTrigger();
    
    /**
     * @brief Set aspect ratio mode for rendering
     * 
     * This affects how images are scaled in render_output and save_png.
     * Thread-safe - can be called from GUI thread.
     * 
     * @param mode The aspect ratio mode to use
     */
    void setAspectRatioMode(orc::AspectRatioMode mode);
    
    /**
     * @brief Set whether to render dropout regions onto images
     * 
     * Thread-safe - can be called from GUI thread.
     * 
     * @param show True to render dropouts, false to hide
     */
    void setShowDropouts(bool show);
    
signals:
    /**
     * @brief Emitted when a preview render completes
     * 
     * @param request_id The request ID from requestPreview()
     * @param result The render result
     */
    void previewReady(uint64_t request_id, orc::public_api::PreviewRenderResult result);
    
    /**
     * @brief Emitted when VBI data is ready
     */
    void vbiDataReady(uint64_t request_id, orc::presenters::VBIFieldInfoView info);
    
    /**
     * @brief Emitted when dropout analysis data is ready
     */
    void dropoutDataReady(uint64_t request_id, std::vector<orc::FrameDropoutStats> frame_stats, int32_t total_frames);
    
    /**
     * @brief Emitted during dropout analysis progress
     */
    void dropoutProgress(size_t current, size_t total, QString message);
    
    /**
     * @brief Emitted when SNR analysis data is ready
     */
    void snrDataReady(uint64_t request_id, std::vector<orc::FrameSNRStats> frame_stats, int32_t total_frames);
    
    /**
     * @brief Emitted during SNR analysis progress
     */
    void snrProgress(size_t current, size_t total, QString message);
    
    /**
     * @brief Emitted when burst level analysis data is ready
     */
    void burstLevelDataReady(uint64_t request_id, std::vector<orc::FrameBurstLevelStats> frame_stats, int32_t total_frames);
    
    /**
     * @brief Emitted during burst level analysis progress
     */
    void burstLevelProgress(size_t current, size_t total, QString message);
    
    /**
     * @brief Emitted when available outputs query completes
     */
    void availableOutputsReady(uint64_t request_id, std::vector<orc::PreviewOutputInfo> outputs);
    
    /**
     * @brief Emitted when line samples are ready
     */
    void lineSamplesReady(uint64_t request_id, uint64_t field_index, int line_number, int sample_x, 
                          std::vector<uint16_t> samples, std::optional<orc::VideoParameters> video_params,
                          std::vector<uint16_t> y_samples, std::vector<uint16_t> c_samples);
    
    /**
     * @brief Emitted during trigger progress
     */
    void triggerProgress(size_t current, size_t total, QString message);
    
    /**
     * @brief Emitted when trigger completes
     */
    void triggerComplete(uint64_t request_id, bool success, QString status);
    
    /**
     * @brief Emitted when frame line navigation result is ready
     */
    void frameLineNavigationReady(uint64_t request_id, orc::FrameLineNavigationResult result);
    
    /**
     * @brief Emitted on any error
     */
    void error(uint64_t request_id, QString message);
    
private:
    // ========================================================================
    // Worker thread methods (run on worker thread only)
    // ========================================================================
    
    /**
     * @brief Main worker thread loop
     */
    void workerLoop();
    
    /**
     * @brief Process a single request
     */
    void processRequest(std::unique_ptr<RenderRequest> request);
    
    /**
     * @brief Handle UpdateDAG request
     */
    void handleUpdateDAG(const UpdateDAGRequest& req);
    
    /**
     * @brief Handle RenderPreview request
     */
    void handleRenderPreview(const RenderPreviewRequest& req);
    
    /**
     * @brief Handle GetVBIData request
     */
    void handleGetVBIData(const GetVBIDataRequest& req);
    
    /**
     * @brief Handle GetDropoutData request
     */
    void handleGetDropoutData(const GetDropoutDataRequest& req);
    
    /**
     * @brief Handle GetSNRData request
     */
    void handleGetSNRData(const GetSNRDataRequest& req);
    
    /**
     * @brief Handle GetBurstLevelData request
     */
    void handleGetBurstLevelData(const GetBurstLevelDataRequest& req);
    
    /**
     * @brief Handle GetAvailableOutputs request
     */
    void handleGetAvailableOutputs(const GetAvailableOutputsRequest& req);
    
    /**
     * @brief Handle GetLineSamples request
     */
    void handleGetLineSamples(const GetLineSamplesRequest& req);
    
    /**
     * @brief Handle NavigateFrameLine request
     */
    void handleNavigateFrameLine(const NavigateFrameLineRequest& req);
    
    void handleSavePNG(const SavePNGRequest& req);
    
    /**
     * @brief Handle TriggerStage request
     */
    void handleTriggerStage(const TriggerStageRequest& req);
    
    /**
     * @brief Enqueue a request (thread-safe)
     */
    void enqueueRequest(std::unique_ptr<RenderRequest> request);
    
    /**
     * @brief Get next request ID (thread-safe)
     */
    uint64_t nextRequestId();
    
    // ========================================================================
    // Thread synchronization
    // ========================================================================
    
    std::thread worker_thread_;
    std::atomic<bool> shutdown_requested_{false};
    
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<std::unique_ptr<RenderRequest>> request_queue_;
    
    std::atomic<uint64_t> next_request_id_{1};
    
    // ========================================================================
    // Worker thread state (owned by worker thread, never accessed from GUI)
    // ========================================================================
    
    std::shared_ptr<const orc::DAG> worker_dag_;
    std::unique_ptr<orc::presenters::RenderPresenter> worker_render_presenter_;
    orc::Project* worker_project_{nullptr};  // Non-owning pointer for presenter
    
    std::atomic<bool> trigger_cancel_requested_{false};
    orc::TriggerableStage* current_trigger_stage_{nullptr};  // Pointer to currently executing trigger (if any)
};

#endif // RENDER_COORDINATOR_H
