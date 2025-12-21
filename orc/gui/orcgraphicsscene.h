/*
 * File:        orcgraphicsscene.h
 * Module:      orc-gui
 * Purpose:     Custom QtNodes scene with context menu support
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include <QtNodes/BasicGraphicsScene>
#include <QMenu>
#include "orcgraphmodel.h"

/**
 * Custom graphics scene that provides context menu for adding nodes
 */
class OrcGraphicsScene : public QtNodes::BasicGraphicsScene
{
    Q_OBJECT

public:
    explicit OrcGraphicsScene(OrcGraphModel& graphModel, QObject* parent = nullptr);
    ~OrcGraphicsScene() override = default;

    QMenu* createSceneMenu(QPointF const scenePos) override;


private:
    OrcGraphModel& graph_model_;
};
