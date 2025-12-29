/*
 * File:        dropoutanalysisdialog.cpp
 * Module:      orc-gui
 * Purpose:     Dropout analysis dialog implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "dropoutanalysisdialog.h"
#include "logging.h"
#include <QPen>
#include <QLabel>
#include <QStackedLayout>
#include <QtMath>

DropoutAnalysisDialog::DropoutAnalysisDialog(QWidget *parent)
    : AnalysisDialogBase(parent)
    , plot_(nullptr)
    , series_(nullptr)
    , plotMarker_(nullptr)
    , visibleAreaCheckBox_(nullptr)
    , maxY_(0.0)
    , numberOfFrames_(0)
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
    
    // Set up "No data available" overlay (from base class)
    setupNoDataOverlay(mainLayout, plot_);
    
    // Set up series and marker
    series_ = plot_->addSeries("Dropout Length");
    series_->setPen(QPen(Qt::red, 1));
    series_->setStyle(PlotSeries::Bars);
    
    plotMarker_ = plot_->addMarker();
    plotMarker_->setStyle(PlotMarker::VLine);
    plotMarker_->setPen(QPen(Qt::blue, 2));
    
    // Set up update throttling timer (from base class)
    setupUpdateTimer();
    
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
    
    // Clear the series data
    if (series_) {
        series_->setData(QVector<QPointF>());
    }
    
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
    // Use base class throttling implementation
    updateFrameMarkerThrottled(currentFrameNumber);
}

void DropoutAnalysisDialog::showNoDataMessage(const QString& reason)
{
    removeChartContents();
    
    // Use base class implementation
    showNoDataMessageImpl(reason, plot_);
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

void DropoutAnalysisDialog::calculateMarkerPosition(int32_t frameNumber)
{
    double yMax = (maxY_ < 10) ? 10 : ceil(maxY_ + (maxY_ * 0.1));
    plotMarker_->setPosition(QPointF(static_cast<double>(frameNumber), yMax / 2));
    // No need to call plot->replot() - marker update() handles the redraw
}

void DropoutAnalysisDialog::onPlotAreaChanged()
{
    // Handle plot area changes if needed
    // The PlotWidget handles zoom/pan internally
}

    // No need to call plot->replot() - marker update() handles the redraw