// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include "dageditorwindow.h"
#include "dagviewerwidget.h"
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
    
    setupMenus();
    
    statusBar()->showMessage("DAG Editor Ready");
}

void DAGEditorWindow::setupMenus()
{
    auto* file_menu = menuBar()->addMenu("&File");
    
    auto* load_dag_action = file_menu->addAction("&Load DAG...");
    load_dag_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(load_dag_action, &QAction::triggered, this, &DAGEditorWindow::onLoadDAG);
    
    auto* save_dag_action = file_menu->addAction("&Save DAG...");
    save_dag_action->setShortcut(QKeySequence::Save);
    connect(save_dag_action, &QAction::triggered, this, &DAGEditorWindow::onSaveDAG);
    
    file_menu->addSeparator();
    
    auto* close_action = file_menu->addAction("&Close");
    close_action->setShortcut(QKeySequence::Close);
    connect(close_action, &QAction::triggered, this, &QWidget::close);
}

void DAGEditorWindow::onLoadDAG()
{
    QString filename = QFileDialog::getOpenFileName(
        this,
        "Load DAG",
        QString(),
        "YAML Files (*.yaml *.yml);;All Files (*)"
    );
    
    if (filename.isEmpty()) {
        return;
    }
    
    try {
        auto dag = orc::dag_serialization::load_dag_from_yaml(filename.toStdString());
        dag_viewer_->importDAG(dag);
        statusBar()->showMessage(QString("Loaded DAG from %1").arg(filename), 3000);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error Loading DAG",
            QString("Failed to load DAG: %1").arg(e.what()));
    }
}

void DAGEditorWindow::onSaveDAG()
{
    QString filename = QFileDialog::getSaveFileName(
        this,
        "Save DAG",
        QString(),
        "YAML Files (*.yaml);;All Files (*)"
    );
    
    if (filename.isEmpty()) {
        return;
    }
    
    // Add .yaml extension if not present
    if (!filename.endsWith(".yaml", Qt::CaseInsensitive) && 
        !filename.endsWith(".yml", Qt::CaseInsensitive)) {
        filename += ".yaml";
    }
    
    try {
        auto dag = dag_viewer_->exportDAG();
        orc::dag_serialization::save_dag_to_yaml(dag, filename.toStdString());
        statusBar()->showMessage(QString("Saved DAG to %1").arg(filename), 3000);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error Saving DAG",
            QString("Failed to save DAG: %1").arg(e.what()));
    }
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
