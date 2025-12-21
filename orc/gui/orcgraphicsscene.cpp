/*
 * File:        orcgraphicsscene.cpp
 * Module:      orc-gui
 * Purpose:     Custom QtNodes scene with context menu support
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "orcgraphicsscene.h"
#include "../core/include/node_type.h"

OrcGraphicsScene::OrcGraphicsScene(OrcGraphModel& graphModel, QObject* parent)
    : QtNodes::BasicGraphicsScene(graphModel, parent)
    , graph_model_(graphModel)
{
}

QMenu* OrcGraphicsScene::createSceneMenu(QPointF const scenePos)
{
    QMenu* menu = new QMenu();
    
    // Add Node submenu
    QMenu* add_node_menu = menu->addMenu("Add Node");
    const auto& all_types = orc::get_all_node_types();
    
    for (const auto& type_info : all_types) {
        QString display_name = QString::fromStdString(type_info.display_name);
        QString tooltip = QString::fromStdString(type_info.description);
        
        auto* action = add_node_menu->addAction(display_name, [this, scenePos, stage_name = type_info.stage_name]() {
            // Add node at clicked position
            QtNodes::NodeId nodeId = graph_model_.addNode(QString::fromStdString(stage_name));
            if (nodeId != QtNodes::InvalidNodeId) {
                graph_model_.setNodeData(nodeId, QtNodes::NodeRole::Position, scenePos);
            }
        });
        action->setToolTip(tooltip);
    }
    
    menu->setAttribute(Qt::WA_DeleteOnClose);
    return menu;
}
