/*
 * File:        waveformmonitordialog.cpp
 * Module:      orc-gui
 * Purpose:     Waveform monitor dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "waveformmonitordialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>

#include "waveformmonitorwidget.h"

// Intensity slider: range 1–100 maps to gain 0.1–10.0 (divide by 10).
static constexpr int kSliderMin = 1;
static constexpr int kSliderMax = 100;
static constexpr int kSliderDefault = 30;  // 3.0×
static constexpr double kSliderScale = 10.0;

WaveformMonitorDialog::WaveformMonitorDialog(QWidget* parent)
    : QDialog(parent),
      monitor_widget_(nullptr),
      gain_slider_(nullptr),
      gain_value_label_(nullptr) {
  setWindowFlags(Qt::Window);
  setModal(false);
  setAttribute(Qt::WA_DeleteOnClose, false);
  setWindowTitle("Waveform Monitor");

  setupUI();

  QSettings settings;
  const QByteArray geom =
      settings.value("WaveformMonitorDialog/geometry").toByteArray();
  if (!geom.isEmpty()) {
    restoreGeometry(geom);
  } else {
    resize(900, 500);
  }
}

WaveformMonitorDialog::~WaveformMonitorDialog() {
  QSettings settings;
  settings.setValue("WaveformMonitorDialog/geometry", saveGeometry());
}

void WaveformMonitorDialog::setupUI() {
  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(4, 4, 4, 4);
  main_layout->setSpacing(4);

  // Raster widget fills the available space.
  monitor_widget_ = new WaveformMonitorWidget(this);
  main_layout->addWidget(monitor_widget_, 1);

  // Controls row at the bottom of the dialog.
  auto* controls = new QHBoxLayout();

  controls->addWidget(new QLabel("Intensity:"));
  gain_slider_ = new QSlider(Qt::Horizontal);
  gain_slider_->setRange(kSliderMin, kSliderMax);
  gain_slider_->setValue(kSliderDefault);
  gain_slider_->setTickPosition(QSlider::NoTicks);
  gain_slider_->setToolTip(
      "Intensity gain — higher values brighten sparse signals and reach "
      "saturation sooner; lower values reduce saturation in uniform scenes.");
  controls->addWidget(gain_slider_, 1);
  gain_value_label_ = new QLabel();
  gain_value_label_->setMinimumWidth(40);
  gain_value_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  controls->addWidget(gain_value_label_);

  main_layout->addLayout(controls);

  // Initialise gain from the default slider position.
  const double initial_gain = kSliderDefault / kSliderScale;
  monitor_widget_->setGain(initial_gain);
  gain_value_label_->setText(QString::number(initial_gain, 'f', 1) +
                             QString::fromUtf8("×"));

  connect(gain_slider_, &QSlider::valueChanged, this, [this](int v) {
    const double gain = v / kSliderScale;
    monitor_widget_->setGain(gain);
    gain_value_label_->setText(QString::number(gain, 'f', 1) +
                               QString::fromUtf8("×"));
  });
}

void WaveformMonitorDialog::setData(
    const std::vector<int16_t>& composite_samples, int first_field_height,
    int second_field_height,
    const std::optional<orc::presenters::VideoParametersView>& video_params) {
  monitor_widget_->setData(composite_samples, first_field_height,
                           second_field_height, video_params);

  if (!isVisible()) {
    show();
    raise();
    activateWindow();
  }
}
