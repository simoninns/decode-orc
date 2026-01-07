/*
 * File:        vbidialog.cpp
 * Module:      orc-gui
 * Purpose:     VBI information display dialog implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "vbidialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFont>
#include <QFontDatabase>
#include <sstream>
#include <iomanip>

VBIDialog::VBIDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    setWindowTitle("VBI Decoder");
    
    // Use Qt::Window flag to allow independent positioning
    setWindowFlags(Qt::Window);
    
    // Don't destroy on close, just hide
    setAttribute(Qt::WA_DeleteOnClose, false);
    
    // Set default size
    resize(500, 600);
}

VBIDialog::~VBIDialog() = default;

void VBIDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    
    // Field information
    auto* fieldGroup = new QGroupBox("Field Information");
    auto* fieldLayout = new QGridLayout(fieldGroup);
    
    fieldLayout->addWidget(new QLabel("Field Number:"), 0, 0);
    field_number_label_ = new QLabel("-");
    fieldLayout->addWidget(field_number_label_, 0, 1);
    
    mainLayout->addWidget(fieldGroup);
    
    // Raw VBI data
    auto* rawGroup = new QGroupBox("Raw VBI Data");
    auto* rawLayout = new QGridLayout(rawGroup);
    
    // Use monospace font for VBI data
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    
    rawLayout->addWidget(new QLabel("Line 16:"), 0, 0);
    line16_label_ = new QLabel("------");
    line16_label_->setFont(monoFont);
    rawLayout->addWidget(line16_label_, 0, 1);
    
    rawLayout->addWidget(new QLabel("Line 17:"), 1, 0);
    line17_label_ = new QLabel("------");
    line17_label_->setFont(monoFont);
    rawLayout->addWidget(line17_label_, 1, 1);
    
    rawLayout->addWidget(new QLabel("Line 18:"), 2, 0);
    line18_label_ = new QLabel("------");
    line18_label_->setFont(monoFont);
    rawLayout->addWidget(line18_label_, 2, 1);
    
    mainLayout->addWidget(rawGroup);
    
    // Frame/Timecode information
    auto* timecodeGroup = new QGroupBox("Frame/Timecode Information");
    auto* timecodeLayout = new QGridLayout(timecodeGroup);
    
    timecodeLayout->addWidget(new QLabel("Picture Number:"), 0, 0);
    picture_number_label_ = new QLabel("-");
    timecodeLayout->addWidget(picture_number_label_, 0, 1);
    
    timecodeLayout->addWidget(new QLabel("CLV Timecode:"), 1, 0);
    clv_timecode_label_ = new QLabel("-");
    timecodeLayout->addWidget(clv_timecode_label_, 1, 1);
    
    timecodeLayout->addWidget(new QLabel("Chapter Number:"), 2, 0);
    chapter_number_label_ = new QLabel("-");
    timecodeLayout->addWidget(chapter_number_label_, 2, 1);
    
    timecodeLayout->addWidget(new QLabel("User Code:"), 3, 0);
    user_code_label_ = new QLabel("-");
    user_code_label_->setFont(monoFont);
    timecodeLayout->addWidget(user_code_label_, 3, 1);
    
    mainLayout->addWidget(timecodeGroup);
    
    // Control codes
    auto* controlGroup = new QGroupBox("Control Codes");
    auto* controlLayout = new QGridLayout(controlGroup);
    
    controlLayout->addWidget(new QLabel("Picture Stop:"), 0, 0);
    stop_code_label_ = new QLabel("-");
    controlLayout->addWidget(stop_code_label_, 0, 1);
    
    controlLayout->addWidget(new QLabel("Lead-In:"), 1, 0);
    lead_in_label_ = new QLabel("-");
    controlLayout->addWidget(lead_in_label_, 1, 1);
    
    controlLayout->addWidget(new QLabel("Lead-Out:"), 2, 0);
    lead_out_label_ = new QLabel("-");
    controlLayout->addWidget(lead_out_label_, 2, 1);
    
    mainLayout->addWidget(controlGroup);
    
    // Programme status tabs
    programme_status_tabs_ = new QTabWidget();
    
    // Original specification tab
    original_spec_tab_ = new QWidget();
    auto* progLayout = new QGridLayout(original_spec_tab_);
    
    progLayout->addWidget(new QLabel("CX Noise Reduction:"), 0, 0);
    cx_enabled_label_ = new QLabel("-");
    progLayout->addWidget(cx_enabled_label_, 0, 1);
    
    progLayout->addWidget(new QLabel("Disc Size:"), 1, 0);
    disc_size_label_ = new QLabel("-");
    progLayout->addWidget(disc_size_label_, 1, 1);
    
    progLayout->addWidget(new QLabel("Disc Side:"), 2, 0);
    disc_side_label_ = new QLabel("-");
    progLayout->addWidget(disc_side_label_, 2, 1);
    
    progLayout->addWidget(new QLabel("Teletext:"), 3, 0);
    teletext_label_ = new QLabel("-");
    progLayout->addWidget(teletext_label_, 3, 1);
    
    progLayout->addWidget(new QLabel("Digital/Analogue:"), 4, 0);
    digital_label_ = new QLabel("-");
    progLayout->addWidget(digital_label_, 4, 1);
    
    progLayout->addWidget(new QLabel("Sound Mode:"), 5, 0);
    sound_mode_label_ = new QLabel("-");
    progLayout->addWidget(sound_mode_label_, 5, 1);
    
    progLayout->addWidget(new QLabel("FM Multiplex:"), 6, 0);
    fm_multiplex_label_ = new QLabel("-");
    progLayout->addWidget(fm_multiplex_label_, 6, 1);
    
    progLayout->addWidget(new QLabel("Programme Dump:"), 7, 0);
    programme_dump_label_ = new QLabel("-");
    progLayout->addWidget(programme_dump_label_, 7, 1);
    
    progLayout->addWidget(new QLabel("Parity Valid:"), 8, 0);
    parity_valid_label_ = new QLabel("-");
    progLayout->addWidget(parity_valid_label_, 8, 1);
    
    progLayout->setRowStretch(9, 1);  // Push content to top
    
    programme_status_tabs_->addTab(original_spec_tab_, "Original Specification");
    
    // Amendment 2 tab
    amendment2_tab_ = new QWidget();
    auto* am2Layout = new QGridLayout(amendment2_tab_);
    
    am2Layout->addWidget(new QLabel("Copy Permitted:"), 0, 0);
    copy_permitted_label_ = new QLabel("-");
    am2Layout->addWidget(copy_permitted_label_, 0, 1);
    
    am2Layout->addWidget(new QLabel("Video Standard:"), 1, 0);
    video_standard_label_ = new QLabel("-");
    am2Layout->addWidget(video_standard_label_, 1, 1);
    
    am2Layout->addWidget(new QLabel("Sound Mode:"), 2, 0);
    sound_mode_am2_label_ = new QLabel("-");
    am2Layout->addWidget(sound_mode_am2_label_, 2, 1);
    
    am2Layout->setRowStretch(3, 1);  // Push content to top
    
    programme_status_tabs_->addTab(amendment2_tab_, "Amendment 2");
    
    mainLayout->addWidget(programme_status_tabs_);
    
    mainLayout->addStretch();
}

void VBIDialog::updateVBIInfo(const orc::VBIFieldInfo& vbi_info)
{
    if (!vbi_info.has_vbi_data) {
        // No valid VBI data - show N/A for field number too
        field_number_label_->setText("-");
        clearVBIInfo();
        return;
    }
    
    // Field number (0-indexed)
    field_number_label_->setText(QString::number(vbi_info.field_id.value()));
    
    // Raw VBI data
    line16_label_->setText(formatVBILine(vbi_info.vbi_data[0]));
    line17_label_->setText(formatVBILine(vbi_info.vbi_data[1]));
    line18_label_->setText(formatVBILine(vbi_info.vbi_data[2]));
    
    // Picture number (CAV)
    if (vbi_info.picture_number.has_value()) {
        picture_number_label_->setText(QString::number(vbi_info.picture_number.value()));
    } else {
        picture_number_label_->setText("-");
    }
    
    // CLV timecode
    if (vbi_info.clv_timecode.has_value()) {
        const auto& tc = vbi_info.clv_timecode.value();
        clv_timecode_label_->setText(QString("%1:%2:%3.%4")
            .arg(tc.hours, 2, 10, QChar('0'))
            .arg(tc.minutes, 2, 10, QChar('0'))
            .arg(tc.seconds, 2, 10, QChar('0'))
            .arg(tc.picture_number, 2, 10, QChar('0')));
    } else {
        clv_timecode_label_->setText("-");
    }
    
    // Chapter number
    if (vbi_info.chapter_number.has_value()) {
        chapter_number_label_->setText(QString::number(vbi_info.chapter_number.value()));
    } else {
        chapter_number_label_->setText("-");
    }
    
    // User code
    if (vbi_info.user_code.has_value()) {
        user_code_label_->setText(QString::fromStdString(vbi_info.user_code.value()));
    } else {
        user_code_label_->setText("-");
    }
    
    // Control codes
    stop_code_label_->setText(vbi_info.stop_code_present ? "Yes" : "No");
    lead_in_label_->setText(vbi_info.lead_in ? "Yes" : "No");
    lead_out_label_->setText(vbi_info.lead_out ? "Yes" : "No");
    
    // Programme status (original spec)
    if (vbi_info.programme_status.has_value()) {
        const auto& ps = vbi_info.programme_status.value();
        cx_enabled_label_->setText(ps.cx_enabled ? "On" : "Off");
        disc_size_label_->setText(ps.is_12_inch ? "12\"" : "8\"");
        disc_side_label_->setText(ps.is_side_1 ? "Side 1" : "Side 2");
        teletext_label_->setText(ps.has_teletext ? "Yes" : "No");
        digital_label_->setText(ps.is_digital ? "Digital" : "Analogue");
        sound_mode_label_->setText(formatSoundMode(ps.sound_mode));
        fm_multiplex_label_->setText(ps.is_fm_multiplex ? "Yes" : "No");
        programme_dump_label_->setText(ps.is_programme_dump ? "Yes" : "No");
        parity_valid_label_->setText(ps.parity_valid ? "Valid" : "Invalid");
        original_spec_tab_->setEnabled(true);
    } else {
        cx_enabled_label_->setText("-");
        disc_size_label_->setText("-");
        disc_side_label_->setText("-");
        teletext_label_->setText("-");
        digital_label_->setText("-");
        sound_mode_label_->setText("-");
        fm_multiplex_label_->setText("-");
        programme_dump_label_->setText("-");
        parity_valid_label_->setText("-");
        original_spec_tab_->setEnabled(false);
    }
    
    // Amendment 2 status
    if (vbi_info.amendment2_status.has_value()) {
        const auto& am2 = vbi_info.amendment2_status.value();
        copy_permitted_label_->setText(am2.copy_permitted ? "Yes" : "No");
        video_standard_label_->setText(am2.is_video_standard ? "Standard" : "Non-standard");
        sound_mode_am2_label_->setText(formatSoundMode(am2.sound_mode));
        amendment2_tab_->setEnabled(true);
    } else {
        copy_permitted_label_->setText("-");
        video_standard_label_->setText("-");
        sound_mode_am2_label_->setText("-");
        amendment2_tab_->setEnabled(false);
    }
}

void VBIDialog::clearVBIInfo()
{
    line16_label_->setText("------");
    line17_label_->setText("------");
    line18_label_->setText("------");
    picture_number_label_->setText("-");
    clv_timecode_label_->setText("-");
    chapter_number_label_->setText("-");
    user_code_label_->setText("-");
    stop_code_label_->setText("-");
    lead_in_label_->setText("-");
    lead_out_label_->setText("-");
    
    // Disable programme status tabs
    original_spec_tab_->setEnabled(false);
    amendment2_tab_->setEnabled(false);
}

QString VBIDialog::formatVBILine(int32_t vbi_value)
{
    if (vbi_value < 0) {
        return "Error";
    } else if (vbi_value == 0) {
        return "Blank";
    } else {
        // Format as hex with 6 digits (24-bit value)
        std::ostringstream oss;
        oss << "0x" << std::hex << std::uppercase << std::setw(6) << std::setfill('0') << vbi_value;
        return QString::fromStdString(oss.str());
    }
}

QString VBIDialog::formatSoundMode(orc::VbiSoundMode mode)
{
    switch (mode) {
        case orc::VbiSoundMode::STEREO:
            return "Stereo";
        case orc::VbiSoundMode::MONO:
            return "Mono";
        case orc::VbiSoundMode::AUDIO_SUBCARRIERS_OFF:
            return "Audio Off";
        case orc::VbiSoundMode::BILINGUAL:
            return "Bilingual";
        case orc::VbiSoundMode::STEREO_STEREO:
            return "Stereo + Stereo";
        case orc::VbiSoundMode::STEREO_BILINGUAL:
            return "Stereo + Bilingual";
        case orc::VbiSoundMode::CROSS_CHANNEL_STEREO:
            return "Cross-Channel Stereo";
        case orc::VbiSoundMode::BILINGUAL_BILINGUAL:
            return "Bilingual + Bilingual";
        case orc::VbiSoundMode::MONO_DUMP:
            return "Mono Dump";
        case orc::VbiSoundMode::STEREO_DUMP:
            return "Stereo Dump";
        case orc::VbiSoundMode::BILINGUAL_DUMP:
            return "Bilingual Dump";
        case orc::VbiSoundMode::FUTURE_USE:
            return "Future Use";
        default:
            return "Unknown";
    }
}

void VBIDialog::updateVBIInfoFrame(const orc::VBIFieldInfo& field1_info, 
                                    const orc::VBIFieldInfo& field2_info)
{
    // Display both field numbers (0-indexed)
    field_number_label_->setText(QString("%1 + %2")
        .arg(field1_info.field_id.value())
        .arg(field2_info.field_id.value()));
    
    // Prefer VBI data from the field that has it
    const orc::VBIFieldInfo* primary = nullptr;
    const orc::VBIFieldInfo* secondary = nullptr;
    
    if (field1_info.has_vbi_data && field2_info.has_vbi_data) {
        // Both have data - use first as primary, second as secondary
        primary = &field1_info;
        secondary = &field2_info;
    } else if (field1_info.has_vbi_data) {
        primary = &field1_info;
    } else if (field2_info.has_vbi_data) {
        primary = &field2_info;
    }
    
    if (!primary) {
        clearVBIInfo();
        return;
    }
    
    // Raw VBI data - show both fields
    line16_label_->setText(QString("%1 / %2")
        .arg(formatVBILine(field1_info.vbi_data[0]))
        .arg(formatVBILine(field2_info.vbi_data[0])));
    line17_label_->setText(QString("%1 / %2")
        .arg(formatVBILine(field1_info.vbi_data[1]))
        .arg(formatVBILine(field2_info.vbi_data[1])));
    line18_label_->setText(QString("%1 / %2")
        .arg(formatVBILine(field1_info.vbi_data[2]))
        .arg(formatVBILine(field2_info.vbi_data[2])));
    
    // For decoded data, prefer picture number from either field
    if (primary->picture_number.has_value()) {
        picture_number_label_->setText(QString::number(primary->picture_number.value()));
    } else if (secondary && secondary->picture_number.has_value()) {
        picture_number_label_->setText(QString::number(secondary->picture_number.value()));
    } else {
        picture_number_label_->setText("-");
    }
    
    // CLV timecode
    if (primary->clv_timecode.has_value()) {
        const auto& tc = primary->clv_timecode.value();
        clv_timecode_label_->setText(QString("%1:%2:%3.%4")
            .arg(tc.hours, 2, 10, QChar('0'))
            .arg(tc.minutes, 2, 10, QChar('0'))
            .arg(tc.seconds, 2, 10, QChar('0'))
            .arg(tc.picture_number, 2, 10, QChar('0')));
    } else if (secondary && secondary->clv_timecode.has_value()) {
        const auto& tc = secondary->clv_timecode.value();
        clv_timecode_label_->setText(QString("%1:%2:%3.%4")
            .arg(tc.hours, 2, 10, QChar('0'))
            .arg(tc.minutes, 2, 10, QChar('0'))
            .arg(tc.seconds, 2, 10, QChar('0'))
            .arg(tc.picture_number, 2, 10, QChar('0')));
    } else {
        clv_timecode_label_->setText("-");
    }
    
    // Chapter number
    if (primary->chapter_number.has_value()) {
        chapter_number_label_->setText(QString::number(primary->chapter_number.value()));
    } else if (secondary && secondary->chapter_number.has_value()) {
        chapter_number_label_->setText(QString::number(secondary->chapter_number.value()));
    } else {
        chapter_number_label_->setText("-");
    }
    
    // User code
    if (primary->user_code.has_value()) {
        user_code_label_->setText(QString::fromStdString(primary->user_code.value()));
    } else if (secondary && secondary->user_code.has_value()) {
        user_code_label_->setText(QString::fromStdString(secondary->user_code.value()));
    } else {
        user_code_label_->setText("-");
    }
    
    // Control codes - show if present in either field
    bool has_stop = primary->stop_code_present || (secondary && secondary->stop_code_present);
    bool has_lead_in = primary->lead_in || (secondary && secondary->lead_in);
    bool has_lead_out = primary->lead_out || (secondary && secondary->lead_out);
    
    stop_code_label_->setText(has_stop ? "Yes" : "No");
    lead_in_label_->setText(has_lead_in ? "Yes" : "No");
    lead_out_label_->setText(has_lead_out ? "Yes" : "No");
    
    // Programme status (prefer primary, fallback to secondary)
    auto prog_status = primary->programme_status.has_value() ? primary->programme_status : 
                      (secondary ? secondary->programme_status : std::nullopt);
    
    if (prog_status.has_value()) {
        const auto& ps = prog_status.value();
        cx_enabled_label_->setText(ps.cx_enabled ? "On" : "Off");
        disc_size_label_->setText(ps.is_12_inch ? "12\"" : "8\"");
        disc_side_label_->setText(ps.is_side_1 ? "Side 1" : "Side 2");
        teletext_label_->setText(ps.has_teletext ? "Yes" : "No");
        digital_label_->setText(ps.is_digital ? "Digital" : "Analogue");
        sound_mode_label_->setText(formatSoundMode(ps.sound_mode));
        fm_multiplex_label_->setText(ps.is_fm_multiplex ? "Yes" : "No");
        programme_dump_label_->setText(ps.is_programme_dump ? "Yes" : "No");
        parity_valid_label_->setText(ps.parity_valid ? "Valid" : "Invalid");
        original_spec_tab_->setEnabled(true);
    } else {
        cx_enabled_label_->setText("-");
        disc_size_label_->setText("-");
        disc_side_label_->setText("-");
        teletext_label_->setText("-");
        digital_label_->setText("-");
        sound_mode_label_->setText("-");
        fm_multiplex_label_->setText("-");
        programme_dump_label_->setText("-");
        parity_valid_label_->setText("-");
        original_spec_tab_->setEnabled(false);
    }
    
    // Amendment 2 status
    auto am2_status = primary->amendment2_status.has_value() ? primary->amendment2_status :
                     (secondary ? secondary->amendment2_status : std::nullopt);
    
    if (am2_status.has_value()) {
        const auto& am2 = am2_status.value();
        copy_permitted_label_->setText(am2.copy_permitted ? "Yes" : "No");
        video_standard_label_->setText(am2.is_video_standard ? "Standard" : "Non-standard");
        sound_mode_am2_label_->setText(formatSoundMode(am2.sound_mode));
        amendment2_tab_->setEnabled(true);
    } else {
        copy_permitted_label_->setText("-");
        video_standard_label_->setText("-");
        sound_mode_am2_label_->setText("-");
        amendment2_tab_->setEnabled(false);
    }
}
