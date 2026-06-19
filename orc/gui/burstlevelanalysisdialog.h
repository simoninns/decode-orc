/*
 * File:        burstlevelanalysisdialog.h
 * Module:      orc-gui
 * Purpose:     Burst level analysis dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef BURSTLEVELANALYSISDIALOG_H
#define BURSTLEVELANALYSISDIALOG_H

#include <amplitude_conversion.h>
#include <common_types.h>

#include <QPointF>
#include <QVBoxLayout>
#include <QVector>
#include <optional>

#include "analysisdialogbase.h"
#include "plotwidget.h"
#include "presenters/include/hints_view_models.h"

/**
 * @brief Dialog for displaying burst level analysis graphs
 *
 * This dialog shows graphs of color burst median IRE levels across all fields
 * in the source. This is useful for tracking signal strength variations and
 * detecting tape/capture issues.
 *
 * Data and business logic is handled by the BurstLevelObserver and
 * BurstLevelAnalysisDecoder in orc-core. This GUI component only handles
 * rendering the graphs.
 */
class BurstLevelAnalysisDialog : public AnalysisDialogBase {
  Q_OBJECT

 public:
  explicit BurstLevelAnalysisDialog(QWidget* parent = nullptr);
  ~BurstLevelAnalysisDialog();

  /**
   * @brief Start a new update cycle
   * @param numberOfFrames Total number of frames in the source
   */
  void startUpdate(int32_t numberOfFrames);

  /**
   * @brief Add a data point to the graph
   * @param frameNumber Frame number (1-based)
   * @param burstLevel10bit Burst level in 10-bit sample domain
   * @param video_params Optional video parameters (cached when provided)
   */
  void addDataPoint(int32_t frameNumber, double burstLevel10bit,
                    const std::optional<orc::presenters::VideoParametersView>&
                        video_params = std::nullopt);

  /**
   * @brief Update the amplitude display unit and replot
   */
  void setAmplitudeUnit(orc::AmplitudeDisplayUnit unit);

  /**
   * @brief Finish the update and render the graph
   * @param currentFrameNumber Current frame being viewed
   */
  void finishUpdate(int32_t currentFrameNumber);

  /**
   * @brief Update the frame marker position
   * @param currentFrameNumber Current frame being viewed
   */
  void updateFrameMarker(int32_t currentFrameNumber);

  /**
   * @brief Show "No data available" message
   * @param reason Optional explanation for why no data is available
   */
  void showNoDataMessage(const QString& reason = QString());

 protected:
  /**
   * @brief Calculate and set marker position (implements base class pure
   * virtual)
   */
  void calculateMarkerPosition(int32_t frameNumber) override;

 private slots:
  void onPlotAreaChanged();

 private:
  void removeChartContents();

  PlotWidget* plot_;
  PlotSeries* burstSeries_;
  PlotMarker* plotMarker_;

  double maxY_;
  double minY_;
  int32_t numberOfFrames_;
  int32_t current_frame_number_{1};
  QVector<QPointF> burstPoints_;
  orc::AmplitudeDisplayUnit amplitude_unit_ = orc::AmplitudeDisplayUnit::IRE;
  std::optional<orc::presenters::VideoParametersView> cached_video_params_;
  double display_y_min_{0.0};
  double display_y_max_{40.0};
};

#endif  // BURSTLEVELANALYSISDIALOG_H
