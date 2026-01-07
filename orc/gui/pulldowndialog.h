/*
 * File:        pulldowndialog.h
 * Module:      orc-gui
 * Purpose:     Pulldown observation display dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef PULLDOWNDIALOG_H
#define PULLDOWNDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QGroupBox>
#include <memory>
#include "../core/observers/pulldown_observer.h"

/**
 * @brief Dialog for displaying pulldown observation information
 * 
 * This dialog shows pulldown detection data for the current field being viewed.
 * It displays:
 * - Whether the field is detected as a pulldown frame
 * - Detection confidence level
 * - Pattern position within the 5-frame 3:2 cycle
 * - Pattern break detection
 * - Phase analysis results
 * - VBI pattern analysis results
 */
class PulldownDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PulldownDialog(QWidget *parent = nullptr);
    ~PulldownDialog();
    
    /**
     * @brief Update the displayed pulldown observation information
     * @param observation The pulldown observation to display
     */
    void updatePulldownObservation(const std::shared_ptr<orc::PulldownObservation>& observation);
    
    /**
     * @brief Clear the displayed pulldown information
     */
    void clearPulldownInfo();

private:
    void setupUI();
    QString formatConfidence(orc::ConfidenceLevel level);
    QString formatDetectionBasis(orc::DetectionBasis basis);
    
    // UI components - Detection result
    QLabel* field_id_label_;
    QLabel* is_pulldown_label_;
    QLabel* confidence_label_;
    QLabel* detection_basis_label_;
    
    // UI components - Pattern information
    QLabel* pattern_position_label_;
    QLabel* pattern_break_label_;
    
    // UI components - Diagnostic information
    QLabel* phase_analysis_label_;
    QLabel* vbi_pattern_label_;
};

#endif // PULLDOWNDIALOG_H
