/*
 * File:        burstlevelanalysisdialog.cpp
 * Module:      orc-gui
 * Purpose:     Burst level analysis dialog implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "burstlevelanalysisdialog.h"

#include <QLabel>
#include <QPen>
#include <QStackedLayout>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

static orc::VideoSystem toOrcVideoSystem(orc::presenters::VideoSystem sys) {
  switch (sys) {
    case orc::presenters::VideoSystem::NTSC:
      return orc::VideoSystem::NTSC;
    case orc::presenters::VideoSystem::PAL_M:
      return orc::VideoSystem::PAL_M;
    default:
      return orc::VideoSystem::PAL;
  }
}

BurstLevelAnalysisDialog::BurstLevelAnalysisDialog(QWidget* parent)
    : AnalysisDialogBase(parent),
      plot_(nullptr),
      burstSeries_(nullptr),
      plotMarker_(nullptr),
      maxY_(0.0),
      minY_(0.0),
      numberOfFrames_(0) {
  setWindowTitle("Burst Level Analysis");
  setWindowFlags(Qt::Window);
  setAttribute(Qt::WA_DeleteOnClose, false);

  // Create main layout
  auto* mainLayout = new QVBoxLayout(this);

  // Create plot widget
  plot_ = new PlotWidget(this);
  plot_->updateTheme();

  // Set up "No data available" overlay (from base class)
  setupNoDataOverlay(mainLayout, plot_);

  // Set up series for Burst Level
  burstSeries_ = plot_->addSeries("Burst Level");
  burstSeries_->setPen(QPen(Qt::yellow, 2));
  burstSeries_->setStyle(PlotSeries::Lines);

  // Set up frame marker
  plotMarker_ = plot_->addMarker();
  plotMarker_->setStyle(PlotMarker::VLine);
  plotMarker_->setPen(QPen(Qt::blue, 2));

  // Set up update throttling timer (from base class)
  setupUpdateTimer();

  // Connect to plot area changed signal
  connect(plot_, &PlotWidget::plotAreaChanged, this,
          &BurstLevelAnalysisDialog::onPlotAreaChanged);

  // Set default size
  resize(800, 600);
}

BurstLevelAnalysisDialog::~BurstLevelAnalysisDialog() { removeChartContents(); }

void BurstLevelAnalysisDialog::startUpdate(int32_t numberOfFrames) {
  removeChartContents();
  numberOfFrames_ = numberOfFrames;
  burstPoints_.reserve(numberOfFrames);

  // Hide the "No data available" label and show the plot
  if (noDataLabel_) {
    noDataLabel_->hide();
  }
  plot_->show();
}

void BurstLevelAnalysisDialog::removeChartContents() {
  maxY_ = 0.0;
  minY_ = 1023.0;  // Initialize high for 10-bit domain
  burstPoints_.clear();
  plot_->replot();
}

void BurstLevelAnalysisDialog::addDataPoint(
    int32_t frameNumber, double burstLevel10bit,
    const std::optional<orc::presenters::VideoParametersView>& video_params) {
  if (video_params.has_value()) {
    cached_video_params_ = video_params;
  }
  if (!std::isnan(burstLevel10bit)) {
    // Store raw 10-bit value; display conversion happens in finishUpdate().
    burstPoints_.append(QPointF(static_cast<qreal>(frameNumber),
                                static_cast<qreal>(burstLevel10bit)));
    if (burstLevel10bit > maxY_) maxY_ = burstLevel10bit;
    if (burstLevel10bit < minY_) minY_ = burstLevel10bit;
  }
}

void BurstLevelAnalysisDialog::finishUpdate(int32_t currentFrameNumber) {
  current_frame_number_ = currentFrameNumber;

  plot_->updateTheme();
  plot_->setGridEnabled(true);
  plot_->setZoomEnabled(true);
  plot_->setPanEnabled(true);
  plot_->setYAxisIntegerLabels(false);

  plot_->setAxisTitle(Qt::Horizontal, "Frame number");
  plot_->setAxisTitle(Qt::Vertical,
                      QString("Burst Level (%1)")
                          .arg(QString::fromStdString(
                              orc::amplitude_unit_suffix(amplitude_unit_))));

  // Resolve video levels for unit conversion.
  int32_t blanking = 256, white = 844;
  orc::VideoSystem sys = orc::VideoSystem::PAL;
  if (cached_video_params_.has_value()) {
    const auto& vp = *cached_video_params_;
    if (vp.blanking_level >= 0 && vp.white_level > vp.blanking_level) {
      blanking = vp.blanking_level;
      white = vp.white_level;
      sys = toOrcVideoSystem(vp.system);
    }
  }

  // X-axis range from actual data.
  double xMin = 0;
  double xMax = static_cast<double>(numberOfFrames_);
  if (!burstPoints_.isEmpty()) {
    double dataMin = std::numeric_limits<double>::max();
    double dataMax = 0;
    for (const auto& pt : burstPoints_) {
      if (pt.x() < dataMin) dataMin = pt.x();
      if (pt.x() > dataMax) dataMax = pt.x();
    }
    if (dataMax > 0) {
      xMin = std::floor(dataMin);
      xMax = std::ceil(dataMax);
    }
  }
  plot_->setAxisRange(Qt::Horizontal, xMin, xMax);

  double xRange = xMax - xMin;
  double xTickStep = 1.0;
  if (xRange > 0) {
    double idealStep = xRange / 10.0;
    double magnitude = std::pow(10.0, std::floor(std::log10(idealStep)));
    double normalized = idealStep / magnitude;
    if (normalized < 1.5) {
      xTickStep = 1.0 * magnitude;
    } else if (normalized < 3.0) {
      xTickStep = 2.0 * magnitude;
    } else if (normalized < 7.0) {
      xTickStep = 5.0 * magnitude;
    } else {
      xTickStep = 10.0 * magnitude;
    }
  }
  plot_->setAxisTickStep(Qt::Horizontal, xTickStep, 0.0);

  // Convert stored 10-bit Y values to display units.
  QVector<QPointF> displayPoints;
  double yMin = 0.0, yMax = 0.0;

  if (!burstPoints_.isEmpty()) {
    double dispMin = std::numeric_limits<double>::max();
    double dispMax = std::numeric_limits<double>::lowest();
    displayPoints.reserve(burstPoints_.size());
    for (const auto& pt : burstPoints_) {
      double yDisp = orc::samples10_to_display(
          static_cast<int32_t>(pt.y()), blanking, white, sys, amplitude_unit_);
      displayPoints.append(QPointF(pt.x(), yDisp));
      if (yDisp < dispMin) dispMin = yDisp;
      if (yDisp > dispMax) dispMax = yDisp;
    }
    double padding = (dispMax - dispMin) * 0.1;
    if (padding < 1.0) padding = 1.0;
    yMin = std::floor(dispMin - padding);
    yMax = std::ceil(dispMax + padding);
    if (yMin < 0) yMin = 0;
  } else {
    const auto [r_min, r_max] = orc::amplitude_display_range(
        0, blanking, white, 1023, sys, amplitude_unit_);
    yMin = r_min;
    yMax = r_max;
  }

  display_y_min_ = yMin;
  display_y_max_ = yMax;
  plot_->setAxisRange(Qt::Vertical, yMin, yMax);
  plot_->setAxisTickStep(Qt::Vertical,
                         orc::amplitude_major_tick(amplitude_unit_), 0.0);

  if (!displayPoints.isEmpty()) {
    std::sort(displayPoints.begin(), displayPoints.end(),
              [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    QColor burstColor =
        PlotWidget::isDarkTheme() ? Qt::yellow : QColor(180, 140, 0);
    burstSeries_->setPen(QPen(burstColor, 2));
    burstSeries_->setData(displayPoints);
    burstSeries_->setVisible(true);
  }

  plotMarker_->setPosition(QPointF(static_cast<double>(currentFrameNumber),
                                   (display_y_max_ + display_y_min_) / 2));

  plot_->replot();
}

void BurstLevelAnalysisDialog::updateFrameMarker(int32_t currentFrameNumber) {
  // Use base class throttling implementation
  updateFrameMarkerThrottled(currentFrameNumber);
}

void BurstLevelAnalysisDialog::showNoDataMessage(const QString& reason) {
  removeChartContents();

  // Use base class implementation
  showNoDataMessageImpl(reason, plot_);
}

void BurstLevelAnalysisDialog::calculateMarkerPosition(int32_t frameNumber) {
  plotMarker_->setPosition(QPointF(static_cast<double>(frameNumber),
                                   (display_y_max_ + display_y_min_) / 2));
}

void BurstLevelAnalysisDialog::setAmplitudeUnit(
    orc::AmplitudeDisplayUnit unit) {
  if (amplitude_unit_ == unit) return;
  amplitude_unit_ = unit;
  if (!burstPoints_.isEmpty()) {
    finishUpdate(current_frame_number_);
  }
}

void BurstLevelAnalysisDialog::onPlotAreaChanged() {
  // The PlotWidget handles zoom/pan internally.
}
