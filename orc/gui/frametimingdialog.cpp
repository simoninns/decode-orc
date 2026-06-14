/*
 * File:        frametimingdialog.cpp
 * Module:      orc-gui
 * Purpose:     Frame timing visualization dialog (CVBS_U10_4FSC domain)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "frametimingdialog.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "fieldtimingwidget.h"

namespace {
constexpr int kMinLinesVisible = 2;
constexpr int kZoomSliderMin = 0;
constexpr int kZoomSliderMax = 1000;
constexpr const char* kSettingsGroup = "FrameTimingDialog";
constexpr const char* kNumberingModeKey = "NumberingMode";
}  // namespace

// ============================================================================
// Construction / destruction
// ============================================================================

FrameTimingDialog::FrameTimingDialog(QWidget* parent) : QDialog(parent) {
  setupUI();
  setWindowTitle("Frame Timing View");
  setWindowFlags(Qt::Window);
  setModal(false);
  setAttribute(Qt::WA_DeleteOnClose, false);
  resize(900, 500);

  QSettings settings;
  restoreGeometry(
      settings.value(QString("%1/geometry").arg(kSettingsGroup)).toByteArray());
  loadNumberingModePreference();
}

FrameTimingDialog::~FrameTimingDialog() {
  QSettings settings;
  settings.setValue(QString("%1/geometry").arg(kSettingsGroup), saveGeometry());
}

// ============================================================================
// QSettings persistence
// ============================================================================

void FrameTimingDialog::saveNumberingModePreference() const {
  QSettings settings;
  settings.beginGroup(kSettingsGroup);
  settings.setValue(kNumberingModeKey, numbering_mode_combo_->currentIndex());
  settings.endGroup();
}

void FrameTimingDialog::loadNumberingModePreference() {
  QSettings settings;
  settings.beginGroup(kSettingsGroup);
  int idx = settings.value(kNumberingModeKey, 0).toInt();
  settings.endGroup();
  const QSignalBlocker blocker(numbering_mode_combo_);
  numbering_mode_combo_->setCurrentIndex(
      qBound(0, idx, numbering_mode_combo_->count() - 1));
}

// ============================================================================
// UI setup
// ============================================================================

void FrameTimingDialog::setupUI() {
  auto* main_layout = new QVBoxLayout(this);

  timing_widget_ = new FieldTimingWidget(this);
  main_layout->addWidget(timing_widget_, 1);

  zoom_settle_timer_ = new QTimer(this);
  zoom_settle_timer_->setSingleShot(true);
  connect(zoom_settle_timer_, &QTimer::timeout, this,
          [this]() { finalizeRenderQuality(); });

  // ---- Info bar (colour frame index + video system + frame height)
  auto* info_row = new QHBoxLayout();
  info_label_ = new QLabel(this);
  info_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  QFont monoFont("Monospace");
  monoFont.setStyleHint(QFont::TypeWriter);
  info_label_->setFont(monoFont);
  info_row->addWidget(info_label_);
  info_row->addStretch();
  main_layout->addLayout(info_row);

  // ---- Control row
  auto* control_layout = new QHBoxLayout();

  jump_button_ = new QPushButton("Jump to Crosshairs");
  jump_button_->setEnabled(false);
  jump_button_->setAutoDefault(false);
  connect(jump_button_, &QPushButton::clicked,
          [this]() { timing_widget_->scrollToMarker(); });
  control_layout->addWidget(jump_button_);

  set_crosshairs_button_ = new QPushButton("Set Crosshairs");
  set_crosshairs_button_->setAutoDefault(false);
  connect(set_crosshairs_button_, &QPushButton::clicked,
          [this]() { emit setCrosshairsRequested(); });
  control_layout->addWidget(set_crosshairs_button_);

  control_layout->addSpacing(20);

  auto* line_label = new QLabel("Line:");
  control_layout->addWidget(line_label);

  line_spinbox_ = new QSpinBox();
  line_spinbox_->setMinimum(1);
  line_spinbox_->setMaximum(625);
  line_spinbox_->setValue(1);
  line_spinbox_->setMinimumWidth(80);
  connect(line_spinbox_, &QSpinBox::editingFinished,
          [this]() { timing_widget_->scrollToLine(line_spinbox_->value()); });
  control_layout->addWidget(line_spinbox_);

  jump_line_button_ = new QPushButton("Jump to Line");
  jump_line_button_->setAutoDefault(false);
  connect(jump_line_button_, &QPushButton::clicked,
          [this]() { timing_widget_->scrollToLine(line_spinbox_->value()); });
  control_layout->addWidget(jump_line_button_);

  control_layout->addSpacing(20);

  // Line numbering mode
  numbering_mode_label_ = new QLabel("Line numbering:");
  control_layout->addWidget(numbering_mode_label_);

  numbering_mode_combo_ = new QComboBox(this);
  numbering_mode_combo_->addItem("Frame flat (0-based)");
  numbering_mode_combo_->addItem("Frame sequential (1-based)");
  numbering_mode_combo_->addItem("Field relative");
  numbering_mode_combo_->addItem("Broadcast interlaced");
  connect(numbering_mode_combo_,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) { saveNumberingModePreference(); });
  control_layout->addWidget(numbering_mode_combo_);

  control_layout->addSpacing(20);

  signal_label_ = new QLabel("Channel:");
  signal_label_->setVisible(false);
  control_layout->addWidget(signal_label_);

  signal_combo_ = new QComboBox();
  signal_combo_->addItem("Luma (Y)");
  signal_combo_->addItem("Chroma (C)");
  signal_combo_->addItem("Both (Y & C)");
  signal_combo_->addItem("Y+C");
  signal_combo_->setVisible(false);
  connect(signal_combo_, &QComboBox::currentIndexChanged, [this](int index) {
    current_signal_index_ = index;
    timing_widget_->setChannelMode(
        static_cast<FieldTimingWidget::ChannelMode>(index));
  });
  control_layout->addWidget(signal_combo_);

  control_layout->addStretch();

  // Zoom controls
  auto* zoom_label = new QLabel("Lines:");
  control_layout->addWidget(zoom_label);

  auto moveZoomByButton = [this](int direction) {
    const int total_lines = currentTotalLines();
    const int delta_lines = std::max(1, current_lines_to_show_ / 25);
    const int target_lines =
        std::clamp(current_lines_to_show_ + (direction * delta_lines),
                   kMinLinesVisible, total_lines);
    if (target_lines == current_lines_to_show_) return;
    int target_pos = linesToSliderPosition(target_lines);
    if (target_pos == zoom_slider_->value()) {
      int scan_pos = zoom_slider_->value();
      while (true) {
        scan_pos += direction;
        if (scan_pos < zoom_slider_->minimum() ||
            scan_pos > zoom_slider_->maximum()) {
          break;
        }
        const int mapped = sliderPositionToLines(scan_pos);
        if ((direction < 0 && mapped < current_lines_to_show_) ||
            (direction > 0 && mapped > current_lines_to_show_)) {
          target_pos = scan_pos;
          break;
        }
      }
    }
    zoom_slider_->setValue(target_pos);
  };

  auto* zoom_in_button = new QPushButton("-");
  zoom_in_button->setMaximumWidth(30);
  zoom_in_button->setAutoRepeat(true);
  zoom_in_button->setAutoRepeatDelay(250);
  zoom_in_button->setAutoRepeatInterval(50);
  connect(zoom_in_button, &QPushButton::clicked,
          [moveZoomByButton]() { moveZoomByButton(-1); });
  connect(zoom_in_button, &QPushButton::pressed,
          [this]() { beginDraftRendering(); });
  connect(zoom_in_button, &QPushButton::released,
          [this]() { finalizeRenderQuality(); });
  control_layout->addWidget(zoom_in_button);

  zoom_slider_ = new QSlider(Qt::Horizontal);
  zoom_slider_->setMinimum(kZoomSliderMin);
  zoom_slider_->setMaximum(kZoomSliderMax);
  zoom_slider_->setValue(linesToSliderPosition(current_lines_to_show_));
  zoom_slider_->setTickPosition(QSlider::TicksBelow);
  zoom_slider_->setTickInterval(100);
  zoom_slider_->setMaximumWidth(150);
  connect(zoom_slider_, &QSlider::valueChanged, [this](int slider_position) {
    current_lines_to_show_ = sliderPositionToLines(slider_position);
    zoom_value_label_->setText(QString::number(current_lines_to_show_));
    beginDraftRendering();
    applyZoomFromLines(current_lines_to_show_);
    scheduleFinalRender();
  });
  connect(zoom_slider_, &QSlider::sliderPressed,
          [this]() { beginDraftRendering(); });
  connect(zoom_slider_, &QSlider::sliderReleased,
          [this]() { finalizeRenderQuality(); });
  control_layout->addWidget(zoom_slider_);

  auto* zoom_out_button = new QPushButton("+");
  zoom_out_button->setMaximumWidth(30);
  zoom_out_button->setAutoRepeat(true);
  zoom_out_button->setAutoRepeatDelay(250);
  zoom_out_button->setAutoRepeatInterval(50);
  connect(zoom_out_button, &QPushButton::clicked,
          [moveZoomByButton]() { moveZoomByButton(1); });
  connect(zoom_out_button, &QPushButton::pressed,
          [this]() { beginDraftRendering(); });
  connect(zoom_out_button, &QPushButton::released,
          [this]() { finalizeRenderQuality(); });
  control_layout->addWidget(zoom_out_button);

  zoom_value_label_ = new QLabel("625");
  zoom_value_label_->setMinimumWidth(40);
  zoom_value_label_->setText(QString::number(current_lines_to_show_));
  control_layout->addWidget(zoom_value_label_);

  control_layout->addSpacing(10);

  auto* close_button = new QPushButton("Close");
  connect(close_button, &QPushButton::clicked, this, &QDialog::close);
  control_layout->addWidget(close_button);

  main_layout->addLayout(control_layout);
}

// ============================================================================
// Field-domain bridge (migration period)
// ============================================================================

void FrameTimingDialog::setFieldData(
    const QString& node_id, uint64_t field_index,
    const std::vector<uint16_t>& samples,
    const std::optional<uint64_t>& field_index_2,
    const std::vector<uint16_t>& samples_2,
    const std::vector<uint16_t>& y_samples,
    const std::vector<uint16_t>& c_samples,
    const std::vector<uint16_t>& y_samples_2,
    const std::vector<uint16_t>& c_samples_2,
    const std::optional<orc::presenters::VideoParametersView>& video_params,
    const std::optional<int>& marker_sample, int first_field_height,
    int second_field_height) {
  first_field_height_ = first_field_height;
  second_field_height_ = second_field_height;

  // Convert uint16_t VFR samples to int16_t CVBS_U10_4FSC domain.
  // Values above 32767 are clamped; on-spec composite signals fit within range.
  auto toI16 = [](const std::vector<uint16_t>& src) -> std::vector<int16_t> {
    std::vector<int16_t> dst;
    dst.reserve(src.size());
    for (uint16_t v : src) {
      dst.push_back(static_cast<int16_t>(std::min<uint32_t>(v, 32767U)));
    }
    return dst;
  };

  std::vector<int16_t> frame_samples = toI16(samples);
  std::vector<int16_t> frame_y = toI16(y_samples);
  std::vector<int16_t> frame_c = toI16(c_samples);

  if (field_index_2.has_value()) {
    auto s2 = toI16(samples_2);
    frame_samples.insert(frame_samples.end(), s2.begin(), s2.end());
    auto y2 = toI16(y_samples_2);
    frame_y.insert(frame_y.end(), y2.begin(), y2.end());
    auto c2 = toI16(c_samples_2);
    frame_c.insert(frame_c.end(), c2.begin(), c2.end());
  }

  int frame_height = first_field_height + second_field_height;
  uint64_t frame_id = field_index / 2;

  setFrameData(node_id, frame_id, frame_samples, /*colour_frame_index=*/-1,
               video_params, marker_sample, frame_height, frame_y, frame_c);
}

// ============================================================================
// Primary data entry point
// ============================================================================

void FrameTimingDialog::setFrameData(
    const QString& node_id, uint64_t frame_id,
    const std::vector<int16_t>& samples, int colour_frame_index,
    const std::optional<orc::presenters::VideoParametersView>& video_params,
    const std::optional<int>& marker_sample, int frame_height,
    const std::vector<int16_t>& y_samples,
    const std::vector<int16_t>& c_samples) {
  current_node_id_ = node_id;
  current_frame_id_ = frame_id;
  current_frame_height_ = frame_height;
  current_colour_frame_index_ = colour_frame_index;
  current_video_params_ = video_params;

  // Window title: Stage N – Frame M
  setWindowTitle(QString("Frame Timing View – Stage: %1, Frame: %2")
                     .arg(node_id)
                     .arg(frame_id + 1));

  // Build info bar text
  const orc::presenters::VideoSystem sys =
      video_params.has_value() ? video_params->system
                               : orc::presenters::VideoSystem::Unknown;
  QString cfi_str = ::formatColourFrameIndex(colour_frame_index, sys);
  QString sys_str = "Unknown";
  int lines_per_frame = (frame_height > 0) ? frame_height : 625;
  if (video_params.has_value()) {
    switch (video_params->system) {
      case orc::presenters::VideoSystem::PAL:
        sys_str = "PAL";
        if (lines_per_frame <= 0) lines_per_frame = 625;
        break;
      case orc::presenters::VideoSystem::NTSC:
        sys_str = "NTSC";
        if (lines_per_frame <= 0) lines_per_frame = 525;
        break;
      case orc::presenters::VideoSystem::PAL_M:
        sys_str = "PAL-M";
        if (lines_per_frame <= 0) lines_per_frame = 525;
        break;
      default:
        break;
    }
  }
  info_label_->setText(
      QString("System: %1   Frame height: %2   Colour frame index: %3")
          .arg(sys_str)
          .arg(lines_per_frame)
          .arg(cfi_str));

  // Convert int16_t → uint16_t at the FieldTimingWidget boundary.
  // CVBS_U10_4FSC values are in [0, 1023] (non-negative 10-bit), so the cast
  // is lossless for on-spec samples.  The waveform amplitude display will
  // appear at the bottom of the uint16_t range; this is a known migration
  // limitation and will be resolved when FieldTimingWidget is updated.
  auto toU16 = [](const std::vector<int16_t>& src) -> std::vector<uint16_t> {
    std::vector<uint16_t> dst;
    dst.reserve(src.size());
    for (const int16_t v : src) {
      dst.push_back(static_cast<uint16_t>(std::max<int16_t>(0, v)));
    }
    return dst;
  };

  const std::vector<uint16_t> u16_samples = toU16(samples);
  const std::vector<uint16_t> u16_y = toU16(y_samples);
  const std::vector<uint16_t> u16_c = toU16(c_samples);

  // Pass to FieldTimingWidget (single-field path — frame-flat samples)
  // The second-field arguments are left empty; the widget treats this as a
  // single contiguous buffer equal to the full frame height.
  timing_widget_->setFieldData(u16_samples, {}, u16_y, u16_c, {}, {},
                                video_params, marker_sample);

  const bool is_yc = !y_samples.empty() || !c_samples.empty();
  signal_label_->setVisible(is_yc);
  signal_combo_->setVisible(is_yc);
  if (is_yc) {
    current_signal_index_ =
        std::clamp(current_signal_index_, 0, signal_combo_->count() - 1);
    signal_combo_->setCurrentIndex(current_signal_index_);
    timing_widget_->setChannelMode(
        static_cast<FieldTimingWidget::ChannelMode>(current_signal_index_));
  } else {
    timing_widget_->setChannelMode(FieldTimingWidget::ChannelMode::YPlusC);
  }

  finalizeRenderQuality();
  jump_button_->setEnabled(marker_sample.has_value());

  if (lines_per_frame > 0) {
    line_spinbox_->setMaximum(lines_per_frame);
    current_lines_to_show_ =
        std::clamp(current_lines_to_show_, kMinLinesVisible, lines_per_frame);
    zoom_slider_->blockSignals(true);
    zoom_slider_->setValue(linesToSliderPosition(current_lines_to_show_));
    zoom_slider_->blockSignals(false);
    zoom_value_label_->setText(QString::number(current_lines_to_show_));
    applyZoomFromLines(current_lines_to_show_);
  }
}

// ============================================================================
// Zoom helpers (identical algorithm to FieldTimingDialog)
// ============================================================================

int FrameTimingDialog::currentTotalLines() const {
  const int total = (current_frame_height_ > 0) ? current_frame_height_ : 625;
  return std::max(kMinLinesVisible, total);
}

int FrameTimingDialog::sliderPositionToLines(int slider_position) const {
  const int total = currentTotalLines();
  if (total <= kMinLinesVisible) return total;
  const double t =
      std::clamp(static_cast<double>(slider_position - kZoomSliderMin) /
                     static_cast<double>(kZoomSliderMax - kZoomSliderMin),
                 0.0, 1.0);
  const double ratio =
      static_cast<double>(total) / static_cast<double>(kMinLinesVisible);
  const int lines =
      static_cast<int>(std::round(static_cast<double>(kMinLinesVisible) *
                                   std::pow(ratio, t)));
  return std::clamp(lines, kMinLinesVisible, total);
}

int FrameTimingDialog::linesToSliderPosition(int lines_to_show) const {
  const int total = currentTotalLines();
  if (total <= kMinLinesVisible) return kZoomSliderMin;
  const int lines = std::clamp(lines_to_show, kMinLinesVisible, total);
  const double ratio =
      static_cast<double>(total) / static_cast<double>(kMinLinesVisible);
  const double normalized =
      std::clamp(std::log(static_cast<double>(lines) /
                          static_cast<double>(kMinLinesVisible)) /
                     std::log(ratio),
                 0.0, 1.0);
  return std::clamp(
      static_cast<int>(std::round(normalized * (kZoomSliderMax - kZoomSliderMin) +
                                   kZoomSliderMin)),
      kZoomSliderMin, kZoomSliderMax);
}

void FrameTimingDialog::applyZoomFromLines(int lines_to_show) {
  const int total = currentTotalLines();
  if (total <= 0 || lines_to_show <= 0) return;
  timing_widget_->setZoomFactor(
      static_cast<double>(total) / static_cast<double>(lines_to_show));
}

void FrameTimingDialog::beginDraftRendering() {
  timing_widget_->setDraftRenderMode(true);
}

void FrameTimingDialog::scheduleFinalRender() {
  constexpr int kZoomSettleDelayMs = 120;
  zoom_settle_timer_->start(kZoomSettleDelayMs);
}

void FrameTimingDialog::finalizeRenderQuality() {
  zoom_settle_timer_->stop();
  timing_widget_->setDraftRenderMode(false);
}
