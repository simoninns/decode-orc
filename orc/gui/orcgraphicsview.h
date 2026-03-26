/*
 * File:        orcgraphicsview.h
 * Module:      orc-gui
 * Purpose:     Custom QtNodes view with validated deletion
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <QtNodes/GraphicsView>
#include <QString>

class OrcGraphModel;

/**
 * Custom graphics view that validates node deletion before allowing it
 */
class OrcGraphicsView : public QtNodes::GraphicsView
{
    Q_OBJECT

public:
    explicit OrcGraphicsView(QWidget* parent = nullptr);
    ~OrcGraphicsView() override = default;

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

public:
    void setShowWelcomeMessage(bool show);

private slots:
    void onDeleteSelectedObjects() override;

private:
    bool show_welcome_message_{true};
    QString welcome_message_;
};
