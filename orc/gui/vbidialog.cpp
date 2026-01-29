/*
 * File:        vbidialog.cpp
 * Module:      orc-gui
 * Purpose:     VBI information display dialog implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "vbidialog.h"
#include "field_frame_presentation.h"
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

void VBIDialog::updateVBIInfo(const orc::presenters::VBIFieldInfoView& vbi_info)
{
    if (!vbi_info.has_vbi_data) {
        // No valid VBI data - show "--" for field number too
        field_number_label_->setText("--");
        clearVBIInfo();
        return;
    }
    
    // Field number - convert 0-indexed to 1-indexed for display
    if (vbi_info.field_id < 0) {
        field_number_label_->setText("--");
    } else {
        field_number_label_->setText(orc::gui::formatFieldNumber(static_cast<uint64_t>(vbi_info.field_id)));
    }
    
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

QString VBIDialog::formatSoundMode(orc::presenters::VbiSoundModeView mode)
{
    switch (mode) {
        case orc::presenters::VbiSoundModeView::STEREO:
            return "Stereo";
        case orc::presenters::VbiSoundModeView::MONO:
            return "Mono";
        case orc::presenters::VbiSoundModeView::AUDIO_SUBCARRIERS_OFF:
            return "Audio Off";
        case orc::presenters::VbiSoundModeView::BILINGUAL:
            return "Bilingual";
        case orc::presenters::VbiSoundModeView::STEREO_STEREO:
            return "Stereo + Stereo";
        case orc::presenters::VbiSoundModeView::STEREO_BILINGUAL:
            return "Stereo + Bilingual";
        case orc::presenters::VbiSoundModeView::CROSS_CHANNEL_STEREO:
            return "Cross-Channel Stereo";
        case orc::presenters::VbiSoundModeView::BILINGUAL_BILINGUAL:
            return "Bilingual + Bilingual";
        case orc::presenters::VbiSoundModeView::MONO_DUMP:
            return "Mono Dump";
        case orc::presenters::VbiSoundModeView::STEREO_DUMP:
            return "Stereo Dump";
        case orc::presenters::VbiSoundModeView::BILINGUAL_DUMP:
            return "Bilingual Dump";
        case orc::presenters::VbiSoundModeView::FUTURE_USE:
            return "Future Use";
        default:
            return "Unknown";
    }
}

void VBIDialog::updateVBIInfoFrame(const orc::presenters::VBIFieldInfoView& field1_info, 
                                    const orc::presenters::VBIFieldInfoView& field2_info)
{
    // Display both field numbers (1-indexed) or "--" if invalid
    if (field1_info.field_id < 0 || field2_info.field_id < 0) {
        field_number_label_->setText("--");
    } else {
        // Calculate frame index from first field ID (0-indexed)
        // Frame consists of fields (2*frame_index) and (2*frame_index + 1)
        uint64_t frame_index = static_cast<uint64_t>(field1_info.field_id) / 2;
        field_number_label_->setText(orc::gui::formatFrameFieldRange(frame_index));
    }
    
    if (!field1_info.has_vbi_data && !field2_info.has_vbi_data) {
        clearVBIInfo();
        return;
    }
    
    // Raw VBI data - show both fields separated by /
    line16_label_->setText(QString("%1 / %2")
        .arg(formatVBILine(field1_info.vbi_data[0]))
        .arg(formatVBILine(field2_info.vbi_data[0])));
    line17_label_->setText(QString("%1 / %2")
        .arg(formatVBILine(field1_info.vbi_data[1]))
        .arg(formatVBILine(field2_info.vbi_data[1])));
    line18_label_->setText(QString("%1 / %2")
        .arg(formatVBILine(field1_info.vbi_data[2]))
        .arg(formatVBILine(field2_info.vbi_data[2])));
    
    // Prefer merged-like data by picking available values
    const auto pick_optional = [](const auto& a, const auto& b) {
        return a.has_value() ? a : b;
    };

    auto picture_number = pick_optional(field1_info.picture_number, field2_info.picture_number);
    if (picture_number.has_value()) {
        picture_number_label_->setText(QString::number(*picture_number));
    } else {
        picture_number_label_->setText("-");
    }

    auto clv_tc = pick_optional(field1_info.clv_timecode, field2_info.clv_timecode);
    if (clv_tc.has_value()) {
        const auto& tc = *clv_tc;
        clv_timecode_label_->setText(QString("%1:%2:%3.%4")
            .arg(tc.hours, 2, 10, QChar('0'))
            .arg(tc.minutes, 2, 10, QChar('0'))
            .arg(tc.seconds, 2, 10, QChar('0'))
            .arg(tc.picture_number, 2, 10, QChar('0')));
    } else {
        clv_timecode_label_->setText("-");
    }

    auto chapter = pick_optional(field1_info.chapter_number, field2_info.chapter_number);
    if (chapter.has_value()) {
        chapter_number_label_->setText(QString::number(*chapter));
    } else {
        chapter_number_label_->setText("-");
    }

    auto user_code = pick_optional(field1_info.user_code, field2_info.user_code);
    if (user_code.has_value()) {
        user_code_label_->setText(QString::fromStdString(*user_code));
    } else {
        user_code_label_->setText("-");
    }

    // Control codes (OR across both fields)
    stop_code_label_->setText((field1_info.stop_code_present || field2_info.stop_code_present) ? "Yes" : "No");
    lead_in_label_->setText((field1_info.lead_in || field2_info.lead_in) ? "Yes" : "No");
    lead_out_label_->setText((field1_info.lead_out || field2_info.lead_out) ? "Yes" : "No");

    // Programme status
    auto programme = pick_optional(field1_info.programme_status, field2_info.programme_status);
    if (programme.has_value()) {
        const auto& ps = *programme;
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
    auto am2 = pick_optional(field1_info.amendment2_status, field2_info.amendment2_status);
    if (am2.has_value()) {
        const auto& a = *am2;
        copy_permitted_label_->setText(a.copy_permitted ? "Yes" : "No");
        video_standard_label_->setText(a.is_video_standard ? "Standard" : "Non-standard");
        sound_mode_am2_label_->setText(formatSoundMode(a.sound_mode));
        amendment2_tab_->setEnabled(true);
    } else {
        copy_permitted_label_->setText("-");
        video_standard_label_->setText("-");
        sound_mode_am2_label_->setText("-");
        amendment2_tab_->setEnabled(false);
    }
}
