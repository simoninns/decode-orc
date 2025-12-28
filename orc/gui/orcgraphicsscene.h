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
#include "../core/include/node_id.h"

namespace orc {
    class AnalysisTool;
}

using orc::NodeID;  // Make NodeID available for Qt signals/slots without namespace

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

signals:
    void nodeSelected(QtNodes::NodeId nodeId);
    void editParametersRequested(const NodeID& node_id);
    void triggerStageRequested(const NodeID& node_id);
    void inspectStageRequested(const NodeID& node_id);
    void runAnalysisRequested(orc::AnalysisTool* tool, const NodeID& node_id, const std::string& stage_name);

private slots:
    void onSelectionChanged();
    void onNodeContextMenu(QtNodes::NodeId nodeId, QPointF const pos);

private:
    OrcGraphModel& graph_model_;
};
