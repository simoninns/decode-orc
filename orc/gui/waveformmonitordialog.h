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

#include <orc/stage/common_types.h>

#include <QDialog>
#include <cstdint>
#include <optional>
#include <vector>

#include "presenters/include/hints_view_models.h"

class WaveformMonitorWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;

/**
 * @brief Selects which signal component the waveform monitor displays.
 *
 * YPlusC — full composite (Y+C): composite_samples when the source is
 * composite, or the point-wise sum of y_samples and c_samples for Y/C sources.
 *
 * YOnly — luma only: y_samples when separately available, or composite
 * low-pass filtered to remove the colour subcarrier for composite sources.
 */
enum class WaveformChannel { YPlusC, YOnly };

/**
 * @brief Selects which part of the frame the waveform monitor accumulates.
 *
 * Frame — both fields together (the whole frame).
 *
 * Field1 / Field2 — one field only.  The NTSC colour subcarrier phase
 * alternates from field to field, so accumulating a single field separates
 * field-correlated artefacts (subcarrier leakage into sync, for example)
 * from noise that is random across the frame.
 *
 * Field1 is the field that appears first in the supplied buffer, which for a
 * reversed-field preview is the second field of the frame.
 */
enum class WaveformFieldSelection { Frame, Field1, Field2 };

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
   * @param composite_samples Composite (Y+C) field samples; empty for Y/C
   *                          sources that supply y_samples/c_samples separately
   * @param y_samples         Luma-only samples; empty for composite sources
   * @param c_samples         Chroma-only samples; empty for composite sources
   * @param first_field_height  Lines in the first field
   * @param second_field_height Lines in the second field (0 = single field)
   * @param video_params        Signal levels and active video range
   */
  void setData(
      std::vector<int16_t> composite_samples, std::vector<int16_t> y_samples,
      std::vector<int16_t> c_samples, int first_field_height,
      int second_field_height,
      const std::optional<orc::presenters::VideoParametersView>& video_params);

  void setAmplitudeUnit(orc::AmplitudeDisplayUnit unit);

  // Reduce a flat two-field buffer to the selected field.  field1_height and
  // field2_height are updated to describe the returned buffer: the selected
  // field becomes field 1 and field 2 becomes empty.  Returns |samples|
  // unchanged for WaveformFieldSelection::Frame, and when the requested field
  // is not present in the buffer.  Public for unit testing.
  static std::vector<int16_t> sliceToField(const std::vector<int16_t>& samples,
                                           int& field1_height,
                                           int& field2_height,
                                           WaveformFieldSelection selection);

 private:
  void setupUI();
  void updateWidgetForCurrentChannel();

  // Field selection is only meaningful when both fields are present; the
  // combo is disabled and forced back to Frame for single-field data.
  void updateFieldComboAvailability();

  // Current field selection, or Frame when the combo is not yet built.
  WaveformFieldSelection currentFieldSelection() const;

  // 4-tap moving-average FIR — notch at fs/4 removes 4FSC colour subcarrier.
  static std::vector<int16_t> extractYFromComposite(
      const std::vector<int16_t>& composite);

  // Strip VBI lines from the front of each field.  field1_height and
  // field2_height are updated to reflect the reduced line counts.
  static std::vector<int16_t> sliceToActiveLines(
      const std::vector<int16_t>& samples, int& field1_height,
      int& field2_height, int first_active_line);

  WaveformMonitorWidget* monitor_widget_;
  QComboBox* channel_combo_;
  QComboBox* range_combo_;
  QComboBox* field_combo_;
  QCheckBox* phosphor_check_;
  QSlider* gain_slider_;
  QLabel* gain_value_label_;

  // Stored frame data — held so the channel selector can switch without a
  // new render request.
  std::vector<int16_t> composite_samples_;
  std::vector<int16_t> y_samples_;
  std::vector<int16_t> c_samples_;
  int first_field_height_ = 0;
  int second_field_height_ = 0;
  std::optional<orc::presenters::VideoParametersView> video_params_;
};

#endif  // WAVEFORMMONITORDIALOG_H
