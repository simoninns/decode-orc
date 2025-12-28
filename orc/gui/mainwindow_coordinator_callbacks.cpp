/*
 * File:        mainwindow_coordinator_callbacks.cpp
 * Module:      orc-gui
 * Purpose:     RenderCoordinator callback implementations for MainWindow
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "mainwindow.h"
#include "logging.h"
#include <QMessageBox>
#include <QStatusBar>
#include "vbidialog.h"
#include "previewdialog.h"
#include "fieldpreviewwidget.h"
#include <algorithm>


// Coordinator response slot implementations

void MainWindow::onPreviewReady(uint64_t request_id, orc::PreviewRenderResult result)
{
    // Ignore stale responses
    if (request_id != pending_preview_request_id_) {
        ORC_LOG_DEBUG("Ignoring stale preview response (id {} != {})", request_id, pending_preview_request_id_);
        return;
    }
    
    ORC_LOG_DEBUG("onPreviewReady: request_id={}, success={}", request_id, result.success);
    
    if (result.success) {
        preview_dialog_->previewWidget()->setImage(result.image);
        updateVectorscope(current_view_node_id_, result.image);
    } else {
        preview_dialog_->previewWidget()->clearImage();
        statusBar()->showMessage(
            QString("Render ERROR at node %1: %2")
                .arg(QString::fromStdString(current_view_node_id_))
                .arg(QString::fromStdString(result.error_message)),
            5000
        );
    }
}

void MainWindow::onVBIDataReady(uint64_t request_id, orc::VBIFieldInfo info)
{
    if (request_id != pending_vbi_request_id_) {
        return;
    }
    
    ORC_LOG_DEBUG("onVBIDataReady: request_id={}", request_id);
    
    if (vbi_dialog_ && vbi_dialog_->isVisible()) {
        vbi_dialog_->updateVBIInfo(info);
    }
}

void MainWindow::onAvailableOutputsReady(uint64_t request_id, std::vector<orc::PreviewOutputInfo> outputs)
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
    bool is_real_node = (current_view_node_id_ != "_no_preview");
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
    // Get node label from project (prefer user_label, fallback to display_name)
    const auto& nodes = project_.coreProject().get_nodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
        [this](const orc::ProjectDAGNode& n) { return n.node_id == current_view_node_id_; });
    QString node_label;
    if (node_it != nodes.end()) {
        if (!node_it->user_label.empty()) {
            node_label = QString::fromStdString(node_it->user_label);
        } else if (!node_it->display_name.empty()) {
            node_label = QString::fromStdString(node_it->display_name);
        } else {
            node_label = QString::fromStdString(current_view_node_id_);
        }
    } else {
        node_label = QString::fromStdString(current_view_node_id_);
    }
    preview_dialog_->setCurrentNode(node_label, QString::fromStdString(current_view_node_id_));
    
    // Update status bar to show which node is being viewed
    QString node_display = QString::fromStdString(current_view_node_id_);
    statusBar()->showMessage(QString("Viewing output from node: %1").arg(node_display), 5000);
    
    // Update UI controls
    updatePreviewModeCombo();
    refreshViewerControls();
    updateUIState();
    
    // Request initial preview
    updatePreview();
}

void MainWindow::onTriggerProgress(size_t current, size_t total, QString message)
{
    if (trigger_progress_dialog_ && total > 0) {
        int percentage = static_cast<int>((current * 100) / total);
        trigger_progress_dialog_->setValue(percentage);
        trigger_progress_dialog_->setLabelText(message);
    }
}

void MainWindow::onTriggerComplete(uint64_t request_id, bool success, QString status)
{
    if (request_id != pending_trigger_request_id_) {
        return;
    }
    
    ORC_LOG_INFO("onTriggerComplete: success={}, status={}", success, status.toStdString());
    
    // Close progress dialog
    if (trigger_progress_dialog_) {
        delete trigger_progress_dialog_;
        trigger_progress_dialog_ = nullptr;
    }
    
    // Show result
    if (success) {
        statusBar()->showMessage(status, 5000);
    } else {
        QMessageBox::warning(this, "Trigger Failed", status);
    }
}

void MainWindow::onCoordinatorError(uint64_t request_id, QString message)
{
    ORC_LOG_ERROR("Coordinator error (request {}): {}", request_id, message.toStdString());
    
    // Show error in status bar
    statusBar()->showMessage(QString("Error: %1").arg(message), 5000);
}
