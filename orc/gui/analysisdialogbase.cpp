/*
 * File:        analysisdialogbase.cpp
 * Module:      orc-gui
 * Purpose:     Base class for analysis dialogs implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "analysisdialogbase.h"
#include <QStackedLayout>
#include <QFont>

AnalysisDialogBase::AnalysisDialogBase(QWidget *parent)
    : QDialog(parent)
    , noDataLabel_(nullptr)
    , updateTimer_(nullptr)
    , pendingFrameNumber_(0)
    , hasPendingUpdate_(false)
{
    // Base initialization only - derived classes handle specific setup
}

void AnalysisDialogBase::setupUpdateTimer()
{
    updateTimer_ = new QTimer(this);
    updateTimer_->setSingleShot(true);
    updateTimer_->setInterval(16); // ~60fps max update rate
    connect(updateTimer_, &QTimer::timeout, this, &AnalysisDialogBase::onUpdateTimerTimeout);
}

void AnalysisDialogBase::setupNoDataOverlay(QVBoxLayout *mainLayout, PlotWidget *plot)
{
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
    plotLayout->addWidget(plot);
    plotLayout->addWidget(noDataLabel_);
    
    mainLayout->addWidget(plotContainer);
    
    // Start with plot visible, label hidden
    noDataLabel_->hide();
}

void AnalysisDialogBase::updateFrameMarkerThrottled(int32_t currentFrameNumber)
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

void AnalysisDialogBase::showNoDataMessageImpl(const QString& reason, PlotWidget *plot)
{
    // Hide the plot and show the "No data available" label
    plot->hide();
    if (noDataLabel_) {
        QString message = reason.isEmpty() ? "No data available" : reason;
        noDataLabel_->setText(message);
        noDataLabel_->show();
    }
}

void AnalysisDialogBase::onUpdateTimerTimeout()
{
    if (!hasPendingUpdate_) return;
    
    // Let derived class calculate and set marker position
    calculateMarkerPosition(pendingFrameNumber_);
    
    hasPendingUpdate_ = false;
}

void AnalysisDialogBase::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    
    // Force immediate marker update if we have a pending position
    if (hasPendingUpdate_) {
        onUpdateTimerTimeout();
    }
}
