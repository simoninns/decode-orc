/*
 * File:        orcgraphicsscene.cpp
 * Module:      orc-gui
 * Purpose:     Custom QtNodes scene with context menu support
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "orcgraphicsscene.h"
#include "orcnodepainter.h"
#include "logging.h"
#include <node_type.h>
#include <common_types.h>  // For VideoSystem, SourceType
#include "presenters/include/project_presenter.h"
#include "presenters/include/analysis_presenter.h"
// Removed: #include "analysis_registry.h"  // Phase 2.4: Now using AnalysisPresenter
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QGraphicsView>
#include <QMessageBox>
#include <QInputDialog>
#include <algorithm>

using orc::NodeID;
using orc::get_all_node_types;
using orc::is_stage_compatible_with_format;
using orc::get_node_type_info;
using orc::NodeTypeInfo;
using orc::VideoSystem;
using orc::SourceType;
using orc::NodeType;

OrcGraphicsScene::OrcGraphicsScene(OrcGraphModel& graphModel, QObject* parent)
    : QtNodes::BasicGraphicsScene(graphModel, parent)
    , graph_model_(graphModel)
{
    // Disable BSP indexing for dynamic node graph to prevent BSP tree crashes
    // See: https://doc.qt.io/qt-6/qgraphicsscene.html#ItemIndexMethod-enum
    // Node graphs have frequent add/remove/update operations which can cause
    // stale BSP tree entries and crashes during paint traversal
    setItemIndexMethod(QGraphicsScene::NoIndex);
    
    // Set custom node painter that distinguishes "one" vs "many" ports
    setNodePainter(std::make_unique<OrcNodePainter>());
    
    // Connect to scene's selection changed signal
    connect(this, &QGraphicsScene::selectionChanged,
            this, &OrcGraphicsScene::onSelectionChanged);
    
    // Connect to node context menu signal from BasicGraphicsScene
    connect(this, &QtNodes::BasicGraphicsScene::nodeContextMenu,
            this, &OrcGraphicsScene::onNodeContextMenu);
}

OrcGraphicsScene::~OrcGraphicsScene()
{
    // Disconnect all signals to prevent callbacks during destruction
    // This prevents Qt from trying to call methods on partially-destructed objects
    disconnect();
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
    QMenu* menu = new QMenu(views().isEmpty() ? nullptr : views().first());
    
    // Check if project has a valid name (indicating it's been created/loaded)
    bool has_project = !graph_model_.presenter().getProjectName().empty();
    
    // Add Node submenu
    QMenu* add_node_menu = menu->addMenu("Add Node");
    add_node_menu->setEnabled(has_project);
    
    if (!has_project) {
        add_node_menu->addAction("(No project loaded)")->setEnabled(false);
    } else {
        const auto& all_types = get_all_node_types();
        auto project_format_enum = graph_model_.presenter().getVideoFormat();
        auto project_source_enum = graph_model_.presenter().getSourceType();
        
        // Convert presenter enums to core enums
        VideoSystem project_format = (project_format_enum == orc::presenters::VideoFormat::NTSC) 
            ? VideoSystem::NTSC : (project_format_enum == orc::presenters::VideoFormat::PAL) 
            ? VideoSystem::PAL : VideoSystem::Unknown;
        SourceType project_source_type = (project_source_enum == orc::presenters::SourceType::Composite)
            ? SourceType::Composite : (project_source_enum == orc::presenters::SourceType::YC)
            ? SourceType::YC : SourceType::Unknown;
        
        // Organize stages by type
        std::vector<const NodeTypeInfo*> source_stages;
        std::vector<const NodeTypeInfo*> transform_stages;
        std::vector<const NodeTypeInfo*> sink_stages;
        std::vector<const NodeTypeInfo*> analysis_stages;
        
        for (const auto& type_info : all_types) {
            // Filter stages by video format compatibility
            if (!is_stage_compatible_with_format(type_info.stage_name, project_format)) {
                continue;
            }
            
            // Categorize by NodeType
            switch (type_info.type) {
                case NodeType::SOURCE: {
                    // Filter source stages by source type if project has a specified source format
                    if (project_source_type != SourceType::Unknown) {
                        bool is_yc_stage = (type_info.stage_name.find("YC") != std::string::npos);
                        SourceType stage_type = is_yc_stage ? SourceType::YC : SourceType::Composite;
                        
                        // Only include source stages that match project's source type
                        if (stage_type != project_source_type) {
                            continue;
                        }
                    }
                    source_stages.push_back(&type_info);
                    break;
                }
                case NodeType::TRANSFORM:
                case NodeType::MERGER:
                case NodeType::COMPLEX:
                    transform_stages.push_back(&type_info);
                    break;
                case NodeType::SINK:
                    sink_stages.push_back(&type_info);
                    break;
                case NodeType::ANALYSIS_SINK:
                    analysis_stages.push_back(&type_info);
                    break;
            }
        }
        
        // Helper lambda to add stages to a menu
        auto add_stages_to_menu = [this, scenePos](QMenu* parent_menu, const std::vector<const NodeTypeInfo*>& stages) {
            for (const auto* type_info : stages) {
                QString display_name = QString::fromStdString(type_info->display_name);
                QString tooltip = QString::fromStdString(type_info->description);
                
                auto* action = parent_menu->addAction(display_name, [this, scenePos, stage_name = type_info->stage_name]() {
                    // Add node at clicked position
                    QtNodes::NodeId nodeId = graph_model_.addNode(QString::fromStdString(stage_name));
                    if (nodeId != QtNodes::InvalidNodeId) {
                        graph_model_.setNodeData(nodeId, QtNodes::NodeRole::Position, scenePos);
                    }
                });
                action->setToolTip(tooltip);
            }
        };
        
        // Add Source submenu
        if (!source_stages.empty()) {
            QMenu* source_menu = add_node_menu->addMenu("Source");
            add_stages_to_menu(source_menu, source_stages);
        }
        
        // Add Transform submenu
        if (!transform_stages.empty()) {
            QMenu* transform_menu = add_node_menu->addMenu("Transform");
            add_stages_to_menu(transform_menu, transform_stages);
        }
        
        // Add Sink submenu
        if (!sink_stages.empty()) {
            QMenu* sink_menu = add_node_menu->addMenu("Sink");
            add_stages_to_menu(sink_menu, sink_stages);
        }
        
        // Add Analysis Sink submenu
        if (!analysis_stages.empty()) {
            QMenu* analysis_menu = add_node_menu->addMenu("Analysis Sink");
            add_stages_to_menu(analysis_menu, analysis_stages);
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
    
    ORC_LOG_DEBUG("Showing context menu for ORC node '{}'", orc_node_id.to_string());
    
    // Get node info from presenter
    try {
        auto node_info = graph_model_.presenter().getNodeInfo(orc_node_id);
        
        QString node_label = QString::fromStdString(
            node_info.label.empty() ? node_info.stage_name : node_info.label);
        
        // Debug: Log capabilities if debug logging is enabled
        qDebug() << "Node capabilities for" << QString::fromStdString(orc_node_id.to_string())
                 << "(" << QString::fromStdString(node_info.stage_name) << "):";
        qDebug() << "  Can remove:" << node_info.can_remove 
                 << (node_info.can_remove ? "" : QString("- %1").arg(QString::fromStdString(node_info.remove_reason)));
        qDebug() << "  Can trigger:" << node_info.can_trigger 
                 << (node_info.can_trigger ? "" : QString("- %1").arg(QString::fromStdString(node_info.trigger_reason)));
        qDebug() << "  Can inspect:" << node_info.can_inspect 
                 << (node_info.can_inspect ? "" : QString("- %1").arg(QString::fromStdString(node_info.inspect_reason)));
        
        // Create context menu (with view as parent to ensure proper cleanup)
        QMenu* menu = new QMenu(views().isEmpty() ? nullptr : views().first());
        menu->addSection(QString("%1 (%2)").arg(node_label).arg(QString::fromStdString(orc_node_id.to_string())));
        
        // Rename Stage action - always available
        auto* rename_action = menu->addAction("Rename Stage...");
        connect(rename_action, &QAction::triggered, [this, nodeId, node_label]() {
            // Prompt for new name
            bool ok;
            QString new_label = QInputDialog::getText(
                nullptr,
                "Rename Stage",
                "Enter new name for stage:",
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
        trigger_action->setEnabled(node_info.can_trigger);
        if (!node_info.can_trigger && !node_info.trigger_reason.empty()) {
            trigger_action->setToolTip(QString::fromStdString(node_info.trigger_reason));
        }
        connect(trigger_action, &QAction::triggered, [this, orc_node_id]() {
            Q_EMIT triggerStageRequested(orc_node_id);
        });
        
        // Inspect Stage action
        auto* inspect_action = menu->addAction("Inspect Stage...");
        inspect_action->setEnabled(node_info.can_inspect);
        if (!node_info.can_inspect && !node_info.inspect_reason.empty()) {
            inspect_action->setToolTip(QString::fromStdString(node_info.inspect_reason));
        }
        connect(inspect_action, &QAction::triggered, [this, orc_node_id]() {
            Q_EMIT inspectStageRequested(orc_node_id);
        });
        
        menu->addSeparator();
        
        // Run Analysis submenu - populate with tools applicable to this stage
        QMenu* analysis_menu = menu->addMenu("Stage Tools");
        
        // Phase 2.4: Use AnalysisPresenter instead of direct registry access
        orc::presenters::AnalysisPresenter analysis_presenter(graph_model_.presenter().getCoreProjectHandle());
        auto tool_infos = analysis_presenter.getToolsForStage(node_info.stage_name);
        
        if (tool_infos.empty()) {
            analysis_menu->addAction("(No analysis tools available for this stage)")->setEnabled(false);
        } else {
            // Tools are already sorted by priority in getToolsForStage()
            for (const auto& tool_info : tool_infos) {
                QString tool_name = QString::fromStdString(tool_info.name);
                QString tool_desc = QString::fromStdString(tool_info.description);
                
                auto* tool_action = analysis_menu->addAction(tool_name);
                tool_action->setToolTip(tool_desc);
                
                // Pass tool_info to signal instead of raw pointer
                connect(tool_action, &QAction::triggered, [this, tool_info, orc_node_id, stage_name = node_info.stage_name]() {
                    Q_EMIT runAnalysisRequested(tool_info, orc_node_id, stage_name);
                });
            }
        }
        
        menu->addSeparator();
        
        // Delete Stage action
        auto* delete_action = menu->addAction("Delete Stage");
        delete_action->setEnabled(node_info.can_remove);
        if (!node_info.can_remove && !node_info.remove_reason.empty()) {
            delete_action->setToolTip(QString::fromStdString(node_info.remove_reason));
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
    } catch (const std::exception& e) {
        ORC_LOG_WARN("Could not get node info for '{}': {}", orc_node_id.to_string(), e.what());
    }
}
