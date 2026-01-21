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
#include "../core/include/field_id.h"
#include "../core/include/node_id.h"

namespace orc {
    class VideoFieldRepresentation;
    class ObservationContext;
    class DropoutAnalysisDecoder;
    class SNRAnalysisDecoder;
    class BurstLevelAnalysisDecoder;
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
     * @param obs_context Observation context with populated metrics
     */
    void updateMetricsFromContext(orc::FieldID field_id, const orc::ObservationContext& obs_context);
    
    /**
     * @brief Update the quality metrics display for a frame using observation context
     * @param field1_id First field ID
     * @param field2_id Second field ID
     * @param obs_context Observation context with populated metrics
     */
    void updateMetricsForFrameFromContext(orc::FieldID field1_id, orc::FieldID field2_id,
                                          const orc::ObservationContext& obs_context);
    
    /**
     * @brief Update the quality metrics display for a field
     * @param field_repr Field representation containing observations
     * @param field_id Field ID to extract metrics for
     */
    void updateMetrics(std::shared_ptr<const orc::VideoFieldRepresentation> field_repr, 
                      orc::FieldID field_id);
    
    /**
     * @brief Update the quality metrics display for a frame (two fields)
     * @param field1_repr First field representation
     * @param field1_id First field ID
     * @param field2_repr Second field representation  
     * @param field2_id Second field ID
     */
    void updateMetricsForFrame(std::shared_ptr<const orc::VideoFieldRepresentation> field1_repr,
                              orc::FieldID field1_id,
                              std::shared_ptr<const orc::VideoFieldRepresentation> field2_repr,
                              orc::FieldID field2_id);
    
    /**
     * @brief Clear all metrics (when no preview is available)
     */
    void clearMetrics();
    
    /**
     * @brief Set analysis decoders for metric extraction
     * @param node_id Current node ID being displayed
     * @param dropout_decoder Dropout analysis decoder
     * @param snr_decoder SNR analysis decoder
     * @param burst_level_decoder Burst level analysis decoder
     */
    void setAnalysisDecoders(orc::NodeID node_id,
                            orc::DropoutAnalysisDecoder* dropout_decoder,
                            orc::SNRAnalysisDecoder* snr_decoder,
                            orc::BurstLevelAnalysisDecoder* burst_level_decoder);

private:
    void setupUI();
    
    /**
     * @brief Extract metrics from a single field
     */
    struct FieldMetrics {
        double white_snr = 0.0;
        double black_psnr = 0.0;
        double burst_level = 0.0;
        double quality_score = 0.0;
        size_t dropout_count = 0;
        bool has_white_snr = false;
        bool has_black_psnr = false;
        bool has_burst_level = false;
        bool has_quality_score = false;
        bool has_dropout_count = false;
    };
    
    FieldMetrics extractFieldMetrics(std::shared_ptr<const orc::VideoFieldRepresentation> field_repr,
                                     orc::FieldID field_id);
    
    FieldMetrics extractMetricsFromContext(orc::FieldID field_id,
                                           const orc::ObservationContext& obs_context);
    
    void updateFieldLabels(const FieldMetrics& metrics, bool is_field1);
    void updateFrameAverageLabels(const FieldMetrics& field1, const FieldMetrics& field2);
    
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
    
    // Analysis decoders for real metric extraction
    orc::NodeID current_node_id_;
    orc::DropoutAnalysisDecoder* dropout_decoder_ = nullptr;
    orc::SNRAnalysisDecoder* snr_decoder_ = nullptr;
    orc::BurstLevelAnalysisDecoder* burst_level_decoder_ = nullptr;
};

#endif // QUALITYMETRICSDIALOG_H
