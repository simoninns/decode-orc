/*
 * File:        hintsdialog.h
 * Module:      orc-gui
 * Purpose:     Video parameter hints display dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef HINTSDIALOG_H
#define HINTSDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QGroupBox>
#include <optional>
#include "presenters/include/hints_view_models.h"

/**
 * @brief Dialog for displaying video parameter hints
 * 
 * This dialog shows hint information for the current field being viewed,
 * displaying:
 * - Field parity hints (is_first_field)
 * - PAL phase hints (field_phase_id)
 * - Dropout hints
 * - Active line hints
 * 
 * Each hint displays its source (metadata, analysis, user override, etc.)
 * and confidence percentage.
 */
class HintsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HintsDialog(QWidget *parent = nullptr);
    ~HintsDialog();
    
    /**
     * @brief Update the displayed field parity hint
     * @param hint The field parity hint to display
     */
    void updateFieldParityHint(const std::optional<orc::presenters::FieldParityHintView>& hint);
    
    /**
     * @brief Update the displayed field phase hint for single field
     * @param hint The field phase hint to display
     */
    void updateFieldPhaseHint(const std::optional<orc::presenters::FieldPhaseHintView>& hint);
    
    /**
     * @brief Update the displayed field phase hint for frame/split view
     * @param first_field_hint The phase hint for the first field
     * @param second_field_hint The phase hint for the second field
     */
    void updateFieldPhaseHintForFrame(const std::optional<orc::presenters::FieldPhaseHintView>& first_field_hint,
                                      const std::optional<orc::presenters::FieldPhaseHintView>& second_field_hint);
    
    /**
     * @brief Update the displayed active line hint
     * @param hint The active line hint to display
     */
    void updateActiveLineHint(const std::optional<orc::presenters::ActiveLineHintView>& hint);
    
    /**
     * @brief Update the displayed video parameters
     * @param params The video parameters to display
     */
    void updateVideoParameters(const std::optional<orc::presenters::VideoParametersView>& params);
    
    /**
     * @brief Clear all displayed hint information
     */
    void clearHints();

private:
    void setupUI();
    QString formatHintSource(orc::presenters::HintSourceView source);
    
    // UI components - Field Parity
    QLabel* field_parity_value_label_;
    QLabel* field_parity_source_label_;
    QLabel* field_parity_confidence_label_;
    
    // UI components - Field Phase
    QLabel* field_phase_value_label_;
    QLabel* field_phase_source_label_;
    QLabel* field_phase_confidence_label_;
    
    // UI components - Active Line
    QLabel* active_line_value_label_;
    QLabel* active_line_source_label_;
    QLabel* active_line_confidence_label_;
    
    // UI components - Video Parameters
    QLabel* active_video_range_label_;
    QLabel* colour_burst_range_label_;
    QLabel* white_level_label_;
    QLabel* blanking_level_label_;
    QLabel* black_level_label_;
    QLabel* sample_rate_label_;
};

#endif // HINTSDIALOG_H
