/*
 * File:        snranalysisdialog.h
 * Module:      orc-gui
 * Purpose:     SNR analysis dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef SNRANALYSISDIALOG_H
#define SNRANALYSISDIALOG_H

#include <QComboBox>
#include <QVBoxLayout>
#include <QVector>
#include <QPointF>
#include "analysisdialogbase.h"
#include "plotwidget.h"
#include "../core/stages/snr_analysis_sink/snr_analysis_types.h"

/**
 * @brief Dialog for displaying SNR analysis graphs
 * 
 * This dialog shows graphs of SNR (Signal-to-Noise Ratio) across all fields
 * in the source, with options to view:
 * - White SNR only
 * - Black PSNR only
 * - Both white SNR and black PSNR
 * 
 * Data and business logic is handled by the WhiteSNRObserver and BlackPSNRObserver
 * in orc-core. This GUI component only handles rendering the graphs.
 */
class SNRAnalysisDialog : public AnalysisDialogBase
{
    Q_OBJECT

public:
    explicit SNRAnalysisDialog(QWidget *parent = nullptr);
    ~SNRAnalysisDialog();

    /**
     * @brief Start a new update cycle
     * @param numberOfFrames Total number of frames in the source
     */
    void startUpdate(int32_t numberOfFrames);
    
    /**
     * @brief Add a data point to the graphs
     * @param frameNumber Frame number (1-based)
     * @param whiteSNR White SNR value (dB), or NaN if not available
     * @param blackPSNR Black PSNR value (dB), or NaN if not available
     */
    void addDataPoint(int32_t frameNumber, double whiteSNR, double blackPSNR);
    
    /**
     * @brief Finish the update and render the graphs
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
    orc::SNRAnalysisMode getCurrentMode() const;

signals:
    /**
     * @brief Emitted when the user changes the analysis mode
     * @param mode New analysis mode
     */
    void modeChanged(orc::SNRAnalysisMode mode);

protected:
    /**
     * @brief Calculate and set marker position (implements base class pure virtual)
     */
    void calculateMarkerPosition(int32_t frameNumber) override;

private slots:
    void onDisplayModeChanged(int index);
    void onPlotAreaChanged();

private:
    void removeChartContents();
    void updateSeriesVisibility();
    
    PlotWidget *plot_;
    PlotSeries *whiteSNRSeries_;
    PlotSeries *blackPSNRSeries_;
    PlotMarker *plotMarker_;
    QComboBox *displayModeCombo_;
    
    double maxWhiteY_;
    double maxBlackY_;
    int32_t numberOfFrames_;
    QVector<QPointF> whitePoints_;
    QVector<QPointF> blackPoints_;
};

#endif // SNRANALYSISDIALOG_H
