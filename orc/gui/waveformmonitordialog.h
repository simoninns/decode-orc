/*
 * File:        waveformmonitordialog.h
 * Module:      orc-gui
 * Purpose:     Waveform monitor dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef WAVEFORMMONITORDIALOG_H
#define WAVEFORMMONITORDIALOG_H

#include <QDialog>
#include <cstdint>
#include <optional>
#include <vector>

#include "presenters/include/hints_view_models.h"

class WaveformMonitorWidget;
class QLabel;
class QSlider;

/**
 * @brief Dialog for the multi-line waveform monitor view
 *
 * Displays a sample-luminance histogram across all active video lines in a
 * frame.  Brightness encodes how many lines share a given (sample, mV) pair.
 * An intensity gain control lets the user brighten sparse signals or prevent
 * saturation in high-uniformity scenes without requiring re-accumulation.
 */
class WaveformMonitorDialog : public QDialog {
  Q_OBJECT

 public:
  explicit WaveformMonitorDialog(QWidget* parent = nullptr);
  ~WaveformMonitorDialog();

  /**
   * @brief Feed new frame data to the waveform monitor.
   *
   * Shows and raises the dialog if it is not already visible.
   *
   * @param composite_samples Flat concatenation of all field samples
   * @param first_field_height  Lines in the first field
   * @param second_field_height Lines in the second field (0 = single field)
   * @param video_params        Signal levels and active video range
   */
  void setData(
      const std::vector<int16_t>& composite_samples, int first_field_height,
      int second_field_height,
      const std::optional<orc::presenters::VideoParametersView>& video_params);

  WaveformMonitorWidget* monitorWidget() const { return monitor_widget_; }

 private:
  void setupUI();

  WaveformMonitorWidget* monitor_widget_;
  QSlider* gain_slider_;
  QLabel* gain_value_label_;
};

#endif  // WAVEFORMMONITORDIALOG_H
