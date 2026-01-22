/*
 * File:        dropoutanalysisdialog.h
 * Module:      orc-gui
 * Purpose:     Dropout analysis dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DROPOUTANALYSISDIALOG_H
#define DROPOUTANALYSISDIALOG_H

#include <QCheckBox>
#include <QVBoxLayout>
#include <QVector>
#include <QPointF>
#include "analysisdialogbase.h"
#include "plotwidget.h"
#include "../core/stages/dropout_analysis_sink/dropout_analysis_types.h"

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
class DropoutAnalysisDialog : public AnalysisDialogBase
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

protected:
    /**
     * @brief Calculate and set marker position (implements base class pure virtual)
     */
    void calculateMarkerPosition(int32_t frameNumber) override;

private slots:
    void onPlotAreaChanged();

private:
    void removeChartContents();
    
    PlotWidget *plot_;
    PlotSeries *series_;
    PlotMarker *plotMarker_;
    
    double maxY_;
    int32_t numberOfFrames_;
    QVector<QPointF> points_;
};

#endif // DROPOUTANALYSISDIALOG_H
