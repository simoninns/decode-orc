/*
 * File:        dropoutanalysisdialog.h
 * Module:      orc-gui
 * Purpose:     Dropout analysis dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef DROPOUTANALYSISDIALOG_H
#define DROPOUTANALYSISDIALOG_H

#include <QDialog>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QTimer>
#include <QShowEvent>
#include <QVector>
#include <QPointF>
#include "plotwidget.h"
#include "../core/include/dropout_analysis_observer.h"

/**
 * @brief Dialog for displaying dropout analysis graphs
 * 
 * This dialog shows a graph of dropout length across all fields in the source,
 * with options to view either:
 * - Full field dropout data
 * - Visible area only dropout data
 * 
 * Data and business logic is handled by the DropoutAnalysisObserver in orc-core.
 * This GUI component only handles rendering the graph.
 */
class DropoutAnalysisDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DropoutAnalysisDialog(QWidget *parent = nullptr);
    ~DropoutAnalysisDialog();

    /**
     * @brief Start a new update cycle
     * @param numberOfFrames Total number of frames in the source
     */
    void startUpdate(int32_t numberOfFrames);
    
    /**
     * @brief Add a data point to the graph
     * @param frameNumber Frame number (1-based)
     * @param dropoutLength Total dropout length in samples
     */
    void addDataPoint(int32_t frameNumber, double dropoutLength);
    
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
    
    /**
     * @brief Get the current analysis mode
     */
    orc::DropoutAnalysisMode getCurrentMode() const;

signals:
    /**
     * @brief Emitted when the user changes the analysis mode
     * @param mode New analysis mode
     */
    void modeChanged(orc::DropoutAnalysisMode mode);

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onVisibleAreaCheckBoxToggled(bool checked);
    void onUpdateTimerTimeout();
    void onPlotAreaChanged();

private:
    void removeChartContents();
    
    PlotWidget *plot_;
    PlotSeries *series_;
    PlotMarker *plotMarker_;
    QCheckBox *visibleAreaCheckBox_;
    QLabel *noDataLabel_;
    
    double maxY_;
    int32_t numberOfFrames_;
    QVector<QPointF> points_;
    
    // Update throttling
    QTimer *updateTimer_;
    int32_t pendingFrameNumber_;
    bool hasPendingUpdate_;
};

#endif // DROPOUTANALYSISDIALOG_H
