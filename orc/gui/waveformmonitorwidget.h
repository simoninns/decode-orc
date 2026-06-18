/*
 * File:        waveformmonitorwidget.h
 * Module:      orc-gui
 * Purpose:     Waveform monitor raster widget
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef WAVEFORMMONITORWIDGET_H
#define WAVEFORMMONITORWIDGET_H

#include <QImage>
#include <QWidget>
#include <cstdint>
#include <optional>
#include <vector>

#include "presenters/include/hints_view_models.h"

/**
 * @brief Waveform monitor widget — sample-luminance histogram across all lines
 *
 * Accumulates a 2D count buffer: for every active video line in the frame,
 * for every sample position x, it increments count[x][mv_bin].  The buffer
 * is rendered as a QImage using area-max mapping followed by a Gaussian blur
 * (phosphor-glow effect), coloured with the theme's CompositePrimary token.
 *
 * Five normative level markers (sync tip, blanking, black, white, peak) are
 * drawn on top of the raster image, matching the Frame-scope style exactly.
 * X-axis carries sample-position tick labels; Y-axis carries mV labels.
 *
 * The gain control affects only the render pass — no re-accumulation needed.
 */
class WaveformMonitorWidget : public QWidget {
  Q_OBJECT

 public:
  explicit WaveformMonitorWidget(QWidget* parent = nullptr);

  /**
   * @brief Load frame data and rebuild the accumulation buffer.
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

  void setGain(double gain);
  double gain() const { return gain_; }

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  void accumulate(const std::vector<int16_t>& samples, int total_lines,
                  int active_start, int active_end, int32_t blanking_level,
                  int32_t white_level, double active_mv_val);
  void rebuildImage(const QRect& plot_area);
  void drawGrid(QPainter& painter, const QRect& plot_area) const;
  void drawYAxis(QPainter& painter, const QRect& plot_area) const;
  void drawXAxis(QPainter& painter, const QRect& plot_area) const;
  void drawLevelMarkers(QPainter& painter, const QRect& plot_area) const;
  int mvToPixelY(double mv, const QRect& plot_area) const;
  QRect plotArea() const;

  // Accumulation buffer: count_buffer_[x_sample][y_bin]
  std::vector<std::vector<uint32_t>> count_buffer_;
  int x_samples_ = 0;
  int active_video_start_ = 0;
  double us_per_sample_ = 1000000.0 / 14318181.8;  // default: NTSC 4FSC
  int y_bins_ = 0;
  double y_min_mv_ = -350.0;
  double y_max_mv_ = 950.0;
  static constexpr double kBinWidthMv = 1.0;
  int line_count_ = 0;

  double gain_ = 1.0;
  bool image_dirty_ = true;
  QImage cached_image_;

  std::optional<orc::presenters::VideoParametersView> video_params_;

  static constexpr int kLeftMargin = 55;
  static constexpr int kRightMargin = 10;
  static constexpr int kTopMargin = 15;
  static constexpr int kBottomMargin = 50;
};

#endif  // WAVEFORMMONITORWIDGET_H
