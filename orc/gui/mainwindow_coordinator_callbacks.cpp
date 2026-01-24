/*
 * File:        mainwindow_coordinator_callbacks.cpp
 * Module:      orc-gui
 * Purpose:     RenderCoordinator callback implementations for MainWindow
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "mainwindow.h"
#include "logging.h"
#include <QMessageBox>
#include <QStatusBar>
#include "vbidialog.h"
#include "previewdialog.h"
#include "fieldpreviewwidget.h"
#include "dropoutanalysisdialog.h"
#include "snranalysisdialog.h"
#include "burstlevelanalysisdialog.h"
#include <algorithm>
#include <limits>
#include "presenters/include/vbi_view_models.h"
#include "presenters/include/vbi_presenter.h"


// Coordinator response slot implementations

void MainWindow::onPreviewReady(uint64_t request_id, orc::public_api::PreviewRenderResult result)
{
    // Ignore stale responses
    if (request_id != pending_preview_request_id_) {
        ORC_LOG_DEBUG("Ignoring stale preview response (id {} != {})", request_id, pending_preview_request_id_);
        return;
    }
    
    ORC_LOG_DEBUG("onPreviewReady: request_id={}, success={}", request_id, result.success);
    
    if (result.success) {
        // Use public API image directly - no conversion needed
        preview_dialog_->previewWidget()->setImage(result.image);
        updateVectorscope(result);
    } else {
        preview_dialog_->previewWidget()->clearImage();
        statusBar()->showMessage(
            QString("Render ERROR at stage %1: %2")
                .arg(QString::fromStdString(current_view_node_id_.to_string()))
                .arg(QString::fromStdString(result.error_message)),
            5000
        );
    }
    
    // Get the index we just rendered
    int rendered_index = preview_dialog_->previewSlider()->value();
    
    // Mark that the render is complete
    preview_render_in_flight_ = false;
    
    // Check if the latest requested index differs from what we just rendered
    // If so, send a new request for the latest cached index
    if (latest_requested_preview_index_ >= 0 && latest_requested_preview_index_ != rendered_index) {
        ORC_LOG_DEBUG("Render queue clear - processing latest cached request: {} (was rendering {})", 
                      latest_requested_preview_index_, rendered_index);
        
        // Update slider to the latest requested position
        preview_dialog_->previewSlider()->blockSignals(true);
        preview_dialog_->previewSlider()->setValue(latest_requested_preview_index_);
        preview_dialog_->previewSlider()->blockSignals(false);
        
        // Send request for the latest cached index
        pending_preview_index_ = latest_requested_preview_index_;
        updatePreview();  // Call updatePreview directly to ensure request is sent
    }
}

void MainWindow::onVBIDataReady(uint64_t request_id, orc::presenters::VBIFieldInfoView info)
{
    if (request_id != pending_vbi_request_id_ && request_id != pending_vbi_request_id_field2_) {
        return;
    }
    
    ORC_LOG_DEBUG("onVBIDataReady: request_id={}", request_id);
    
    if (!vbi_dialog_ || !vbi_presenter_) {
        return;
    }
    
    // Process VBI data whether or not the dialog is currently visible,
    // so that when it is shown, it has the latest data
    
    if (pending_vbi_is_frame_mode_) {
        // Frame mode - need both fields
        if (request_id == pending_vbi_request_id_) {
            // First field received - cache it
            pending_vbi_field1_info_ = info;
            pending_vbi_request_id_ = 0;  // Mark first request as processed
        } else if (request_id == pending_vbi_request_id_field2_) {
            // Second field received - update dialog with both fields
            if (vbi_dialog_->isVisible()) {
                vbi_dialog_->updateVBIInfoFrame(pending_vbi_field1_info_, info);
            }
            pending_vbi_is_frame_mode_ = false;
            pending_vbi_request_id_field2_ = 0;
            pending_vbi_request_id_ = 0;
        }
    } else {
        // Field mode - single field display
        if (vbi_dialog_->isVisible()) {
            vbi_dialog_->updateVBIInfo(info);
        }
        pending_vbi_request_id_ = 0;
    }
}

void MainWindow::onAvailableOutputsReady(uint64_t request_id, std::vector<orc::public_api::PreviewOutputInfo> outputs)
{
    if (request_id != pending_outputs_request_id_) {
        return;
    }
    
    ORC_LOG_DEBUG("onAvailableOutputsReady: request_id={}, count={}", request_id, outputs.size());
    
    available_outputs_ = std::move(outputs);
    
    // Try to preserve current option_id across node switches
    bool found_match = false;
    for (const auto& output : available_outputs_) {
        if (output.option_id == current_option_id_) {
            current_output_type_ = output.type;
            found_match = true;
            ORC_LOG_DEBUG("Preserved option_id '{}', output_type={}", 
                          current_option_id_, static_cast<int>(current_output_type_));
            break;
        }
    }
    
    // If current option not available, try to find a sensible default
    if (!found_match && !available_outputs_.empty()) {
        // Prefer "frame" (Frame (Y)) if available, otherwise use first output
        bool found_frame = false;
        for (const auto& output : available_outputs_) {
            if (output.option_id == "frame") {
                current_output_type_ = output.type;
                current_option_id_ = output.option_id;
                found_frame = true;
                break;
            }
        }
        if (!found_frame) {
            current_output_type_ = available_outputs_[0].type;
            current_option_id_ = available_outputs_[0].option_id;
        }
    }
    
    // Check if we should show preview dialog
    bool is_real_node = current_view_node_id_.is_valid();
    bool has_valid_content = false;
    for (const auto& output : available_outputs_) {
        if (output.is_available) {
            has_valid_content = true;
            break;
        }
    }
    
    bool auto_show_enabled = auto_show_preview_action_ && auto_show_preview_action_->isChecked();
    
    // Enable the Show Preview menu action whenever there's valid content
    if (is_real_node && has_valid_content) {
        show_preview_action_->setEnabled(true);
    }
    
    // Auto-show the preview dialog only if the setting is enabled
    if (!preview_dialog_->isVisible() && is_real_node && has_valid_content && auto_show_enabled) {
        preview_dialog_->show();
    }
    
    // Update preview dialog to show current node
    // Get node label from project (prefer label, fallback to stage_name)
    const auto nodes = project_.presenter()->getNodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
        [this](const orc::presenters::NodeInfo& n) { return n.node_id == current_view_node_id_; });
    QString node_label;
    if (node_it != nodes.end()) {
        if (!node_it->label.empty()) {
            node_label = QString::fromStdString(node_it->label);
        } else if (!node_it->stage_name.empty()) {
            node_label = QString::fromStdString(node_it->stage_name);
        } else {
            node_label = QString::fromStdString(current_view_node_id_.to_string());
        }
    } else {
        node_label = QString::fromStdString(current_view_node_id_.to_string());
    }
    preview_dialog_->setCurrentNode(node_label, QString::fromStdString(current_view_node_id_.to_string()));
    
    // Update status bar to show which stage is being viewed
    QString node_display = QString::fromStdString(current_view_node_id_.to_string());
    statusBar()->showMessage(QString("Viewing output from stage: %1").arg(node_display), 5000);
    
    // Update UI controls
    updatePreviewModeCombo();
    refreshViewerControls();
    updateUIState();
    
    // Update dropouts button state based on current output's availability
    // Find the current output info to check if dropouts are available
    bool dropouts_available = false;
    for (const auto& output : available_outputs_) {
        if (output.option_id == current_option_id_ && output.type == current_output_type_) {
            dropouts_available = output.dropouts_available;
            break;
        }
    }
    
    // Update dropouts button - disable and turn off if not available
    if (preview_dialog_ && preview_dialog_->dropoutsButton()) {
        if (!dropouts_available) {
            // Disable and turn off dropouts for stages where they're not available (e.g., chroma decoder)
            preview_dialog_->dropoutsButton()->setEnabled(false);
            preview_dialog_->dropoutsButton()->setChecked(false);
            render_coordinator_->setShowDropouts(false);
        } else {
            // Re-enable dropouts button for stages that support it
            preview_dialog_->dropoutsButton()->setEnabled(true);
        }
    }
    
    // Request initial preview
    updatePreview();
}

void MainWindow::onTriggerProgress(size_t current, size_t total, QString message)
{
    // Ignore progress updates if we're not waiting for a trigger
    // This prevents race conditions where progress arrives after completion
    if (pending_trigger_request_id_ == 0) {
        return;
    }
    
    // Store pointer locally and check validity before each use
    // This protects against the dialog being deleted mid-function
    QPointer<QProgressDialog> dialog = trigger_progress_dialog_;
    if (!dialog || total == 0) {
        return;
    }
    
    int percentage = static_cast<int>((current * 100) / total);
    
    // Re-check before each call in case dialog was deleted
    if (dialog) {
        dialog->setValue(percentage);
    }
    if (dialog) {
        dialog->setLabelText(message);
    }
}

void MainWindow::onTriggerComplete(uint64_t request_id, bool success, QString status)
{
    if (request_id != pending_trigger_request_id_) {
        return;
    }
    
    ORC_LOG_DEBUG("onTriggerComplete: success={}, status={}", success, status.toStdString());
    
    // CRITICAL: Clear pending request ID FIRST to stop any racing progress updates
    // This ensures onTriggerProgress will ignore any queued signals
    pending_trigger_request_id_ = 0;
    
    // Close and delete progress dialog immediately (not deleteLater)
    // Using delete instead of deleteLater ensures the object is truly gone
    // and QPointer will be nulled immediately
    if (trigger_progress_dialog_) {
        trigger_progress_dialog_->hide();
        delete trigger_progress_dialog_;  // Immediate deletion, not deferred
        // QPointer automatically becomes null after delete
    }
    
    // If trigger was successful, automatically create dialog and request analysis data for display
    if (success && pending_trigger_node_id_.is_valid()) {
        // Determine which type of analysis sink was triggered by checking stage_name
        const auto nodes = project_.presenter()->getNodes();
        auto node_it = std::find_if(nodes.begin(), nodes.end(),
            [this](const orc::presenters::NodeInfo& n) { return n.node_id == pending_trigger_node_id_; });
        
        if (node_it != nodes.end()) {
            const std::string& stage_name = node_it->stage_name;
            
            // Create and show analysis dialog
            createAndShowAnalysisDialog(pending_trigger_node_id_, stage_name);
            
            // Request the appropriate data based on sink type
            uint64_t data_request_id;
            if (stage_name == "burst_level_analysis_sink") {
                data_request_id = render_coordinator_->requestBurstLevelData(pending_trigger_node_id_);
                pending_burst_level_requests_[data_request_id] = pending_trigger_node_id_;
                ORC_LOG_DEBUG("Auto-requesting burst level data after trigger complete");
            }
            else if (stage_name == "dropout_analysis_sink") {
                data_request_id = render_coordinator_->requestDropoutData(pending_trigger_node_id_, orc::DropoutAnalysisMode::FULL_FIELD);
                pending_dropout_requests_[data_request_id] = pending_trigger_node_id_;
                ORC_LOG_DEBUG("Auto-requesting dropout analysis data after trigger complete");
            }
            else if (stage_name == "snr_analysis_sink") {
                data_request_id = render_coordinator_->requestSNRData(pending_trigger_node_id_, orc::SNRAnalysisMode::BOTH);
                pending_snr_requests_[data_request_id] = pending_trigger_node_id_;
                ORC_LOG_DEBUG("Auto-requesting SNR analysis data after trigger complete");
            }
        }
    }
    
    // Show result
    if (success) {
        statusBar()->showMessage(status, 5000);
    } else {
        QMessageBox::warning(this, "Trigger Failed", status);
    }
    
    // Clear trigger state
    pending_trigger_node_id_ = orc::NodeID();
}

void MainWindow::onCoordinatorError(uint64_t request_id, QString message)
{
    ORC_LOG_ERROR("Coordinator error (request {}): {}", request_id, message.toStdString());
    
    // Check if this is a line sample request error
    if (request_id == pending_line_sample_request_id_) {
        pending_line_sample_request_id_ = 0;
        
        // Show empty line scope with appropriate message
        if (preview_dialog_ && preview_dialog_->isLineScopeVisible()) {
            ORC_LOG_DEBUG("Line samples not available for this stage, showing empty line scope");
            
            QString node_id_str = QString::fromStdString(current_view_node_id_.to_string());
            
            // Show empty line scope (no samples) - this will display "No data available for this line"
            preview_dialog_->showLineScope(node_id_str, 0, 0, 0, 
                                          std::vector<uint16_t>(),  // Empty samples
                                          std::nullopt, 0, 0, 0);
        }
        
        // Show brief message in status bar
        statusBar()->showMessage(QString("Line data not available for this stage"), 3000);
        return;
    }
    
    // Show error in status bar for other errors
    statusBar()->showMessage(QString("Error: %1").arg(message), 5000);
}

void MainWindow::onDropoutDataReady(uint64_t request_id, 
                                    std::vector<orc::FrameDropoutStats> frame_stats, 
                                    int32_t total_frames)
{
    // Find which node this request was for
    auto req_it = pending_dropout_requests_.find(request_id);
    if (req_it == pending_dropout_requests_.end()) {
        ORC_LOG_DEBUG("Ignoring stale dropout data response (unknown request_id {})", request_id);
        return;
    }
    
    orc::NodeID node_id = req_it->second;
    pending_dropout_requests_.erase(req_it);
    
    ORC_LOG_DEBUG("onDropoutDataReady for node '{}': {} frames, total={}", 
                  node_id.to_string(), frame_stats.size(), total_frames);
    
    // Close progress dialog for this stage
    auto prog_it = dropout_progress_dialogs_.find(node_id);
    if (prog_it != dropout_progress_dialogs_.end() && prog_it->second) {
        prog_it->second->close();
        prog_it->second->deleteLater();
    }
    
    // Find the dialog for this stage
    auto dialog_it = dropout_analysis_dialogs_.find(node_id);
    if (dialog_it == dropout_analysis_dialogs_.end() || !dialog_it->second || !dialog_it->second->isVisible()) {
        return;
    }
    
    auto* dialog = dialog_it->second;
    
    // If no data available, show message
    if (frame_stats.empty() || total_frames == 0) {
        dialog->showNoDataMessage(
            "No dropout analysis data available.\n\n"
            "Make sure dropout detection is enabled in the pipeline."
        );
        return;
    }
    
    // Start update cycle
    dialog->startUpdate(total_frames);
    
    // Add all data points
    for (const auto& stats : frame_stats) {
        if (stats.has_data) {
            dialog->addDataPoint(stats.frame_number, stats.total_dropout_length);
        }
    }
    
    // Finish update with current frame marker
    int32_t current_frame = 1;  // Default to first frame
    if (preview_dialog_ && preview_dialog_->previewSlider()) {
        current_frame = static_cast<int32_t>(preview_dialog_->previewSlider()->value()) + 1;
    }
    
    dialog->finishUpdate(current_frame);
}

void MainWindow::onSNRDataReady(uint64_t request_id,
                               std::vector<orc::FrameSNRStats> frame_stats,
                               int32_t total_frames)
{
    // Find which node this request was for
    auto req_it = pending_snr_requests_.find(request_id);
    if (req_it == pending_snr_requests_.end()) {
        ORC_LOG_DEBUG("Ignoring stale SNR data response (unknown request_id {})", request_id);
        return;
    }
    
    orc::NodeID node_id = req_it->second;
    pending_snr_requests_.erase(req_it);
    
    ORC_LOG_DEBUG("onSNRDataReady for node '{}': {} frames, total={}", 
                  node_id.to_string(), frame_stats.size(), total_frames);
    
    // Close progress dialog for this stage
    auto prog_it = snr_progress_dialogs_.find(node_id);
    if (prog_it != snr_progress_dialogs_.end() && prog_it->second) {
        prog_it->second->close();
        prog_it->second->deleteLater();
    }
    
    // Find the dialog for this stage
    auto dialog_it = snr_analysis_dialogs_.find(node_id);
    if (dialog_it == snr_analysis_dialogs_.end() || !dialog_it->second || !dialog_it->second->isVisible()) {
        return;
    }
    
    auto* dialog = dialog_it->second;
    
    // If no data available, show message
    if (frame_stats.empty() || total_frames == 0) {
        dialog->showNoDataMessage(
            "No SNR analysis data available.\n\n"
            "Make sure VITS (Vertical Interval Test Signal) is present in the source."
        );
        return;
    }
    
    // Start update cycle
    dialog->startUpdate(total_frames);
    
    // Add all data points
    for (const auto& stats : frame_stats) {
        if (stats.has_data) {
            double white_snr = stats.has_white_snr ? stats.white_snr : std::numeric_limits<double>::quiet_NaN();
            double black_psnr = stats.has_black_psnr ? stats.black_psnr : std::numeric_limits<double>::quiet_NaN();
            dialog->addDataPoint(stats.frame_number, white_snr, black_psnr);
        }
    }
    
    // Finish update with current frame marker
    int32_t current_frame = 1;  // Default to first frame
    if (preview_dialog_ && preview_dialog_->previewSlider()) {
        current_frame = static_cast<int32_t>(preview_dialog_->previewSlider()->value()) + 1;
    }
    
    dialog->finishUpdate(current_frame);
}

void MainWindow::onDropoutProgress(size_t current, size_t total, QString message)
{
    // Update all active dropout progress dialogs
    for (auto& pair : dropout_progress_dialogs_) {
        if (pair.second) {
            pair.second->setMaximum(static_cast<int>(total));
            pair.second->setValue(static_cast<int>(current));
            pair.second->setLabelText(message);
        }
    }
}

void MainWindow::onSNRProgress(size_t current, size_t total, QString message)
{
    // Update all active SNR progress dialogs
    for (auto& pair : snr_progress_dialogs_) {
        if (pair.second) {
            pair.second->setMaximum(static_cast<int>(total));
            pair.second->setValue(static_cast<int>(current));
            pair.second->setLabelText(message);
        }
    }
}

void MainWindow::onBurstLevelDataReady(uint64_t request_id,
                                      std::vector<orc::FrameBurstLevelStats> frame_stats,
                                      int32_t total_frames)
{
    // Find which node this request was for
    auto req_it = pending_burst_level_requests_.find(request_id);
    if (req_it == pending_burst_level_requests_.end()) {
        ORC_LOG_DEBUG("Ignoring stale burst level data response (unknown request_id {})", request_id);
        return;
    }
    
    orc::NodeID node_id = req_it->second;
    pending_burst_level_requests_.erase(req_it);
    
    ORC_LOG_DEBUG("onBurstLevelDataReady for node '{}': {} frames, total={}", 
                  node_id.to_string(), frame_stats.size(), total_frames);
    
    // Close progress dialog for this stage
    auto prog_it = burst_level_progress_dialogs_.find(node_id);
    if (prog_it != burst_level_progress_dialogs_.end() && prog_it->second) {
        prog_it->second->close();
        prog_it->second->deleteLater();
    }
    
    // Find the dialog for this stage
    auto dialog_it = burst_level_analysis_dialogs_.find(node_id);
    if (dialog_it == burst_level_analysis_dialogs_.end() || !dialog_it->second || !dialog_it->second->isVisible()) {
        return;
    }
    
    auto* dialog = dialog_it->second;
    
    // If no data available, show message
    if (frame_stats.empty() || total_frames == 0) {
        dialog->showNoDataMessage(
            "No burst level data available.\n\n"
            "Color burst detection may have failed."
        );
        return;
    }
    
    // Start update cycle
    dialog->startUpdate(total_frames);
    
    // Add all data points
    for (const auto& stats : frame_stats) {
        if (stats.has_data) {
            dialog->addDataPoint(stats.frame_number, stats.median_burst_ire);
        }
    }
    
    // Finish update with current frame marker
    int32_t current_frame = 1;
    if (preview_dialog_ && preview_dialog_->previewSlider()) {
        current_frame = static_cast<int32_t>(preview_dialog_->previewSlider()->value()) + 1;
    }
    
    dialog->finishUpdate(current_frame);
}

void MainWindow::onBurstLevelProgress(size_t current, size_t total, QString message)
{
    // Update all active burst level progress dialogs
    for (auto& pair : burst_level_progress_dialogs_) {
        if (pair.second) {
            pair.second->setMaximum(static_cast<int>(total));
            pair.second->setValue(static_cast<int>(current));
            pair.second->setLabelText(message);
        }
    }
}
