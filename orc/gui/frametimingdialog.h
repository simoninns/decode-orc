/*
 * File:        frametimingdialog.h
 * Module:      orc-gui
 * Purpose:     Frame timing visualization dialog (CVBS_U10_4FSC domain)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef FRAMETIMINGDIALOG_H
#define FRAMETIMINGDIALOG_H

#include <QDialog>
#include <QString>
#include <cstdint>
#include <optional>
#include <vector>

#include "presenters/include/hints_view_models.h"

// ============================================================================
// formatColourFrameIndex (standalone — testable without widget)
// ============================================================================
// Convert a FrameDescriptor::colour_frame_index to a display string.
//   -1        → "Unknown"
//   PAL       → "1" … "4"  (ITU-R BT.470-6 §3.5.2 / four-field sequence)
//   NTSC/PAL_M → "A" or "B"  (SMPTE 170M-2004 §11.2 / two-field sequence)
inline QString formatColourFrameIndex(int cfi,
                                       orc::presenters::VideoSystem system) {
  if (cfi < 0) return "Unknown";
  switch (system) {
    case orc::presenters::VideoSystem::PAL:
      return QString::number(cfi);
    case orc::presenters::VideoSystem::NTSC:
    case orc::presenters::VideoSystem::PAL_M:
      return (cfi == 0) ? "A" : "B";
    default:
      return QString::number(cfi);
  }
}

class FieldTimingWidget;
class QPushButton;
class QSpinBox;
class QSlider;
class QLabel;
class QComboBox;
class QTimer;

/**
 * @brief Dialog for viewing frame samples as a timing graph
 *
 * Replaces FieldTimingDialog with frame-level semantics:
 *  - Takes int16_t CVBS_U10_4FSC samples (converted to uint16_t at the
 *    FieldTimingWidget boundary during the migration period).
 *  - Displays colour_frame_index (PAL: 1-4, NTSC: A/B, -1 → Unknown).
 *  - Provides a LineNumberingMode selector shared with FrameScopeDialog.
 *  - Shows video system, frame rate, and field line count metadata.
 */
class FrameTimingDialog : public QDialog {
  Q_OBJECT

 public:
  explicit FrameTimingDialog(QWidget* parent = nullptr);
  ~FrameTimingDialog();

  /**
   * @brief Set frame data for timing display
   *
   * @param node_id           Stage node identifier (for window title)
   * @param frame_id          0-based frame index being displayed
   * @param samples           Frame-flat int16_t composite samples
   * @param colour_frame_index From FrameDescriptor: -1=unknown, 1-4=PAL,
   *                           0-1=NTSC/PAL_M
   * @param video_params      Optional video parameters (system, levels, etc.)
   * @param marker_sample     Optional sample position for the cross-hair marker
   * @param frame_height      Total line count for the frame (0 = use system default)
   * @param y_samples         Optional luma samples (YC sources)
   * @param c_samples         Optional chroma samples (YC sources)
   */
  void setFrameData(
      const QString& node_id, uint64_t frame_id,
      const std::vector<int16_t>& samples, int colour_frame_index = -1,
      const std::optional<orc::presenters::VideoParametersView>& video_params =
          std::nullopt,
      const std::optional<int>& marker_sample = std::nullopt,
      int frame_height = 0, const std::vector<int16_t>& y_samples = {},
      const std::vector<int16_t>& c_samples = {});

  /**
   * @brief Field-domain bridge: set two fields as a combined frame.
   *
   * Adapts the old field-domain API (two uint16_t field buffers) to the
   * frame-domain setFrameData() call.  This is a migration-period bridge; the
   * caller should move to setFrameData() once it produces frame-level data.
   */
  void setFieldData(
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
      int second_field_height);

  /// @name State accessors
  /// @{
  FieldTimingWidget* timingWidget() const { return timing_widget_; }
  uint64_t currentFrameId() const { return current_frame_id_; }
  int currentFrameHeight() const { return current_frame_height_; }
  int firstFieldHeight() const { return first_field_height_; }
  int secondFieldHeight() const { return second_field_height_; }
  /// @}

 Q_SIGNALS:
  void refreshRequested();
  void setCrosshairsRequested();

 private:
  void setupUI();
  void applyZoomFromLines(int lines_to_show);
  void beginDraftRendering();
  void scheduleFinalRender();
  void finalizeRenderQuality();
  int currentTotalLines() const;
  int sliderPositionToLines(int slider_position) const;
  int linesToSliderPosition(int lines_to_show) const;
  void saveNumberingModePreference() const;
  void loadNumberingModePreference();

  FieldTimingWidget* timing_widget_;
  QPushButton* jump_button_;
  QPushButton* set_crosshairs_button_;
  QSpinBox* line_spinbox_;
  QPushButton* jump_line_button_;
  QLabel* signal_label_;
  QComboBox* signal_combo_;
  QSlider* zoom_slider_;
  QLabel* zoom_value_label_;
  QTimer* zoom_settle_timer_;

  QLabel* info_label_;
  QLabel* numbering_mode_label_;
  QComboBox* numbering_mode_combo_;

  QString current_node_id_;
  uint64_t current_frame_id_ = 0;
  int current_frame_height_ = 0;
  int first_field_height_ = 0;
  int second_field_height_ = 0;
  int current_colour_frame_index_ = -1;
  int current_signal_index_ = 2;
  int current_lines_to_show_ = 625;
  std::optional<orc::presenters::VideoParametersView> current_video_params_;
};

#endif  // FRAMETIMINGDIALOG_H
