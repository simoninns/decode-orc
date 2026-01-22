/*
 * File:        snranalysisdialog.cpp
 * Module:      orc-gui
 * Purpose:     SNR analysis dialog implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "snranalysisdialog.h"
#include <QPen>
#include <QLabel>
#include <QStackedLayout>
#include <QtMath>
#include <cmath>
#include <limits>

SNRAnalysisDialog::SNRAnalysisDialog(QWidget *parent)
    : AnalysisDialogBase(parent)
    , plot_(nullptr)
    , whiteSNRSeries_(nullptr)
    , blackPSNRSeries_(nullptr)
    , plotMarker_(nullptr)
    , displayModeCombo_(nullptr)
    , maxWhiteY_(0.0)
    , maxBlackY_(0.0)
    , numberOfFrames_(0)
{
    setWindowTitle("SNR Analysis");
    setWindowFlags(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose, false);
    
    // Create main layout
    auto *mainLayout = new QVBoxLayout(this);
    
    // Create display mode combo box
    displayModeCombo_ = new QComboBox(this);
    displayModeCombo_->addItem("White", QVariant::fromValue(orc::SNRAnalysisMode::WHITE));
    displayModeCombo_->addItem("Black", QVariant::fromValue(orc::SNRAnalysisMode::BLACK));
    displayModeCombo_->addItem("Both", QVariant::fromValue(orc::SNRAnalysisMode::BOTH));
    displayModeCombo_->setCurrentIndex(2); // Default to "Both"
    displayModeCombo_->setToolTip("Select which SNR metrics to display");
    connect(displayModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &SNRAnalysisDialog::onDisplayModeChanged);
    mainLayout->addWidget(displayModeCombo_);
    
    // Create plot widget
    plot_ = new PlotWidget(this);
    plot_->updateTheme();
    
    // Set up "No data available" overlay (from base class)
    setupNoDataOverlay(mainLayout, plot_);
    
    // Set up series for White SNR
    whiteSNRSeries_ = plot_->addSeries("White SNR");
    whiteSNRSeries_->setPen(QPen(Qt::green, 2));
    whiteSNRSeries_->setStyle(PlotSeries::Lines);
    
    // Set up series for Black PSNR
    blackPSNRSeries_ = plot_->addSeries("Black PSNR");
    blackPSNRSeries_->setPen(QPen(Qt::cyan, 2));
    blackPSNRSeries_->setStyle(PlotSeries::Lines);
    
    // Set up frame marker
    plotMarker_ = plot_->addMarker();
    plotMarker_->setStyle(PlotMarker::VLine);
    plotMarker_->setPen(QPen(Qt::blue, 2));
    
    // Set up update throttling timer (from base class)
    setupUpdateTimer();
    
    // Connect to plot area changed signal
    connect(plot_, &PlotWidget::plotAreaChanged, this, &SNRAnalysisDialog::onPlotAreaChanged);
    
    // Set default size
    resize(800, 600);
}

SNRAnalysisDialog::~SNRAnalysisDialog()
{
    removeChartContents();
}

void SNRAnalysisDialog::startUpdate(int32_t numberOfFrames)
{
    removeChartContents();
    numberOfFrames_ = numberOfFrames;
    whitePoints_.reserve(numberOfFrames);
    blackPoints_.reserve(numberOfFrames);
    
    // Hide the "No data available" label and show the plot
    if (noDataLabel_) {
        noDataLabel_->hide();
    }
    plot_->show();
}

void SNRAnalysisDialog::removeChartContents()
{
    maxWhiteY_ = 0.0;
    maxBlackY_ = 0.0;
    whitePoints_.clear();
    blackPoints_.clear();
    plot_->replot();
}

void SNRAnalysisDialog::addDataPoint(int32_t frameNumber, double whiteSNR, double blackPSNR)
{
    // Add white SNR point if valid
    if (!std::isnan(whiteSNR)) {
        whitePoints_.append(QPointF(static_cast<qreal>(frameNumber), static_cast<qreal>(whiteSNR)));
        
        // Keep track of the maximum Y value
        if (whiteSNR > maxWhiteY_) {
            maxWhiteY_ = whiteSNR;
        }
    }
    
    // Add black PSNR point if valid
    if (!std::isnan(blackPSNR)) {
        blackPoints_.append(QPointF(static_cast<qreal>(frameNumber), static_cast<qreal>(blackPSNR)));
        
        // Keep track of the maximum Y value
        if (blackPSNR > maxBlackY_) {
            maxBlackY_ = blackPSNR;
        }
    }
}

void SNRAnalysisDialog::finishUpdate(int32_t currentFrameNumber)
{
    // Set up plot properties
    plot_->updateTheme(); // Auto-detect theme and set appropriate background
    plot_->setGridEnabled(true);
    plot_->setZoomEnabled(true);
    plot_->setPanEnabled(true);
    plot_->setYAxisIntegerLabels(false); // SNR values can be decimal
    
    // Set axis titles and ranges
    plot_->setAxisTitle(Qt::Horizontal, "Frame number");
    plot_->setAxisTitle(Qt::Vertical, "SNR (dB)");
    
    // Calculate X-axis range from actual data points
    double xMin = 0;
    double xMax = numberOfFrames_;
    if (!whitePoints_.isEmpty() || !blackPoints_.isEmpty()) {
        xMin = std::numeric_limits<double>::max();
        xMax = std::numeric_limits<double>::lowest();
        
        for (const auto& pt : whitePoints_) {
            xMin = std::min(xMin, pt.x());
            xMax = std::max(xMax, pt.x());
        }
        for (const auto& pt : blackPoints_) {
            xMin = std::min(xMin, pt.x());
            xMax = std::max(xMax, pt.x());
        }
        
        // Round to integers (frame numbers are always whole)
        xMin = std::floor(xMin);
        xMax = std::ceil(xMax);
    }
    plot_->setAxisRange(Qt::Horizontal, xMin, xMax);
    
    // Calculate appropriate Y-axis range
    // SNR values are typically in the range of 20-60 dB
    double maxY = std::max(maxWhiteY_, maxBlackY_);
    double yMax = (maxY < 10) ? 10 : ceil(maxY + 5); // Add some padding
    double yMin = 0; // SNR can theoretically be 0 or negative, but typically positive
    
    // If we have data, adjust the min to show the range better
    if (maxY > 0) {
        double minWhite = whitePoints_.isEmpty() ? yMax : whitePoints_[0].y();
        double minBlack = blackPoints_.isEmpty() ? yMax : blackPoints_[0].y();
        
        for (const auto& pt : whitePoints_) {
            if (pt.y() < minWhite) minWhite = pt.y();
        }
        for (const auto& pt : blackPoints_) {
            if (pt.y() < minBlack) minBlack = pt.y();
        }
        
        double minY = std::min(minWhite, minBlack);
        yMin = floor(minY - 5); // Add some padding below
        if (yMin < 0) yMin = 0;
    }
    
    plot_->setAxisRange(Qt::Vertical, yMin, yMax);
    
    // Set the data for both series with theme-aware colors
    if (!whitePoints_.isEmpty()) {
        QColor whiteColor = PlotWidget::isDarkTheme() ? Qt::green : Qt::darkGreen;
        whiteSNRSeries_->setPen(QPen(whiteColor, 2));
        whiteSNRSeries_->setData(whitePoints_);
    }
    
    if (!blackPoints_.isEmpty()) {
        QColor blackColor = PlotWidget::isDarkTheme() ? Qt::cyan : Qt::darkBlue;
        blackPSNRSeries_->setPen(QPen(blackColor, 2));
        blackPSNRSeries_->setData(blackPoints_);
    }
    
    // Update series visibility based on current mode
    updateSeriesVisibility();
    
    // Set the frame marker position
    plotMarker_->setPosition(QPointF(static_cast<double>(currentFrameNumber), (yMax + yMin) / 2));
    
    // Render the plot
    plot_->replot();
}

void SNRAnalysisDialog::updateFrameMarker(int32_t currentFrameNumber)
{
    // Use base class throttling implementation
    updateFrameMarkerThrottled(currentFrameNumber);
}

void SNRAnalysisDialog::showNoDataMessage(const QString& reason)
{
    removeChartContents();
    
    // Use base class implementation
    showNoDataMessageImpl(reason, plot_);
}

orc::SNRAnalysisMode SNRAnalysisDialog::getCurrentMode() const
{
    return displayModeCombo_->currentData().value<orc::SNRAnalysisMode>();
}

void SNRAnalysisDialog::onDisplayModeChanged(int index)
{
    Q_UNUSED(index);
    
    // Update series visibility
    updateSeriesVisibility();
    
    // Emit mode changed signal
    emit modeChanged(getCurrentMode());
}

void SNRAnalysisDialog::updateSeriesVisibility()
{
    auto mode = getCurrentMode();
    
    switch (mode) {
        case orc::SNRAnalysisMode::WHITE:
            whiteSNRSeries_->setVisible(true);
            blackPSNRSeries_->setVisible(false);
            break;
            
        case orc::SNRAnalysisMode::BLACK:
            whiteSNRSeries_->setVisible(false);
            blackPSNRSeries_->setVisible(true);
            break;
            
        case orc::SNRAnalysisMode::BOTH:
            whiteSNRSeries_->setVisible(true);
            blackPSNRSeries_->setVisible(true);
            break;
    }
    
    plot_->replot();
}

void SNRAnalysisDialog::calculateMarkerPosition(int32_t frameNumber)
{
    // Calculate the Y position for the marker (middle of the visible range)
    double maxY = std::max(maxWhiteY_, maxBlackY_);
    double yMax = (maxY < 10) ? 10 : ceil(maxY + 5);
    double yMin = 0;
    
    if (maxY > 0) {
        double minWhite = whitePoints_.isEmpty() ? yMax : whitePoints_[0].y();
        double minBlack = blackPoints_.isEmpty() ? yMax : blackPoints_[0].y();
        
        for (const auto& pt : whitePoints_) {
            if (pt.y() < minWhite) minWhite = pt.y();
        }
        for (const auto& pt : blackPoints_) {
            if (pt.y() < minBlack) minBlack = pt.y();
        }
        
        double minY = std::min(minWhite, minBlack);
        yMin = floor(minY - 5);
        if (yMin < 0) yMin = 0;
    }
    
    plotMarker_->setPosition(QPointF(static_cast<double>(frameNumber), (yMax + yMin) / 2));
    // No need to call plot->replot() - marker update() handles the redraw
}

void SNRAnalysisDialog::onPlotAreaChanged()
{
    // Handle plot area changes if needed
    // The PlotWidget handles zoom/pan internally
}
