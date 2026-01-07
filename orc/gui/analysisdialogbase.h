/*
 * File:        analysisdialogbase.h
 * Module:      orc-gui
 * Purpose:     Base class for analysis dialogs
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ANALYSISDIALOGBASE_H
#define ANALYSISDIALOGBASE_H

#include <QDialog>
#include <QTimer>
#include <QLabel>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QStackedLayout>
#include "plotwidget.h"

/**
 * @brief Base class for analysis dialogs with common update throttling and UI patterns
 * 
 * This base class provides:
 * - Update throttling timer (~60fps max update rate)
 * - "No data available" overlay pattern
 * - Frame marker update handling
 * - Show event handling for deferred updates
 */
class AnalysisDialogBase : public QDialog
{
    Q_OBJECT

protected:
    explicit AnalysisDialogBase(QWidget *parent = nullptr);
    virtual ~AnalysisDialogBase() = default;

    /**
     * @brief Set up the update throttling timer
     * Should be called in derived class constructor after this base constructor
     */
    void setupUpdateTimer();

    /**
     * @brief Set up the "No data available" overlay
     * @param mainLayout The main layout to add the plot container to
     * @param plot The plot widget to overlay the label on
     */
    void setupNoDataOverlay(QVBoxLayout *mainLayout, PlotWidget *plot);

    /**
     * @brief Update the frame marker position (with throttling)
     * @param currentFrameNumber Current frame being viewed
     */
    void updateFrameMarkerThrottled(int32_t currentFrameNumber);

    /**
     * @brief Show "No data available" message
     * @param reason Optional explanation for why no data is available
     * @param plot The plot widget to hide
     */
    void showNoDataMessageImpl(const QString& reason, PlotWidget *plot);

    /**
     * @brief Handle show event - triggers deferred updates
     */
    void showEvent(QShowEvent *event) override;

    /**
     * @brief Pure virtual method for derived classes to implement marker position calculation
     * Called by onUpdateTimerTimeout when update is ready
     */
    virtual void calculateMarkerPosition(int32_t frameNumber) = 0;

protected slots:
    /**
     * @brief Timer timeout handler - calls calculateMarkerPosition
     */
    void onUpdateTimerTimeout();

protected:
    // Common UI elements
    QLabel *noDataLabel_;
    
    // Update throttling state
    QTimer *updateTimer_;
    int32_t pendingFrameNumber_;
    bool hasPendingUpdate_;
};

#endif // ANALYSISDIALOGBASE_H
