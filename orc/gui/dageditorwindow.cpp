// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include "dageditorwindow.h"
#include "dagviewerwidget.h"
#include "guiproject.h"
#include "stageparameterdialog.h"
#include "../core/include/dag_serialization.h"
#include "../core/include/dropout_correct_stage.h"
#include "../core/include/passthrough_stage.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QVBoxLayout>

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
    
    // Connect modification signals to sync with project
    connect(dag_viewer_, &DAGViewerWidget::edgeCreated,
            this, &DAGEditorWindow::onDAGModified);
    connect(dag_viewer_, &DAGViewerWidget::addNodeRequested,
            this, &DAGEditorWindow::onDAGModified);
    connect(dag_viewer_, &DAGViewerWidget::deleteNodeRequested,
            this, &DAGEditorWindow::onDAGModified);
    
    setupMenus();
    
    statusBar()->showMessage("DAG Editor Ready");
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
        loadProjectDAG();
    }
}

void DAGEditorWindow::loadProjectDAG()
{
    if (!project_) {
        return;
    }
    
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
        gui_node.display_name = node.display_name;
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
    
    // Set source info for START nodes
    if (project_->hasSource()) {
        // For now, set the same source info for all START nodes (single source workflow)
        dag_viewer_->setSourceInfo(project_->getSourceId(), project_->getSourceName());
    }
    
    statusBar()->showMessage("Loaded DAG from project", 2000);
}

void DAGEditorWindow::onDAGModified()
{
    // Sync changes back to project
    syncDAGToProject();
}

void DAGEditorWindow::syncDAGToProject()
{
    if (!project_) {
        return;
    }
    
    // Export current DAG state
    auto gui_dag = dag_viewer_->exportDAG();
    
    // Convert nodes to project format
    std::vector<orc::ProjectDAGNode> nodes;
    for (const auto& gui_node : gui_dag.nodes) {
        orc::ProjectDAGNode node;
        node.node_id = gui_node.node_id;
        node.stage_name = gui_node.stage_name;
        node.display_name = gui_node.display_name;
        node.x_position = gui_node.x_position;
        node.y_position = gui_node.y_position;
        node.parameters = gui_node.parameters;
        
        // Extract source_id if this is a Source node
        node.source_id = -1;
        if (gui_node.stage_name == "Source") {
            // Parse node_id like "start_0" to get source_id
            std::string id_str = gui_node.node_id;
            if (id_str.find("start_") == 0) {
                try {
                    node.source_id = std::stoi(id_str.substr(6));
                } catch (...) {
                    node.source_id = -1;
                }
            }
        }
        
        nodes.push_back(node);
    }
    
    // Convert edges to project format
    std::vector<orc::ProjectDAGEdge> edges;
    for (const auto& gui_edge : gui_dag.edges) {
        orc::ProjectDAGEdge edge;
        edge.source_node_id = gui_edge.source_node_id;
        edge.target_node_id = gui_edge.target_node_id;
        edges.push_back(edge);
    }
    
    // Use core function to update project DAG (preserves START nodes)
    orc::project_io::update_project_dag(project_->coreProject(), nodes, edges);
    
    // Mark project as modified
    project_->setModified(true);
}

void DAGEditorWindow::onNodeSelected(const std::string& node_id)
{
    statusBar()->showMessage(QString("Selected node: %1").arg(QString::fromStdString(node_id)));
}

void DAGEditorWindow::onChangeNodeType(const std::string& node_id)
{
    QStringList stage_types;
    stage_types << "Passthrough" << "Dropout Correct";
    
    bool ok;
    QString selected = QInputDialog::getItem(
        this,
        "Change Node Type",
        QString("Select new stage type for node '%1':").arg(QString::fromStdString(node_id)),
        stage_types,
        0,
        false,
        &ok
    );
    
    if (ok && !selected.isEmpty()) {
        dag_viewer_->setNodeStageType(node_id, selected.toStdString());
        statusBar()->showMessage(
            QString("Changed node '%1' to %2")
                .arg(QString::fromStdString(node_id))
                .arg(selected),
            3000
        );
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
    
    // Get parameters from the appropriate stage
    std::unique_ptr<orc::ParameterizedStage> stage;
    
    if (stage_name == "Dropout Correct") {
        stage = std::make_unique<orc::DropoutCorrectStage>();
    } else if (stage_name == "Passthrough") {
        stage = std::make_unique<orc::PassthroughStage>();
    } else {
        QMessageBox::information(this, "Edit Parameters",
            QString("Stage '%1' does not have configurable parameters")
                .arg(QString::fromStdString(stage_name)));
        return;
    }
    
    // Get current parameter values from the node
    auto current_values = dag_viewer_->getNodeParameters(node_id);
    
    // Show parameter dialog
    StageParameterDialog dialog(stage_name, stage->get_parameter_descriptors(), current_values, this);
    
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

void DAGEditorWindow::setSourceInfo(int source_number, const QString& source_name)
{
    if (dag_viewer_) {
        dag_viewer_->setSourceInfo(source_number, source_name);
    }
}
