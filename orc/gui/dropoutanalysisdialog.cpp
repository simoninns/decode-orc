/*
 * File:        dropoutanalysisdialog.cpp
 * Module:      orc-gui
 * Purpose:     Dropout analysis dialog implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "dropoutanalysisdialog.h"
#include <QPen>
#include <QLabel>
#include <QStackedLayout>
#include <QtMath>

DropoutAnalysisDialog::DropoutAnalysisDialog(QWidget *parent)
    : QDialog(parent)
    , plot_(nullptr)
    , series_(nullptr)
    , plotMarker_(nullptr)
    , visibleAreaCheckBox_(nullptr)
    , noDataLabel_(nullptr)
    , maxY_(0.0)
    , numberOfFrames_(0)
    , updateTimer_(nullptr)
    , pendingFrameNumber_(0)
    , hasPendingUpdate_(false)
{
    setWindowTitle("Dropout Analysis");
    setWindowFlags(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose, false);
    
    // Create main layout
    auto *mainLayout = new QVBoxLayout(this);
    
    // Create checkbox for visible area mode
    visibleAreaCheckBox_ = new QCheckBox("Visible Area Only", this);
    visibleAreaCheckBox_->setToolTip("When checked, only counts dropouts in the visible/active video area");
    connect(visibleAreaCheckBox_, &QCheckBox::toggled, this, &DropoutAnalysisDialog::onVisibleAreaCheckBoxToggled);
    mainLayout->addWidget(visibleAreaCheckBox_);
    
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
    
    // Set up series and marker
    series_ = plot_->addSeries("Dropout Length");
    series_->setPen(QPen(Qt::red, 1));
    series_->setStyle(PlotSeries::Bars);
    
    plotMarker_ = plot_->addMarker();
    plotMarker_->setStyle(PlotMarker::VLine);
    plotMarker_->setPen(QPen(Qt::blue, 2));
    
    // Set up update throttling timer
    updateTimer_ = new QTimer(this);
    updateTimer_->setSingleShot(true);
    updateTimer_->setInterval(16); // ~60fps max update rate
    connect(updateTimer_, &QTimer::timeout, this, &DropoutAnalysisDialog::onUpdateTimerTimeout);
    
    // Connect to plot area changed signal
    connect(plot_, &PlotWidget::plotAreaChanged, this, &DropoutAnalysisDialog::onPlotAreaChanged);
    
    // Set default size
    resize(800, 600);
}

DropoutAnalysisDialog::~DropoutAnalysisDialog()
{
    removeChartContents();
}

void DropoutAnalysisDialog::startUpdate(int32_t numberOfFrames)
{
    removeChartContents();
    numberOfFrames_ = numberOfFrames;
    points_.reserve(numberOfFrames);
    
    // Hide the "No data available" label and show the plot
    if (noDataLabel_) {
        noDataLabel_->hide();
    }
    plot_->show();
}

void DropoutAnalysisDialog::removeChartContents()
{
    maxY_ = 0.0;
    points_.clear();
    plot_->replot();
}

void DropoutAnalysisDialog::addDataPoint(int32_t frameNumber, double dropoutLength)
{
    points_.append(QPointF(static_cast<qreal>(frameNumber), static_cast<qreal>(dropoutLength)));
    
    // Keep track of the maximum Y value
    if (dropoutLength > maxY_) {
        maxY_ = dropoutLength;
    }
}

void DropoutAnalysisDialog::finishUpdate(int32_t currentFrameNumber)
{
    // Set up plot properties
    plot_->updateTheme(); // Auto-detect theme and set appropriate background
    plot_->setGridEnabled(true);
    plot_->setZoomEnabled(true);
    plot_->setPanEnabled(true);
    plot_->setYAxisIntegerLabels(true); // Dropouts should be whole numbers
    
    // Set axis titles and ranges
    plot_->setAxisTitle(Qt::Horizontal, "Frame number");
    plot_->setAxisTitle(Qt::Vertical, "Dropout length (in samples)");
    plot_->setAxisRange(Qt::Horizontal, 0, numberOfFrames_);
    
    // Calculate appropriate Y-axis range (dropout lengths should always be >= 0)
    // Round to whole numbers since fractions of dropouts aren't meaningful
    double yMax = (maxY_ < 10) ? 10 : ceil(maxY_ + (maxY_ * 0.1)); // Add 10% padding and round up
    plot_->setAxisRange(Qt::Vertical, 0, yMax);
    
    // Set the dropout curve data with theme-aware color
    QColor dataColor = PlotWidget::isDarkTheme() ? Qt::yellow : Qt::darkMagenta;
    series_->setPen(QPen(dataColor, 2));
    series_->setData(points_);
    
    // Set the frame marker position
    plotMarker_->setPosition(QPointF(static_cast<double>(currentFrameNumber), yMax / 2));
    
    // Render the plot
    plot_->replot();
}

void DropoutAnalysisDialog::updateFrameMarker(int32_t currentFrameNumber)
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

void DropoutAnalysisDialog::showNoDataMessage(const QString& reason)
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

orc::DropoutAnalysisMode DropoutAnalysisDialog::getCurrentMode() const
{
    return visibleAreaCheckBox_->isChecked() ? 
        orc::DropoutAnalysisMode::VISIBLE_AREA : 
        orc::DropoutAnalysisMode::FULL_FIELD;
}

void DropoutAnalysisDialog::onVisibleAreaCheckBoxToggled(bool checked)
{
    orc::DropoutAnalysisMode mode = checked ? 
        orc::DropoutAnalysisMode::VISIBLE_AREA : 
        orc::DropoutAnalysisMode::FULL_FIELD;
    
    emit modeChanged(mode);
}

void DropoutAnalysisDialog::onUpdateTimerTimeout()
{
    if (!hasPendingUpdate_) return;
    
    double yMax = (maxY_ < 10) ? 10 : ceil(maxY_ + (maxY_ * 0.1));
    plotMarker_->setPosition(QPointF(static_cast<double>(pendingFrameNumber_), yMax / 2));
    // No need to call plot->replot() - marker update() handles the redraw
    
    hasPendingUpdate_ = false;
}

void DropoutAnalysisDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    
    // Force immediate marker update if we have a pending position
    if (hasPendingUpdate_) {
        onUpdateTimerTimeout();
    }
}

void DropoutAnalysisDialog::onPlotAreaChanged()
{
    // Handle plot area changes if needed
    // The PlotWidget handles zoom/pan internally
}
