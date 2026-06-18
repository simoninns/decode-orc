/*
 * File:        waveformmonitorwidget.cpp
 * Module:      orc-gui
 * Purpose:     Waveform monitor raster widget
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "waveformmonitorwidget.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QRect>
#include <QResizeEvent>
#include <QSizePolicy>
#include <algorithm>
#include <cmath>

#include "framescopedialog.h"  // cvbs_sample_to_mv, active_video_mv
#include "plotwidget.h"        // PlotWidget::isDarkTheme()
#include "theme_color_tokens.h"

WaveformMonitorWidget::WaveformMonitorWidget(QWidget* parent)
    : QWidget(parent) {
  setMinimumSize(400, 300);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QRect WaveformMonitorWidget::plotArea() const {
  return QRect(kLeftMargin, kTopMargin, width() - kLeftMargin - kRightMargin,
               height() - kTopMargin - kBottomMargin);
}

// ---------------------------------------------------------------------------
// Data ingestion
// ---------------------------------------------------------------------------

void WaveformMonitorWidget::setData(
    const std::vector<int16_t>& composite_samples, int first_field_height,
    int second_field_height,
    const std::optional<orc::presenters::VideoParametersView>& video_params) {
  video_params_ = video_params;

  // Derive Y-axis range from video_params (matching Frame-scope style).
  y_min_mv_ = -350.0;
  y_max_mv_ = 950.0;
  if (video_params.has_value()) {
    const auto& vp = *video_params;
    if (vp.blanking_level >= 0 && vp.white_level > vp.blanking_level &&
        vp.sync_tip_level >= 0 && vp.peak_level >= 0) {
      const double amv = active_video_mv(vp.system);
      const double sync_tip_mv =
          cvbs_sample_to_mv(static_cast<int16_t>(vp.sync_tip_level),
                            vp.blanking_level, vp.white_level, amv);
      const double peak_mv =
          cvbs_sample_to_mv(static_cast<int16_t>(vp.peak_level),
                            vp.blanking_level, vp.white_level, amv);
      const double span = peak_mv - sync_tip_mv;
      if (span > 0.0) {
        y_min_mv_ = sync_tip_mv - span * 0.05;
        y_max_mv_ = peak_mv + span * 0.05;
      }
    }
  }
  y_bins_ = static_cast<int>((y_max_mv_ - y_min_mv_) / kBinWidthMv) + 1;

  const int total_lines =
      first_field_height + (second_field_height > 0 ? second_field_height : 0);
  if (total_lines <= 0 || composite_samples.empty()) {
    count_buffer_.clear();
    x_samples_ = 0;
    active_video_start_ = 0;
    line_count_ = 0;
    image_dirty_ = true;
    update();
    return;
  }

  const int samples_per_line =
      static_cast<int>(composite_samples.size()) / total_lines;
  if (samples_per_line <= 0) {
    count_buffer_.clear();
    x_samples_ = 0;
    active_video_start_ = 0;
    line_count_ = 0;
    image_dirty_ = true;
    update();
    return;
  }

  // Active video range — clip to samples_per_line to handle edge cases.
  int active_start = 0;
  int active_end = samples_per_line - 1;  // inclusive
  int32_t blanking_level = -1;
  int32_t white_level = -1;
  double amv = 700.0;

  if (video_params.has_value()) {
    const auto& vp = *video_params;
    if (vp.active_video_start >= 0 &&
        vp.active_video_end > vp.active_video_start) {
      active_start = std::min(vp.active_video_start, samples_per_line - 1);
      active_end = std::min(vp.active_video_end, samples_per_line - 1);
    }
    blanking_level = vp.blanking_level;
    white_level = vp.white_level;
    amv = active_video_mv(vp.system);
  }

  active_video_start_ = active_start;

  // µs per sample for the X-axis time labels.  Rates are 4FSC standards:
  // NTSC/PAL-M: 4 × 3.579545 MHz = 14 318 181.8 Hz
  // PAL:        4 × 4.433618 MHz = 17 734 475.0 Hz
  if (video_params.has_value()) {
    switch (video_params->system) {
      case orc::presenters::VideoSystem::PAL:
        us_per_sample_ = 1000000.0 / 17734475.0;
        break;
      case orc::presenters::VideoSystem::NTSC:
      case orc::presenters::VideoSystem::PAL_M:
      default:
        us_per_sample_ = 1000000.0 / 14318181.8;
        break;
    }
  }

  accumulate(composite_samples, total_lines, active_start, active_end,
             blanking_level, white_level, amv);
  line_count_ = total_lines;
  image_dirty_ = true;
  update();
}

void WaveformMonitorWidget::accumulate(const std::vector<int16_t>& samples,
                                       int total_lines, int active_start,
                                       int active_end, int32_t blanking_level,
                                       int32_t white_level,
                                       double active_mv_val) {
  const int active_width = active_end - active_start + 1;
  if (active_width <= 0 || total_lines <= 0) return;

  const int samples_per_line = static_cast<int>(samples.size()) / total_lines;
  if (samples_per_line <= 0) return;

  x_samples_ = active_width;
  count_buffer_.assign(static_cast<size_t>(x_samples_),
                       std::vector<uint32_t>(static_cast<size_t>(y_bins_), 0));

  const bool have_levels =
      (blanking_level >= 0 && white_level > blanking_level);

  for (int line = 0; line < total_lines; ++line) {
    const int line_start = line * samples_per_line;
    // Guard against truncated data (non-orthogonal PAL lines).
    if (line_start + active_end >= static_cast<int>(samples.size())) break;

    for (int x = 0; x < active_width; ++x) {
      const int16_t raw =
          samples[static_cast<size_t>(line_start) +
                  static_cast<size_t>(active_start) + static_cast<size_t>(x)];

      const double mv = have_levels
                            ? cvbs_sample_to_mv(raw, blanking_level,
                                                white_level, active_mv_val)
                            : static_cast<double>(raw);

      const int y_bin = static_cast<int>((mv - y_min_mv_) / kBinWidthMv);
      if (y_bin >= 0 && y_bin < y_bins_) {
        count_buffer_[static_cast<size_t>(x)][static_cast<size_t>(y_bin)]++;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Gain
// ---------------------------------------------------------------------------

void WaveformMonitorWidget::setGain(double gain) {
  gain_ = std::clamp(gain, 0.1, 10.0);
  image_dirty_ = true;
  update();
}

// ---------------------------------------------------------------------------
// Qt events
// ---------------------------------------------------------------------------

void WaveformMonitorWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  image_dirty_ = true;
}

void WaveformMonitorWidget::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  const QPalette palette = this->palette();

  // Widget background — themed like other plot widgets.
  painter.fillRect(rect(), palette.color(QPalette::Base));

  const QRect pa = plotArea();
  if (pa.isEmpty()) return;

  if (count_buffer_.empty() || x_samples_ == 0 || y_bins_ == 0) {
    painter.setPen(theme_tokens::mutedText(palette));
    painter.drawText(pa, Qt::AlignCenter, "No data");
    drawYAxis(painter, pa);
    drawXAxis(painter, pa);
    return;
  }

  if (image_dirty_ || cached_image_.size() != pa.size()) {
    rebuildImage(pa);
    image_dirty_ = false;
  }

  drawGrid(painter, pa);
  painter.drawImage(pa.topLeft(), cached_image_);

  drawLevelMarkers(painter, pa);
  drawYAxis(painter, pa);
  drawXAxis(painter, pa);
}

// ---------------------------------------------------------------------------
// Image rebuild — area-max accumulation + separable Gaussian blur
// ---------------------------------------------------------------------------

void WaveformMonitorWidget::rebuildImage(const QRect& pa) {
  if (pa.isEmpty() || x_samples_ == 0 || y_bins_ == 0) return;

  // ARGB32 with transparent background so grid lines drawn before drawImage()
  // remain visible through unpopulated pixels.
  cached_image_ = QImage(pa.size(), QImage::Format_ARGB32_Premultiplied);
  cached_image_.fill(Qt::transparent);
  const QColor base_color = this->palette().color(QPalette::Base);

  const int img_w = pa.width();
  const int img_h = pa.height();
  const size_t w = static_cast<size_t>(img_w);
  const size_t buf_sz = w * static_cast<size_t>(img_h);

  // For each output pixel, find the maximum count across all buffer cells that
  // map into its fractional range.  This avoids missed-bin aliasing when
  // x_samples_ >> img_w.
  //
  // Brightness mapping follows the Color Tools VirtualDub plugin reference:
  //   clamp[0] = 0
  //   clamp[j] = min(j + 128, 255)   for j >= 1
  //   brightness = clamp[count * 5 * gain] / 255
  //
  // Any non-zero count maps immediately to ~52 % brightness; full saturation
  // is reached after ~26 occurrences at gain=1 (ITU-R BT.601 broadcast norm).
  // The gain control moves the saturation knee: higher gain saturates faster.
  const float k = 5.0f * static_cast<float>(gain_);

  std::vector<float> bright(buf_sz, 0.0f);

  for (int px = 0; px < img_w; ++px) {
    const int xi_lo =
        static_cast<int>(static_cast<double>(px) / img_w * x_samples_);
    const int xi_hi = std::min(
        static_cast<int>(static_cast<double>(px + 1) / img_w * x_samples_),
        x_samples_ - 1);

    for (int py = 0; py < img_h; ++py) {
      const int yi_lo = static_cast<int>(static_cast<double>(img_h - py - 1) /
                                         img_h * y_bins_);
      const int yi_hi = std::min(
          static_cast<int>(static_cast<double>(img_h - py) / img_h * y_bins_),
          y_bins_ - 1);

      uint32_t max_count = 0;
      for (int xi = xi_lo; xi <= xi_hi; ++xi) {
        const auto& col = count_buffer_[static_cast<size_t>(xi)];
        for (int yi = yi_lo; yi <= yi_hi; ++yi) {
          max_count = std::max(max_count, col[static_cast<size_t>(yi)]);
        }
      }
      if (max_count == 0) continue;

      const float b =
          std::min(1.0f, (static_cast<float>(max_count) * k + 128.0f) / 255.0f);
      bright[static_cast<size_t>(py) * w + static_cast<size_t>(px)] = b;
    }
  }

  // Write QImage — interpolate Base → plotColor by brightness.
  const bool is_dark = PlotWidget::isDarkTheme();
  const QColor plot_color = theme_tokens::plotColor(
      theme_tokens::PlotColorToken::CompositePrimary, is_dark);
  const float br = static_cast<float>(base_color.redF());
  const float bg = static_cast<float>(base_color.greenF());
  const float bb = static_cast<float>(base_color.blueF());
  const float pr = static_cast<float>(plot_color.redF());
  const float pg = static_cast<float>(plot_color.greenF());
  const float pb = static_cast<float>(plot_color.blueF());

  for (int py = 0; py < img_h; ++py) {
    const size_t row = static_cast<size_t>(py) * w;
    for (int px = 0; px < img_w; ++px) {
      const float b = bright[row + static_cast<size_t>(px)];
      if (b <= 0.0f) continue;
      const int cr = static_cast<int>((br + (pr - br) * b) * 255.0f);
      const int cg = static_cast<int>((bg + (pg - bg) * b) * 255.0f);
      const int cb = static_cast<int>((bb + (pb - bb) * b) * 255.0f);
      cached_image_.setPixel(px, py, qRgb(cr, cg, cb));
    }
  }
}

// ---------------------------------------------------------------------------
// Axis and markers
// ---------------------------------------------------------------------------

int WaveformMonitorWidget::mvToPixelY(double mv, const QRect& pa) const {
  const double range = y_max_mv_ - y_min_mv_;
  if (range <= 0.0) return pa.center().y();
  const double t = (mv - y_min_mv_) / range;
  return pa.bottom() - static_cast<int>(t * pa.height());
}

void WaveformMonitorWidget::drawYAxis(QPainter& painter,
                                      const QRect& pa) const {
  const QPalette palette = this->palette();
  const QColor axis_color = palette.color(QPalette::WindowText);
  painter.setPen(axis_color);

  // Axis line.
  painter.drawLine(pa.left(), pa.top(), pa.left(), pa.bottom());

  const QFontMetrics fm(painter.font());
  const double tick_step = 100.0;
  const double first_tick = std::ceil(y_min_mv_ / tick_step) * tick_step;

  for (double mv = first_tick; mv <= y_max_mv_ + 0.5; mv += tick_step) {
    const int py = mvToPixelY(mv, pa);
    if (py < pa.top() || py > pa.bottom()) continue;

    painter.setPen(axis_color);
    painter.drawLine(pa.left() - 4, py, pa.left(), py);

    const QString label = QString::number(static_cast<int>(std::round(mv)));
    const QRect lr(0, py - fm.height() / 2, kLeftMargin - 6, fm.height());
    painter.drawText(lr, Qt::AlignRight | Qt::AlignVCenter, label);
  }

  // Y-axis title.
  painter.save();
  painter.setPen(axis_color);
  painter.translate(12, pa.center().y());
  painter.rotate(-90);
  painter.drawText(QRect(-50, -10, 100, 20), Qt::AlignCenter, "mV");
  painter.restore();
}

void WaveformMonitorWidget::drawGrid(QPainter& painter, const QRect& pa) const {
  const QPalette palette = this->palette();
  painter.setPen(QPen(theme_tokens::gridLine(palette), 1, Qt::SolidLine));

  // Horizontal lines at every 100 mV Y-axis tick position.
  const double y_tick_step = 100.0;
  const double first_y = std::ceil(y_min_mv_ / y_tick_step) * y_tick_step;
  for (double mv = first_y; mv <= y_max_mv_ + 0.5; mv += y_tick_step) {
    const int py = mvToPixelY(mv, pa);
    if (py < pa.top() || py > pa.bottom()) continue;
    painter.drawLine(pa.left(), py, pa.right(), py);
  }

  if (x_samples_ <= 0) return;

  // Vertical lines at the same µs tick positions as the X-axis labels.
  const double start_us = active_video_start_ * us_per_sample_;
  const double end_us = (active_video_start_ + x_samples_) * us_per_sample_;
  const double total_us = end_us - start_us;
  const double raw_step = total_us / 7.0;
  const double mag = std::pow(10.0, std::floor(std::log10(raw_step)));
  double tick_us = mag;
  if (raw_step / mag >= 5.0) {
    tick_us = mag * 5.0;
  } else if (raw_step / mag >= 2.0) {
    tick_us = mag * 2.0;
  }
  const double first_tick = std::ceil(start_us / tick_us) * tick_us;
  for (double t = first_tick; t <= end_us + tick_us * 0.01; t += tick_us) {
    const int x_buf =
        static_cast<int>(std::round(t / us_per_sample_)) - active_video_start_;
    if (x_buf < 0 || x_buf >= x_samples_) continue;
    const int px = pa.left() + x_buf * pa.width() / x_samples_;
    if (px < pa.left() || px > pa.right()) continue;
    painter.drawLine(px, pa.top(), px, pa.bottom());
  }
}

void WaveformMonitorWidget::drawXAxis(QPainter& painter,
                                      const QRect& pa) const {
  const QPalette palette = this->palette();
  const QColor axis_color = palette.color(QPalette::WindowText);
  painter.setPen(axis_color);

  // Axis baseline.
  painter.drawLine(pa.left(), pa.bottom(), pa.right(), pa.bottom());

  if (x_samples_ <= 0) return;

  const QFontMetrics fm(painter.font());

  // Tick positions in µs, snapped to a nice interval for ~7 ticks.
  const double start_us = active_video_start_ * us_per_sample_;
  const double end_us = (active_video_start_ + x_samples_) * us_per_sample_;
  const double total_us = end_us - start_us;
  const double raw_step = total_us / 7.0;
  const double mag = std::pow(10.0, std::floor(std::log10(raw_step)));
  double tick_us = mag;
  if (raw_step / mag >= 5.0) {
    tick_us = mag * 5.0;
  } else if (raw_step / mag >= 2.0) {
    tick_us = mag * 2.0;
  }
  const double first_tick = std::ceil(start_us / tick_us) * tick_us;
  for (double t = first_tick; t <= end_us + tick_us * 0.01; t += tick_us) {
    const int x_buf =
        static_cast<int>(std::round(t / us_per_sample_)) - active_video_start_;
    if (x_buf < 0 || x_buf >= x_samples_) continue;
    const int px = pa.left() + x_buf * pa.width() / x_samples_;
    if (px < pa.left() || px > pa.right()) continue;

    painter.setPen(axis_color);
    painter.drawLine(px, pa.bottom(), px, pa.bottom() + 4);

    const QString label = QString::number(t, 'f', 1);
    const int label_w = fm.horizontalAdvance(label);
    const QRect lr(px - label_w / 2, pa.bottom() + 5, label_w + 2, fm.height());
    painter.drawText(lr, Qt::AlignHCenter | Qt::AlignTop, label);
  }

  // X-axis title centered below the tick labels.
  const QString title = QString::fromUtf8("Time (µs)");
  const int title_y = pa.bottom() + 5 + fm.height() + 2;
  painter.drawText(QRect(pa.left(), title_y, pa.width(), fm.height()),
                   Qt::AlignHCenter | Qt::AlignTop, title);
}

void WaveformMonitorWidget::drawLevelMarkers(QPainter& painter,
                                             const QRect& pa) const {
  if (!video_params_.has_value()) return;
  const auto& vp = *video_params_;
  if (vp.blanking_level < 0 || vp.white_level <= vp.blanking_level) return;

  const double amv = active_video_mv(vp.system);

  struct LevelSpec {
    int32_t raw;
    const char* label;
    Qt::PenStyle style;
    qreal alpha;
  };

  const LevelSpec levels[] = {
      {vp.sync_tip_level, "Sync tip", Qt::DashLine, 0.60},
      {vp.blanking_level, "Blanking (0 mV)", Qt::DashLine, 0.35},
      {vp.black_level, "Black", Qt::DashDotLine, 0.50},
      {vp.white_level, "White", Qt::DashLine, 0.70},
      {vp.peak_level, "Peak", Qt::DashLine, 0.55},
  };

  const QFontMetrics fm(painter.font());

  for (const auto& lv : levels) {
    if (lv.raw < 0) continue;

    const double mv = cvbs_sample_to_mv(static_cast<int16_t>(lv.raw),
                                        vp.blanking_level, vp.white_level, amv);
    const int py = mvToPixelY(mv, pa);
    if (py < pa.top() || py > pa.bottom()) continue;

    const QColor lc = theme_tokens::neutralLine(palette(), lv.alpha);
    painter.setPen(QPen(lc, 1, lv.style));
    painter.drawLine(pa.left(), py, pa.right(), py);

    // Label inside the plot area near the left edge — matches PlotWidget
    // marker label placement (scenePos + QPointF(5, -5)).
    painter.setPen(lc);
    painter.drawText(pa.left() + 5, py - 3, QString::fromUtf8(lv.label));
  }
}
