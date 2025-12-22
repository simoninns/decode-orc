/*
 * File:        orcgraphicsview.cpp
 * Module:      orc-gui
 * Purpose:     Custom QtNodes view with validated deletion
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "orcgraphicsview.h"
#include "orcgraphicsscene.h"
#include "orcgraphmodel.h"
#include "logging.h"
#include "../core/include/project.h"
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QKeySequence>
#include <QAction>
#include <QMessageBox>

OrcGraphicsView::OrcGraphicsView(QWidget* parent)
    : QtNodes::GraphicsView(parent)
{
    // Find and disconnect the default delete action
    for (QAction* action : actions()) {
        if (action->shortcut() == QKeySequence::Delete) {
            // Disconnect all connections from this action
            disconnect(action, nullptr, nullptr, nullptr);
            // Connect to our custom handler
            connect(action, &QAction::triggered, this, &OrcGraphicsView::onDeleteSelectedObjects);
            break;
        }
    }
}

void OrcGraphicsView::onDeleteSelectedObjects()
{
    auto* orc_scene = dynamic_cast<OrcGraphicsScene*>(scene());
    if (!orc_scene) {
        return;
    }
    
    // Check if anything is selected at all
    auto selected_items = scene()->selectedItems();
    if (selected_items.isEmpty()) {
        ORC_LOG_DEBUG("Nothing selected, ignoring delete request");
        return;
    }
    
    auto& graph_model = dynamic_cast<OrcGraphModel&>(orc_scene->graphModel());
    const auto& project = graph_model.project();
    
    // Check if any selected nodes have connections
    std::vector<std::string> cannot_delete;
    bool has_selected_nodes = false;
    
    for (QGraphicsItem* item : selected_items) {
        auto* node_graphics = dynamic_cast<QtNodes::NodeGraphicsObject*>(item);
        if (node_graphics) {
            has_selected_nodes = true;
            QtNodes::NodeId qt_node_id = node_graphics->nodeId();
            std::string orc_node_id = graph_model.getOrcNodeId(qt_node_id);
            
            ORC_LOG_DEBUG("Delete check: QtNode {} -> ORC node '{}'", qt_node_id, orc_node_id);
            
            if (!orc_node_id.empty()) {
                std::string reason;
                if (!orc::project_io::can_remove_node(project, orc_node_id, &reason)) {
                    ORC_LOG_DEBUG("Cannot delete '{}': {}", orc_node_id, reason);
                    cannot_delete.push_back(orc_node_id);
                }
            }
        }
    }
    
    if (!cannot_delete.empty()) {
        // Prevent deletion - show message with node IDs
        QString msg = "Cannot delete node";
        if (cannot_delete.size() > 1) {
            msg += "s";
        }
        msg += " with connections (";
        for (size_t i = 0; i < cannot_delete.size(); ++i) {
            if (i > 0) msg += ", ";
            msg += QString::fromStdString(cannot_delete[i]);
        }
        msg += "). Disconnect all edges first.";
        
        QMessageBox::warning(this, "Cannot Delete Node", msg);
        return;  // Don't proceed with deletion
    }
    
    // All checks passed - call parent implementation
    ORC_LOG_DEBUG("All validation passed, calling parent onDeleteSelectedObjects");
    QtNodes::GraphicsView::onDeleteSelectedObjects();
}
