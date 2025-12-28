/*
 * File:        burstlevelanalysisdialog.cpp
 * Module:      orc-gui
 * Purpose:     Burst level analysis dialog implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "burstlevelanalysisdialog.h"
#include <QPen>
#include <QLabel>
#include <QStackedLayout>
#include <QtMath>
#include <cmath>

BurstLevelAnalysisDialog::BurstLevelAnalysisDialog(QWidget *parent)
    : QDialog(parent)
    , plot_(nullptr)
    , burstSeries_(nullptr)
    , plotMarker_(nullptr)
    , noDataLabel_(nullptr)
    , maxY_(0.0)
    , minY_(0.0)
    , numberOfFrames_(0)
    , updateTimer_(nullptr)
    , pendingFrameNumber_(0)
    , hasPendingUpdate_(false)
{
    setWindowTitle("Burst Level Analysis");
    setWindowFlags(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose, false);
    
    // Create main layout
    auto *mainLayout = new QVBoxLayout(this);
    
    // Create plot widget
    plot_ = new PlotWidget(this);
    plot_->updateTheme();
    
    // Create "No data available" label (initially hidden)
    noDataLabel_ = new QLabel("No data available", this);
    noDataLabel_->setAlignment(Qt::AlignCenter);
    QFont font = noDataLabel_->font();
    font.setPointSize(14);
    noDataLabel_->setFont(font);
    
    // Use a stacked layout to overlay label on plot
    auto *plotContainer = new QWidget(this);
    auto *plotLayout = new QStackedLayout(plotContainer);
    plotLayout->setStackingMode(QStackedLayout::StackAll);
    plotLayout->addWidget(plot_);
    plotLayout->addWidget(noDataLabel_);
    
    mainLayout->addWidget(plotContainer);
    
    // Start with plot visible, label hidden
    noDataLabel_->hide();
    
    // Set up series for Burst Level
    burstSeries_ = plot_->addSeries("Burst Level");
    burstSeries_->setPen(QPen(Qt::yellow, 2));
    burstSeries_->setStyle(PlotSeries::Lines);
    
    // Set up frame marker
    plotMarker_ = plot_->addMarker();
    plotMarker_->setStyle(PlotMarker::VLine);
    plotMarker_->setPen(QPen(Qt::blue, 2));
    
    // Set up update throttling timer
    updateTimer_ = new QTimer(this);
    updateTimer_->setSingleShot(true);
    updateTimer_->setInterval(16); // ~60fps max update rate
    connect(updateTimer_, &QTimer::timeout, this, &BurstLevelAnalysisDialog::onUpdateTimerTimeout);
    
    // Connect to plot area changed signal
    connect(plot_, &PlotWidget::plotAreaChanged, this, &BurstLevelAnalysisDialog::onPlotAreaChanged);
    
    // Set default size
    resize(800, 600);
}

BurstLevelAnalysisDialog::~BurstLevelAnalysisDialog()
{
    removeChartContents();
}

void BurstLevelAnalysisDialog::startUpdate(int32_t numberOfFrames)
{
    removeChartContents();
    numberOfFrames_ = numberOfFrames;
    burstPoints_.reserve(numberOfFrames);
    
    // Hide the "No data available" label and show the plot
    if (noDataLabel_) {
        noDataLabel_->hide();
    }
    plot_->show();
}

void BurstLevelAnalysisDialog::removeChartContents()
{
    maxY_ = 0.0;
    minY_ = 100.0; // Initialize to high value for burst levels
    burstPoints_.clear();
    plot_->replot();
}

void BurstLevelAnalysisDialog::addDataPoint(int32_t frameNumber, double burstLevel)
{
    // Add burst level point if valid
    if (!std::isnan(burstLevel)) {
        burstPoints_.append(QPointF(static_cast<qreal>(frameNumber), static_cast<qreal>(burstLevel)));
        
        // Keep track of the maximum and minimum Y values
        if (burstLevel > maxY_) {
            maxY_ = burstLevel;
        }
        if (burstLevel < minY_) {
            minY_ = burstLevel;
        }
    }
}

void BurstLevelAnalysisDialog::finishUpdate(int32_t currentFrameNumber)
{
    // Set up plot properties
    plot_->updateTheme(); // Auto-detect theme and set appropriate background
    plot_->setGridEnabled(true);
    plot_->setZoomEnabled(true);
    plot_->setPanEnabled(true);
    plot_->setYAxisIntegerLabels(false); // Burst level values are decimal
    
    // Set axis titles and ranges
    plot_->setAxisTitle(Qt::Horizontal, "Frame number");
    plot_->setAxisTitle(Qt::Vertical, "Burst Level (IRE)");
    plot_->setAxisRange(Qt::Horizontal, 0, numberOfFrames_);
    
    // Calculate appropriate Y-axis range
    // Burst levels are typically around 20 IRE for NTSC, 21.5 IRE for PAL
    // Allow for some variation (e.g., 10-40 IRE range)
    double yMax = 40.0;  // Default max
    double yMin = 0.0;   // Default min
    
    // If we have data, adjust the range to show it better
    if (!burstPoints_.isEmpty()) {
        yMax = ceil(maxY_ + 5);   // Add padding above
        yMin = floor(minY_ - 5);  // Add padding below
        if (yMin < 0) yMin = 0;
        if (yMax < 30) yMax = 30; // Ensure minimum range
    }
    
    plot_->setAxisRange(Qt::Vertical, yMin, yMax);
    
    // Set the data for the series with theme-aware colors
    if (!burstPoints_.isEmpty()) {
        QColor burstColor = PlotWidget::isDarkTheme() ? Qt::yellow : QColor(180, 140, 0); // Dark gold for light theme
        burstSeries_->setPen(QPen(burstColor, 2));
        burstSeries_->setData(burstPoints_);
        burstSeries_->setVisible(true);
    }
    
    // Set the frame marker position
    plotMarker_->setPosition(QPointF(static_cast<double>(currentFrameNumber), (yMax + yMin) / 2));
    
    // Render the plot
    plot_->replot();
}

void BurstLevelAnalysisDialog::updateFrameMarker(int32_t currentFrameNumber)
{
    // Always store the pending frame number
    pendingFrameNumber_ = currentFrameNumber;
    hasPendingUpdate_ = true;
    
    // Skip timer start if dialog is not visible - update will happen on show
    if (!isVisible()) return;
    
    // Start or restart the timer
    if (!updateTimer_->isActive()) {
        updateTimer_->start();
    }
}

void BurstLevelAnalysisDialog::showNoDataMessage(const QString& reason)
{
    removeChartContents();
    
    // Hide the plot and show the "No data available" label
    plot_->hide();
    if (noDataLabel_) {
        QString message = reason.isEmpty() ? "No data available" : reason;
        noDataLabel_->setText(message);
        noDataLabel_->show();
    }
}

void BurstLevelAnalysisDialog::onUpdateTimerTimeout()
{
    if (!hasPendingUpdate_) return;
    
    // Calculate the Y position for the marker (middle of the visible range)
    double yMax = 40.0;
    double yMin = 0.0;
    
    if (!burstPoints_.isEmpty()) {
        yMax = ceil(maxY_ + 5);
        yMin = floor(minY_ - 5);
        if (yMin < 0) yMin = 0;
        if (yMax < 30) yMax = 30;
    }
    
    plotMarker_->setPosition(QPointF(static_cast<double>(pendingFrameNumber_), (yMax + yMin) / 2));
    // No need to call plot->replot() - marker update() handles the redraw
    
    hasPendingUpdate_ = false;
}

void BurstLevelAnalysisDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    
    // Force immediate marker update if we have a pending position
    if (hasPendingUpdate_) {
        onUpdateTimerTimeout();
    }
}

void BurstLevelAnalysisDialog::onPlotAreaChanged()
{
    // Handle plot area changes if needed
    // The PlotWidget handles zoom/pan internally
}
