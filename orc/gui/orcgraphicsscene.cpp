/*
 * File:        orcgraphicsscene.cpp
 * Module:      orc-gui
 * Purpose:     Custom QtNodes scene with context menu support
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "orcgraphicsscene.h"
#include "orcnodepainter.h"
#include "logging.h"
#include "../core/include/node_type.h"
#include "../core/include/project.h"
#include "../core/include/stage_registry.h"
#include "../core/analysis/analysis_registry.h"
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QGraphicsView>
#include <QMessageBox>
#include <QInputDialog>
#include <algorithm>

OrcGraphicsScene::OrcGraphicsScene(OrcGraphModel& graphModel, QObject* parent)
    : QtNodes::BasicGraphicsScene(graphModel, parent)
    , graph_model_(graphModel)
{
    // Set custom node painter that distinguishes "one" vs "many" ports
    setNodePainter(std::make_unique<OrcNodePainter>());
    
    // Connect to scene's selection changed signal
    connect(this, &QGraphicsScene::selectionChanged,
            this, &OrcGraphicsScene::onSelectionChanged);
    
    // Connect to node context menu signal from BasicGraphicsScene
    connect(this, &QtNodes::BasicGraphicsScene::nodeContextMenu,
            this, &OrcGraphicsScene::onNodeContextMenu);
}

void OrcGraphicsScene::onSelectionChanged()
{
    // Get selected items and emit nodeSelected for the first selected node
    auto selected = selectedItems();
    for (auto* item : selected) {
        auto* node_graphics = dynamic_cast<QtNodes::NodeGraphicsObject*>(item);
        if (node_graphics) {
            QtNodes::NodeId nodeId = node_graphics->nodeId();
            Q_EMIT nodeSelected(nodeId);
            return; // Only handle first selected node
        }
    }
}

QMenu* OrcGraphicsScene::createSceneMenu(QPointF const scenePos)
{
    QMenu* menu = new QMenu();
    
    // Check if project has a valid name (indicating it's been created/loaded)
    bool has_project = !graph_model_.project().get_name().empty();
    
    // Add Node submenu
    QMenu* add_node_menu = menu->addMenu("Add Node");
    add_node_menu->setEnabled(has_project);
    
    if (!has_project) {
        add_node_menu->addAction("(No project loaded)")->setEnabled(false);
    } else {
        const auto& all_types = orc::get_all_node_types();
        orc::VideoSystem project_format = graph_model_.project().get_video_format();
        
        for (const auto& type_info : all_types) {
            // Filter stages by video format compatibility
            if (!orc::is_stage_compatible_with_format(type_info.stage_name, project_format)) {
                continue;
            }
            
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
    }
    
    menu->setAttribute(Qt::WA_DeleteOnClose);
    return menu;
}

void OrcGraphicsScene::onNodeContextMenu(QtNodes::NodeId nodeId, QPointF const pos)
{
    ORC_LOG_DEBUG("Node context menu requested for QtNode {}", nodeId);
    
    // Get ORC node ID
    NodeID orc_node_id = graph_model_.getOrcNodeId(nodeId);
    if (!orc_node_id.is_valid()) {
        ORC_LOG_WARN("Could not find ORC node ID for QtNode {}", nodeId);
        return;
    }
    
    ORC_LOG_DEBUG("Showing context menu for ORC node '{}'", orc_node_id);
    
    // Get node info from project
    const auto& nodes = graph_model_.project().get_nodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
        [&orc_node_id](const orc::ProjectDAGNode& n) { return n.node_id == orc_node_id; });
    
    if (node_it == nodes.end()) {
        ORC_LOG_WARN("Could not find node '{}' in project", orc_node_id);
        return;
    }
    
    QString node_label = QString::fromStdString(
        node_it->user_label.empty() ? node_it->display_name : node_it->user_label);
    
    // Get all node capabilities in a single call
    auto caps = orc::project_io::get_node_capabilities(graph_model_.project(), orc_node_id);
    
    // Debug: Log capabilities if debug logging is enabled
    qDebug() << "Node capabilities for" << QString::number(caps.node_id.value()) 
             << "(" << QString::fromStdString(caps.stage_name) << "):";
    qDebug() << "  Can remove:" << caps.can_remove 
             << (caps.can_remove ? "" : QString("- %1").arg(QString::fromStdString(caps.remove_reason)));
    qDebug() << "  Can trigger:" << caps.can_trigger 
             << (caps.can_trigger ? "" : QString("- %1").arg(QString::fromStdString(caps.trigger_reason)));
    qDebug() << "  Can inspect:" << caps.can_inspect 
             << (caps.can_inspect ? "" : QString("- %1").arg(QString::fromStdString(caps.inspect_reason)));
    
    // Create context menu
    QMenu* menu = new QMenu();
    menu->addSection(QString("Node: %1").arg(node_label));
    
    // Rename Node action - always available
    auto* rename_action = menu->addAction("Rename Node...");
    connect(rename_action, &QAction::triggered, [this, nodeId, node_label]() {
        // Prompt for new name
        bool ok;
        QString new_label = QInputDialog::getText(
            nullptr,
            "Rename Node",
            "Enter new name for node:",
            QLineEdit::Normal,
            node_label,
            &ok
        );
        if (ok && !new_label.isEmpty()) {
            graph_model_.setNodeData(nodeId, QtNodes::NodeRole::Caption, new_label);
        }
    });
    
    // Edit Parameters action - always available
    auto* edit_params_action = menu->addAction("Edit Parameters...");
    connect(edit_params_action, &QAction::triggered, [this, orc_node_id]() {
        Q_EMIT editParametersRequested(orc_node_id);
    });
    
    menu->addSeparator();
    
    // Trigger Stage action
    auto* trigger_action = menu->addAction("Trigger Stage");
    trigger_action->setEnabled(caps.can_trigger);
    if (!caps.can_trigger && !caps.trigger_reason.empty()) {
        trigger_action->setToolTip(QString::fromStdString(caps.trigger_reason));
    }
    connect(trigger_action, &QAction::triggered, [this, orc_node_id]() {
        Q_EMIT triggerStageRequested(orc_node_id);
    });
    
    // Inspect Stage action
    auto* inspect_action = menu->addAction("Inspect Stage...");
    inspect_action->setEnabled(caps.can_inspect);
    if (!caps.can_inspect && !caps.inspect_reason.empty()) {
        inspect_action->setToolTip(QString::fromStdString(caps.inspect_reason));
    }
    connect(inspect_action, &QAction::triggered, [this, orc_node_id]() {
        Q_EMIT inspectStageRequested(orc_node_id);
    });
    
    menu->addSeparator();
    
    // Run Analysis submenu - populate with tools applicable to this stage
    QMenu* analysis_menu = menu->addMenu("Run Analysis");
    auto& analysis_registry = orc::AnalysisRegistry::instance();
    auto all_tools = analysis_registry.tools();
    
    // Filter tools that are applicable to this stage type
    std::vector<orc::AnalysisTool*> applicable_tools;
    for (auto* tool : all_tools) {
        if (tool->isApplicableToStage(node_it->stage_name)) {
            applicable_tools.push_back(tool);
        }
    }
    
    // Sort tools: by priority (1=stage-specific, 2=common), then alphabetically
    std::sort(applicable_tools.begin(), applicable_tools.end(), 
        [](const orc::AnalysisTool* a, const orc::AnalysisTool* b) {
            int priority_a = a->priority();
            int priority_b = b->priority();
            if (priority_a != priority_b) {
                return priority_a < priority_b;  // Lower priority first
            }
            return a->name() < b->name();  // Same priority: alphabetical
        });
    
    if (applicable_tools.empty()) {
        analysis_menu->addAction("(No analysis tools available for this stage)")->setEnabled(false);
    } else {
        for (auto* tool : applicable_tools) {
            QString tool_name = QString::fromStdString(tool->name());
            QString tool_desc = QString::fromStdString(tool->description());
            
            auto* tool_action = analysis_menu->addAction(tool_name);
            tool_action->setToolTip(tool_desc);
            connect(tool_action, &QAction::triggered, [this, tool, orc_node_id, stage_name = node_it->stage_name]() {
                Q_EMIT runAnalysisRequested(tool, orc_node_id, stage_name);
            });
        }
    }
    
    menu->addSeparator();
    
    // Delete Node action
    auto* delete_action = menu->addAction("Delete Node");
    delete_action->setEnabled(caps.can_remove);
    if (!caps.can_remove && !caps.remove_reason.empty()) {
        delete_action->setToolTip(QString::fromStdString(caps.remove_reason));
    }
    connect(delete_action, &QAction::triggered, [this, nodeId]() {
        graph_model_.deleteNode(nodeId);
    });
    
    menu->setAttribute(Qt::WA_DeleteOnClose);
    
    // Convert scene position to screen position
    if (!views().isEmpty()) {
        QPoint screen_pos = views().first()->mapToGlobal(views().first()->mapFromScene(pos));
        menu->popup(screen_pos);
    } else {
        menu->popup(pos.toPoint());
    }
}
