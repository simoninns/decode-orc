/*
 * File:        vbidialog.h
 * Module:      orc-gui
 * Purpose:     VBI information display dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef VBIDIALOG_H
#define VBIDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QGroupBox>
#include <QTabWidget>
#include <memory>
#include "../core/include/vbi_decoder.h"

/**
 * @brief Dialog for displaying decoded VBI information
 * 
 * This dialog shows VBI data for the current field being viewed,
 * similar to ld-analyse's VBI dialog. It displays:
 * - Raw VBI data (lines 16, 17, 18)
 * - Decoded frame/chapter numbers
 * - CLV timecode
 * - Programme status (original spec)
 * - Amendment 2 status
 * - Control codes (stop, lead-in, lead-out)
 */
class VBIDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VBIDialog(QWidget *parent = nullptr);
    ~VBIDialog();
    
    /**
     * @brief Update the displayed VBI information
     * @param vbi_info The VBI data to display
     */
    void updateVBIInfo(const orc::VBIFieldInfo& vbi_info);
    
    /**
     * @brief Update the displayed VBI information for a frame (both fields)
     * @param field1_info VBI data for first field
     * @param field2_info VBI data for second field
     */
    void updateVBIInfoFrame(const orc::VBIFieldInfo& field1_info, 
                            const orc::VBIFieldInfo& field2_info);
    
    /**
     * @brief Clear the displayed VBI information
     */
    void clearVBIInfo();

private:
    void setupUI();
    QString formatVBILine(int32_t vbi_value);
    QString formatSoundMode(orc::VbiSoundMode mode);
    
    // UI components - Raw VBI data
    QLabel* field_number_label_;
    QLabel* line16_label_;
    QLabel* line17_label_;
    QLabel* line18_label_;
    
    // Frame/timecode information
    QLabel* picture_number_label_;
    QLabel* clv_timecode_label_;
    QLabel* chapter_number_label_;
    QLabel* user_code_label_;
    
    // Control codes
    QLabel* stop_code_label_;
    QLabel* lead_in_label_;
    QLabel* lead_out_label_;
    
    // Programme status tabs
    QTabWidget* programme_status_tabs_;
    QWidget* original_spec_tab_;
    QWidget* amendment2_tab_;
    
    // Programme status (original spec) labels
    QLabel* cx_enabled_label_;
    QLabel* disc_size_label_;
    QLabel* disc_side_label_;
    QLabel* teletext_label_;
    QLabel* digital_label_;
    QLabel* sound_mode_label_;
    QLabel* fm_multiplex_label_;
    QLabel* programme_dump_label_;
    QLabel* parity_valid_label_;
    
    // Amendment 2 status labels
    QLabel* copy_permitted_label_;
    QLabel* video_standard_label_;
    QLabel* sound_mode_am2_label_;
};

#endif // VBIDIALOG_H
