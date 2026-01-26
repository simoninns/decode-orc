/*
 * File:        ntscobserverdialog.h
 * Module:      orc-gui
 * Purpose:     NTSC observer display dialog (FM code, white flag)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef NTSCOBSERVERDIALOG_H
#define NTSCOBSERVERDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QGroupBox>
#include <memory>
#include <field_id.h>
#include "presenters/include/ntsc_observation_view_models.h"

/**
 * @brief Dialog for displaying NTSC-specific observations
 * 
 * This dialog shows NTSC-specific observations for the current field(s) being viewed:
 * - FM Code (line 10): 20-bit data value and field flag
 * - White Flag (line 11): Presence indicator
 * 
 * Displays separate Field 1 and Field 2 sections when in frame/split view mode.
 */
class NtscObserverDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NtscObserverDialog(QWidget *parent = nullptr);
    ~NtscObserverDialog();
    
    /**
     * @brief Update the displayed observation information for a single field
     * @param field_id Field ID to display observations for
     * @param observations NTSC observations containing FM code and white flag data
     */
    void updateObservations(const orc::FieldID& field_id, const orc::presenters::NtscFieldObservationsView& observations);
    
    /**
     * @brief Update the displayed observation information for a frame (two fields)
     * @param field1_id First field ID
     * @param field1_observations NTSC observations for first field
     * @param field2_id Second field ID
     * @param field2_observations NTSC observations for second field
     */
    void updateObservationsForFrame(const orc::FieldID& field1_id, 
                                   const orc::presenters::NtscFieldObservationsView& field1_observations,
                                   const orc::FieldID& field2_id,
                                   const orc::presenters::NtscFieldObservationsView& field2_observations);
    
    /**
     * @brief Clear the displayed observation information
     */
    void clearObservations();

private:
    void setupUI();
    void updateFieldObservations(QGroupBox* field_group, const QString& field_label,
                                QLabel* fm_present, QLabel* fm_data, QLabel* fm_flag,
                                QLabel* white_flag,
                                const orc::presenters::NtscFieldObservationsView& observations);
    
    // UI components - Field 1
    QGroupBox* field1_group_;
    QLabel* field1_fm_code_present_label_;
    QLabel* field1_fm_code_data_label_;
    QLabel* field1_fm_code_field_flag_label_;
    QLabel* field1_white_flag_present_label_;
    
    // UI components - Field 2
    QGroupBox* field2_group_;
    QLabel* field2_fm_code_present_label_;
    QLabel* field2_fm_code_data_label_;
    QLabel* field2_fm_code_field_flag_label_;
    QLabel* field2_white_flag_present_label_;
    
    bool showing_frame_mode_;
};

#endif // NTSCOBSERVERDIALOG_H
