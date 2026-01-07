/*
 * File:        orcgraphicsscene.h
 * Module:      orc-gui
 * Purpose:     Custom QtNodes scene with context menu support
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
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
 * @brief Custom QtNodes graphics scene with DAG-specific context menus
 * 
 * Extends QtNodes::BasicGraphicsScene to provide:
 * - Context menus for adding new nodes
 * - Node selection signals
 * - Stage inspection, triggering, and analysis integration
 * 
 * Manages the visual representation of the processing DAG and handles
 * user interactions for node manipulation.
 */
class OrcGraphicsScene : public QtNodes::BasicGraphicsScene
{
    Q_OBJECT

public:
    /**
     * @brief Construct a new graphics scene
     * @param graphModel The DAG model to visualize
     * @param parent Parent QObject
     */
    explicit OrcGraphicsScene(OrcGraphModel& graphModel, QObject* parent = nullptr);
    ~OrcGraphicsScene() override = default;

    /**
     * @brief Create context menu for scene background
     * @param scenePos Position where menu was requested
     * @return Context menu with node creation options
     */
    QMenu* createSceneMenu(QPointF const scenePos) override;

signals:
    void nodeSelected(QtNodes::NodeId nodeId);  ///< Emitted when a node is selected
    void editParametersRequested(const NodeID& node_id);  ///< Emitted when user wants to edit node parameters
    void triggerStageRequested(const NodeID& node_id);  ///< Emitted when user wants to trigger a stage
    void inspectStageRequested(const NodeID& node_id);  ///< Emitted when user wants to inspect a stage
    
    /**
     * @brief Emitted when user requests to run an analysis tool on a node
     * @param tool The analysis tool to run
     * @param node_id Node to analyze
     * @param stage_name Stage type name
     */
    void runAnalysisRequested(orc::AnalysisTool* tool, const NodeID& node_id, const std::string& stage_name);

private slots:
    void onSelectionChanged();
    void onNodeContextMenu(QtNodes::NodeId nodeId, QPointF const pos);

private:
    OrcGraphModel& graph_model_;
};
