/*
 * File:        dageditorwindow.cpp
 * Module:      orc-gui
 * Purpose:     DAG editor window
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "dageditorwindow.h"
#include "dagviewerwidget.h"
#include "guiproject.h"
#include "stageparameterdialog.h"
#include "logging.h"
#include "../core/include/dag_serialization.h"
#include "../core/include/stage_registry.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QVBoxLayout>
#include <QCloseEvent>

DAGEditorWindow::DAGEditorWindow(QWidget *parent)
    : QMainWindow(parent)
    , dag_viewer_(nullptr)
    , project_(nullptr)
{
    setWindowTitle("DAG Editor - orc-gui");
    resize(1000, 800);
    
    // Create DAG viewer widget
    dag_viewer_ = new DAGViewerWidget(this);
    setCentralWidget(dag_viewer_);
    
    // Connect signals
    connect(dag_viewer_, &DAGViewerWidget::nodeSelected,
            this, &DAGEditorWindow::onNodeSelected);
    connect(dag_viewer_, &DAGViewerWidget::changeNodeTypeRequested,
            this, &DAGEditorWindow::onChangeNodeType);
    connect(dag_viewer_, &DAGViewerWidget::editParametersRequested,
            this, &DAGEditorWindow::onEditParameters);
    
    // Forward dagModified signal to notify parent window
    connect(dag_viewer_, &DAGViewerWidget::dagModified,
            this, &DAGEditorWindow::projectModified);
    
    // Update DAG editor window title when project is modified
    connect(dag_viewer_, &DAGViewerWidget::dagModified,
            this, &DAGEditorWindow::updateWindowTitle);
    
    setupMenus();
    
    statusBar()->showMessage("DAG Editor Ready");
}

DAGEditorWindow::~DAGEditorWindow()
{
    // Disconnect all signals before destruction to avoid "object is not of correct type" errors
    if (dag_viewer_) {
        disconnect(dag_viewer_, nullptr, this, nullptr);
    }
}

void DAGEditorWindow::closeEvent(QCloseEvent* event)
{
    // Accept the close event
    event->accept();
    
    // Disconnect signals immediately to prevent issues during destruction
    if (dag_viewer_) {
        disconnect(dag_viewer_, nullptr, this, nullptr);
    }
    
    // Schedule deletion for later (after all events are processed)
    deleteLater();
}

void DAGEditorWindow::setupMenus()
{
    auto* file_menu = menuBar()->addMenu("&File");
    
    auto* close_action = file_menu->addAction("&Close");
    close_action->setShortcut(QKeySequence::Close);
    connect(close_action, &QAction::triggered, this, &QWidget::close);
    
    // Edit menu
    auto* edit_menu = menuBar()->addMenu("&Edit");
    
    auto* arrange_action = edit_menu->addAction("&Arrange to Grid");
    arrange_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    connect(arrange_action, &QAction::triggered, dag_viewer_, &DAGViewerWidget::arrangeToGrid);
    
    // Note: DAG is saved as part of the project via File → Save Project in main window
}

void DAGEditorWindow::setProject(GUIProject* project)
{
    project_ = project;
    if (project_) {
        ORC_LOG_DEBUG("DAG Editor: setting project {}", project_->projectName().toStdString());
        // Connect DAG viewer to the core project for CRUD operations
        if (dag_viewer_) {
            dag_viewer_->setProject(&project_->coreProject());
        }
        loadProjectDAG();
        updateWindowTitle();
    }
}

void DAGEditorWindow::loadProjectDAG()
{
    if (!project_) {
        return;
    }
    
    ORC_LOG_DEBUG("DAG Editor: loading project DAG for visualization");
    
    // Convert project DAG to GUIDAG for visualization
    orc::GUIDAG gui_dag;
    gui_dag.name = project_->projectName().toStdString();
    gui_dag.version = "1.0";
    
    auto& core_project = project_->coreProject();
    
    // Convert nodes
    for (const auto& node : core_project.nodes) {
        orc::GUIDAGNode gui_node;
        gui_node.node_id = node.node_id;
        gui_node.stage_name = node.stage_name;
        gui_node.node_type = node.node_type;
        gui_node.display_name = node.display_name;
        gui_node.user_label = node.user_label;
        gui_node.x_position = node.x_position;
        gui_node.y_position = node.y_position;
        gui_node.parameters = node.parameters;
        gui_dag.nodes.push_back(gui_node);
    }
    
    // Convert edges
    for (const auto& edge : core_project.edges) {
        orc::GUIDAGEdge gui_edge;
        gui_edge.source_node_id = edge.source_node_id;
        gui_edge.target_node_id = edge.target_node_id;
        gui_dag.edges.push_back(gui_edge);
    }
    
    dag_viewer_->importDAG(gui_dag);
    
    statusBar()->showMessage("Loaded DAG from project", 2000);
}


void DAGEditorWindow::onNodeSelected(const std::string& node_id)
{
    statusBar()->showMessage(QString("Selected node: %1").arg(QString::fromStdString(node_id)));
}

void DAGEditorWindow::onChangeNodeType(const std::string& node_id)
{
    // Get all available node types from the registry
    const auto& all_types = orc::get_all_node_types();
    
    QStringList stage_display_names;
    std::vector<std::string> stage_names;  // Parallel array to map display names to stage names
    
    for (const auto& type_info : all_types) {
        stage_display_names << QString::fromStdString(type_info.display_name);
        stage_names.push_back(type_info.stage_name);
    }
    
    bool ok;
    QString selected_display_name = QInputDialog::getItem(
        this,
        "Change Node Type",
        QString("Select new stage type for node '%1':").arg(QString::fromStdString(node_id)),
        stage_display_names,
        0,
        false,
        &ok
    );
    
    if (ok && !selected_display_name.isEmpty()) {
        // Find the stage_name corresponding to the selected display_name
        int index = stage_display_names.indexOf(selected_display_name);
        if (index >= 0 && index < static_cast<int>(stage_names.size())) {
            std::string stage_name = stage_names[index];
            dag_viewer_->setNodeStageType(node_id, stage_name);
            statusBar()->showMessage(
                QString("Changed node '%1' to %2")
                    .arg(QString::fromStdString(node_id))
                    .arg(selected_display_name),
                3000
            );
        }
    }
}

void DAGEditorWindow::onEditParameters(const std::string& node_id)
{
    std::string stage_name = dag_viewer_->getNodeStageType(node_id);
    
    if (stage_name.empty()) {
        QMessageBox::warning(this, "Edit Parameters",
            QString("Node '%1' not found").arg(QString::fromStdString(node_id)));
        return;
    }
    
    // Create stage instance using the registry
    auto& registry = orc::StageRegistry::instance();
    if (!registry.has_stage(stage_name)) {
        QMessageBox::warning(this, "Edit Parameters",
            QString("Unknown stage type '%1'").arg(QString::fromStdString(stage_name)));
        return;
    }
    
    auto stage = registry.create_stage(stage_name);
    if (!stage) {
        QMessageBox::warning(this, "Edit Parameters",
            QString("Failed to create stage '%1'").arg(QString::fromStdString(stage_name)));
        return;
    }
    
    // Check if stage supports parameters (using dynamic_cast)
    auto* param_stage = dynamic_cast<orc::ParameterizedStage*>(stage.get());
    if (!param_stage) {
        QMessageBox::information(this, "Edit Parameters",
            QString("Stage '%1' does not have configurable parameters")
                .arg(QString::fromStdString(stage_name)));
        return;
    }
    
    // Get parameter descriptors
    auto param_descriptors = param_stage->get_parameter_descriptors();
    
    if (param_descriptors.empty()) {
        QMessageBox::information(this, "Edit Parameters",
            QString("Stage '%1' does not have configurable parameters")
                .arg(QString::fromStdString(stage_name)));
        return;
    }
    
    // Get current parameter values from the node
    auto current_values = dag_viewer_->getNodeParameters(node_id);
    
    // Show parameter dialog
    StageParameterDialog dialog(stage_name, param_descriptors, current_values, this);
    
    if (dialog.exec() == QDialog::Accepted) {
        auto new_values = dialog.get_values();
        dag_viewer_->setNodeParameters(node_id, new_values);
        statusBar()->showMessage(
            QString("Updated parameters for node '%1'")
                .arg(QString::fromStdString(node_id)),
            3000
        );
    }
}



void DAGEditorWindow::updateWindowTitle()
{
    QString title = "DAG Editor";
    
    if (project_) {
        QString project_name = project_->projectName();
        if (!project_name.isEmpty()) {
            title = "DAG Editor - " + project_name;
            
            // Add modified indicator
            if (project_->isModified()) {
                title += " *";
            }
        }
    }
    
    setWindowTitle(title);
}
