/*
 * File:        qualitymetricsdialog.h
 * Module:      orc-gui
 * Purpose:     Quality metrics dialog for displaying field/frame quality data
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef QUALITYMETRICSDIALOG_H
#define QUALITYMETRICSDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QGridLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <memory>
#include <field_id.h>
#include <metrics_presenter.h>

// Forward declarations - wrapped by presenter, not used directly
namespace orc {
    class ObservationContext;
}

/**
 * @brief Dialog for displaying quality metrics for the current field/frame
 * 
 * This dialog shows real-time quality metrics for the currently displayed
 * field or frame in the preview dialog, including:
 * - White SNR (from VITS)
 * - Black PSNR (from VITS)
 * - Burst level (median IRE)
 * - Disc quality score
 * - Dropout count
 * 
 * The dialog updates automatically when the preview changes to show metrics
 * for the current field/frame.
 * 
 * Metrics are extracted from the observation context via the MetricsPresenter,
 * which provides a clean MVP interface for data access.
 */
class QualityMetricsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QualityMetricsDialog(QWidget *parent = nullptr);
    ~QualityMetricsDialog();

    /**
     * @brief Update the quality metrics display for a field using observation context
     * @param field_id Field ID to extract metrics for
     * @param obs_context Observation context with populated metrics (wrapped by presenter)
     */
    void updateMetricsFromContext(orc::FieldID field_id, const orc::ObservationContext& obs_context);
    
    /**
     * @brief Update the quality metrics display for a frame using observation context
     * @param field1_id First field ID
     * @param field2_id Second field ID
     * @param obs_context Observation context with populated metrics (wrapped by presenter)
     */
    void updateMetricsForFrameFromContext(orc::FieldID field1_id, orc::FieldID field2_id,
                                          const orc::ObservationContext& obs_context);
    
    /**
     * @brief Update the quality metrics display for a field using pre-extracted metrics
     * @param field_id Field ID for display
     * @param metrics Pre-extracted quality metrics
     */
    void updateMetrics(orc::FieldID field_id, const orc::presenters::QualityMetrics& metrics);
    
    /**
     * @brief Update the quality metrics display for a frame using pre-extracted metrics
     * @param field1_id First field ID for display
     * @param field2_id Second field ID for display
     * @param metrics Combined/averaged quality metrics for the frame
     */
    void updateMetricsForFrame(orc::FieldID field1_id, orc::FieldID field2_id,
                                const orc::presenters::QualityMetrics& metrics);
    
    /**
     * @brief Clear all metrics (when no preview is available)
     */
    void clearMetrics();

private:
    void setupUI();
    
    void updateFieldLabels(const orc::presenters::QualityMetrics& metrics, bool is_field1);
    void updateFrameAverageLabels(const orc::presenters::QualityMetrics& field1, 
                                   const orc::presenters::QualityMetrics& field2);
    
    // UI components
    QGroupBox* field1_group_;
    QGroupBox* field2_group_;
    QGroupBox* frame_group_;
    
    // Field 1 labels
    QLabel* field1_white_snr_label_;
    QLabel* field1_black_psnr_label_;
    QLabel* field1_burst_level_label_;
    QLabel* field1_quality_score_label_;
    QLabel* field1_dropout_count_label_;
    
    // Field 2 labels
    QLabel* field2_white_snr_label_;
    QLabel* field2_black_psnr_label_;
    QLabel* field2_burst_level_label_;
    QLabel* field2_quality_score_label_;
    QLabel* field2_dropout_count_label_;
    
    // Frame average labels
    QLabel* frame_white_snr_label_;
    QLabel* frame_black_psnr_label_;
    QLabel* frame_burst_level_label_;
    QLabel* frame_quality_score_label_;
    QLabel* frame_dropout_count_label_;
    
    bool showing_frame_mode_;  // True if showing two fields, false if showing single field
};

#endif // QUALITYMETRICSDIALOG_H
