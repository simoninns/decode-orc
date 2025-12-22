/*
 * File:        mainwindow.cpp
 * Module:      orc-gui
 * Purpose:     Main application window
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "mainwindow.h"
#include "fieldpreviewwidget.h"
#include "previewdialog.h"
#include "projectpropertiesdialog.h"
#include "stageparameterdialog.h"
#include "inspection_dialog.h"
#include "analysis/analysis_dialog.h"
#include "orcgraphicsview.h"
#include "logging.h"
#include "../core/include/preview_renderer.h"
#include "../core/include/stage_registry.h"
#include "../core/include/dag_executor.h"
#include "../core/include/project_to_dag.h"
#include "../core/analysis/analysis_registry.h"
#include "../core/analysis/analysis_context.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QDir>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QMessageBox>
#include <QDebug>
#include <QKeyEvent>
#include <QComboBox>
#include <QApplication>
#include <QSplitter>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , preview_dialog_(nullptr)
    , dag_view_(nullptr)
    , dag_model_(nullptr)
    , dag_scene_(nullptr)
    , toolbar_(nullptr)
    , save_project_action_(nullptr)
    , save_project_as_action_(nullptr)
    , edit_project_action_(nullptr)
    , show_preview_action_(nullptr)
    , auto_show_preview_action_(nullptr)
    , preview_renderer_(nullptr)
    , current_view_node_id_()
    , current_output_type_(orc::PreviewOutputType::Frame)
{
    setupUI();
    setupMenus();
    setupToolbar();
    
    updateWindowTitle();
    resize(1200, 800);
    
    updateUIState();
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUI()
{
    // Create preview dialog (initially hidden)
    preview_dialog_ = new PreviewDialog(this);
    
    // Connect preview dialog signals
    connect(preview_dialog_, &PreviewDialog::previewIndexChanged,
            this, &MainWindow::onPreviewIndexChanged);
    connect(preview_dialog_, &PreviewDialog::previewModeChanged,
            this, &MainWindow::onPreviewModeChanged);
    connect(preview_dialog_, &PreviewDialog::aspectRatioModeChanged,
            this, &MainWindow::onAspectRatioModeChanged);
    connect(preview_dialog_, &PreviewDialog::exportPNGRequested,
            this, &MainWindow::onExportPNG);
    
    // Create QtNodes DAG editor
    dag_view_ = new OrcGraphicsView(this);
    dag_model_ = new OrcGraphModel(project_.coreProject(), dag_view_);
    dag_scene_ = new OrcGraphicsScene(*dag_model_, dag_view_);
    
    dag_view_->setScene(dag_scene_);
    
    // Connect scene/model signals for DAG modifications  
    connect(dag_model_, &QtNodes::AbstractGraphModel::connectionCreated,
            this, &MainWindow::onDAGModified);
    connect(dag_model_, &QtNodes::AbstractGraphModel::connectionDeleted,
            this, &MainWindow::onDAGModified);
    connect(dag_model_, &QtNodes::AbstractGraphModel::nodeCreated,
            this, &MainWindow::onDAGModified);
    connect(dag_model_, &QtNodes::AbstractGraphModel::nodeDeleted,
            this, &MainWindow::onDAGModified);
    
    // Connect node selection signal
    connect(dag_scene_, &OrcGraphicsScene::nodeSelected,
            this, &MainWindow::onQtNodeSelected);
    
    // Connect node context menu action signals
    connect(dag_scene_, &OrcGraphicsScene::editParametersRequested,
            this, &MainWindow::onEditParameters);
    connect(dag_scene_, &OrcGraphicsScene::triggerStageRequested,
            this, &MainWindow::onTriggerStage);
    connect(dag_scene_, &OrcGraphicsScene::inspectStageRequested,
            this, &MainWindow::onInspectStage);
    connect(dag_scene_, &OrcGraphicsScene::runAnalysisRequested,
            this, &MainWindow::runAnalysisForNode);
    
    // DAG editor takes up full main window
    setCentralWidget(dag_view_);
    
    // Status bar
    statusBar()->showMessage("Ready");
}

void MainWindow::setupMenus()
{
    auto* file_menu = menuBar()->addMenu("&File");
    
    auto* new_project_action = file_menu->addAction("&New Project...");
    new_project_action->setShortcut(QKeySequence::New);
    connect(new_project_action, &QAction::triggered, this, &MainWindow::onNewProject);
    
    auto* open_project_action = file_menu->addAction("&Open Project...");
    open_project_action->setShortcut(QKeySequence::Open);
    connect(open_project_action, &QAction::triggered, this, &MainWindow::onOpenProject);
    
    file_menu->addSeparator();
    
    save_project_action_ = file_menu->addAction("&Save Project");
    save_project_action_->setShortcut(QKeySequence::Save);
    save_project_action_->setEnabled(false);
    connect(save_project_action_, &QAction::triggered, this, &MainWindow::onSaveProject);
    
    save_project_as_action_ = file_menu->addAction("Save Project &As...");
    save_project_as_action_->setShortcut(QKeySequence::SaveAs);
    save_project_as_action_->setEnabled(false);
    connect(save_project_as_action_, &QAction::triggered, this, &MainWindow::onSaveProjectAs);
    
    file_menu->addSeparator();
    
    edit_project_action_ = file_menu->addAction("&Edit Project...");
    edit_project_action_->setEnabled(false);
    connect(edit_project_action_, &QAction::triggered, this, &MainWindow::onEditProject);
    
    file_menu->addSeparator();
    
    export_png_action_ = file_menu->addAction("E&xport Preview as PNG...");
    export_png_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    export_png_action_->setEnabled(false);
    connect(export_png_action_, &QAction::triggered, this, &MainWindow::onExportPNG);
    
    file_menu->addSeparator();
    
    auto* quit_action = file_menu->addAction("&Quit");
    quit_action->setShortcut(QKeySequence::Quit);
    connect(quit_action, &QAction::triggered, this, &QWidget::close);
    
    // View menu for DAG operations
    auto* view_menu = menuBar()->addMenu("&View");
    
    show_preview_action_ = view_menu->addAction("Show &Preview");
    show_preview_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    show_preview_action_->setEnabled(false);
    connect(show_preview_action_, &QAction::triggered, this, [this]() { preview_dialog_->show(); });
    
    view_menu->addSeparator();
    
    auto_show_preview_action_ = view_menu->addAction("Show Preview on &Selection");
    auto_show_preview_action_->setCheckable(true);
    
    // Load setting from QSettings (default: true)
    QSettings settings;
    bool auto_show = settings.value("preview/auto_show_on_selection", true).toBool();
    auto_show_preview_action_->setChecked(auto_show);
    
    // Save setting when changed
    connect(auto_show_preview_action_, &QAction::toggled, this, [](bool checked) {
        QSettings settings;
        settings.setValue("preview/auto_show_on_selection", checked);
    });
    
    view_menu->addSeparator();
    
    auto* arrange_action = view_menu->addAction("&Arrange DAG to Grid");
    arrange_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    connect(arrange_action, &QAction::triggered, this, &MainWindow::onArrangeDAGToGrid);
}

void MainWindow::setupToolbar()
{
    toolbar_ = addToolBar("Main");
    // Toolbar can be used for other controls later
}



void MainWindow::onNewProject()
{
    newProject();
}

void MainWindow::onOpenProject()
{
    QString filename = QFileDialog::getOpenFileName(
        this,
        "Open Project",
        getLastProjectDirectory(),
        "ORC Project Files (*.orcprj);;All Files (*)"
    );
    
    if (filename.isEmpty()) {
        ORC_LOG_DEBUG("Project open cancelled");
        return;
    }
    
    // Remember this directory
    setLastProjectDirectory(QFileInfo(filename).absolutePath());
    
    ORC_LOG_INFO("Opening project: {}", filename.toStdString());
    openProject(filename);
}

void MainWindow::onSaveProject()
{
    if (project_.projectPath().isEmpty()) {
        onSaveProjectAs();
        return;
    }
    
    saveProject();
}

void MainWindow::onSaveProjectAs()
{
    saveProjectAs();
}

void MainWindow::onEditProject()
{
    // Open dialog with current project properties
    ProjectPropertiesDialog dialog(this);
    dialog.setProjectName(QString::fromStdString(project_.coreProject().get_name()));
    dialog.setProjectDescription(QString::fromStdString(project_.coreProject().get_description()));
    
    if (dialog.exec() == QDialog::Accepted) {
        // Update project with new values
        QString new_name = dialog.projectName();
        QString new_description = dialog.projectDescription();
        
        if (new_name.isEmpty()) {
            QMessageBox::warning(this, "Invalid Input", "Project name cannot be empty.");
            return;
        }
        
        // Update core project using project_io
        orc::project_io::set_project_name(project_.coreProject(), new_name.toStdString());
        orc::project_io::set_project_description(project_.coreProject(), new_description.toStdString());
        
        ORC_LOG_INFO("Project properties updated: name='{}', description='{}'", 
                     new_name.toStdString(), new_description.toStdString());
        
        // Update UI to reflect changes
        updateWindowTitle();
        updateUIState();
        
        statusBar()->showMessage("Project properties updated", 3000);
    }
}


void MainWindow::newProject()
{
    QString filename = QFileDialog::getSaveFileName(
        this,
        "New Project",
        getLastProjectDirectory(),
        "ORC Project Files (*.orcprj);;All Files (*)"
    );
    
    if (filename.isEmpty()) {
        ORC_LOG_DEBUG("New project creation cancelled");
        return;
    }
    
    // Ensure .orcprj extension
    if (!filename.endsWith(".orcprj", Qt::CaseInsensitive)) {
        filename += ".orcprj";
    }
    
    // Remember this directory
    setLastProjectDirectory(QFileInfo(filename).absolutePath());
    
    ORC_LOG_INFO("Creating new project: {}", filename.toStdString());
    
    // Clear existing project state
    project_.clear();
    preview_dialog_->previewWidget()->clearImage();
    preview_dialog_->previewSlider()->setEnabled(false);
    preview_dialog_->previewSlider()->setValue(0);
    
    // Derive project name from filename
    QString project_name = QFileInfo(filename).completeBaseName();
    
    QString error;
    if (!project_.newEmptyProject(project_name, &error)) {
        ORC_LOG_ERROR("Failed to create project: {}", error.toStdString());
        QMessageBox::critical(this, "Error", error);
        return;
    }
    
    // Save immediately to the specified location
    if (!project_.saveToFile(filename, &error)) {
        ORC_LOG_ERROR("Failed to save project: {}", error.toStdString());
        QMessageBox::critical(this, "Error", error);
        return;
    }
    
    ORC_LOG_INFO("Project created successfully: {}", project_name.toStdString());
    updateWindowTitle();
    updateUIState();
    
    // Initialize preview renderer for new project
    updatePreviewRenderer();
    
    // Load DAG into embedded viewer
    loadProjectDAG();
    
    statusBar()->showMessage(QString("Created new project: %1").arg(project_name));
}

void MainWindow::openProject(const QString& filename)
{
    ORC_LOG_INFO("Loading project: {}", filename.toStdString());
    
    // Clear existing project state
    project_.clear();
    preview_dialog_->previewWidget()->clearImage();
    preview_dialog_->previewSlider()->setEnabled(false);
    preview_dialog_->previewSlider()->setValue(0);
    
    QString error;
    if (!project_.loadFromFile(filename, &error)) {
        ORC_LOG_ERROR("Failed to load project: {}", error.toStdString());
        QMessageBox::critical(this, "Error", error);
        return;
    }
    
    ORC_LOG_DEBUG("Project loaded: {}", project_.projectName().toStdString());
    
    // Project loaded - user can select a node in the DAG editor for viewing
    if (project_.hasSource()) {
        ORC_LOG_INFO("Source loaded - select a node in DAG editor for viewing");
        
        // Show helpful message
        statusBar()->showMessage("Project loaded - select a node in DAG editor to view", 5000);
    } else {
        ORC_LOG_DEBUG("Project has no source");
    }
    
    updateWindowTitle();
    updateUIState();
    
    // Initialize preview renderer with project DAG
    updatePreviewRenderer();
    
    // Load DAG into embedded viewer
    loadProjectDAG();
    
    statusBar()->showMessage(QString("Opened project: %1").arg(project_.projectName()));
}

void MainWindow::saveProject()
{
    if (project_.projectPath().isEmpty()) {
        saveProjectAs();
        return;
    }
    
    ORC_LOG_INFO("Saving project: {}", project_.projectPath().toStdString());
    
    QString error;
    if (!project_.saveToFile(project_.projectPath(), &error)) {
        ORC_LOG_ERROR("Failed to save project: {}", error.toStdString());
        QMessageBox::critical(this, "Error", error);
        return;
    }
    
    ORC_LOG_DEBUG("Project saved successfully");
    updateWindowTitle();
    statusBar()->showMessage("Project saved");
}

void MainWindow::saveProjectAs()
{
    QString filename = QFileDialog::getSaveFileName(
        this,
        "Save Project As",
        getLastProjectDirectory(),
        "ORC Project Files (*.orcprj);;All Files (*)"
    );
    
    if (filename.isEmpty()) {
        return;
    }
    
    // Ensure .orcprj extension
    if (!filename.endsWith(".orcprj", Qt::CaseInsensitive)) {
        filename += ".orcprj";
    }
    
    // Remember this directory
    setLastProjectDirectory(QFileInfo(filename).absolutePath());
    
    QString error;
    if (!project_.saveToFile(filename, &error)) {
        QMessageBox::critical(this, "Error", error);
        return;
    }
    
    updateWindowTitle();
    updateUIState();
    statusBar()->showMessage("Project saved as " + filename);
}



void MainWindow::updateUIState()
{
    bool has_project = !project_.projectName().isEmpty();
    bool has_preview = preview_renderer_ && !current_view_node_id_.empty();
    
    // Enable/disable actions based on project state
    if (save_project_action_) {
        save_project_action_->setEnabled(has_project && project_.isModified());
    }
    if (save_project_as_action_) {
        save_project_as_action_->setEnabled(has_project);
    }
    if (edit_project_action_) {
        edit_project_action_->setEnabled(has_project);
    }
    if (export_png_action_) {
        export_png_action_->setEnabled(has_preview);
    }
    
    // Enable/disable DAG view based on project state
    if (dag_view_) {
        dag_view_->setEnabled(has_project);
    }
    
    // Enable aspect ratio selector when preview is available (in preview dialog)
    if (preview_dialog_) {
        preview_dialog_->aspectRatioCombo()->setEnabled(has_preview);
    }
    
    // Update window title to reflect modified state
    updateWindowTitle();
}

void MainWindow::onPreviewIndexChanged(int index)
{
    // Slider changed - update the preview at the current node
    updatePreview();
    updatePreviewInfo();
}

void MainWindow::onNavigatePreview(int delta)
{
    if (!preview_dialog_->previewSlider()->isEnabled()) {
        return;
    }
    
    // In frame view modes, move by 2 items at a time
    int step = (current_output_type_ == orc::PreviewOutputType::Frame ||
                current_output_type_ == orc::PreviewOutputType::Frame_Reversed) ? 2 : 1;
    
    int current_index = preview_dialog_->previewSlider()->value();
    int new_index = current_index + (delta * step);
    int max_index = preview_dialog_->previewSlider()->maximum();
    
    if (new_index >= 0 && new_index <= max_index) {
        preview_dialog_->previewSlider()->setValue(new_index);
    }
}

void MainWindow::onPreviewModeChanged(int index)
{
    // Get output type from stored available outputs
    if (index < 0 || index >= static_cast<int>(available_outputs_.size())) {
        return;
    }
    
    // Remember previous type and current position
    auto previous_type = current_output_type_;
    int current_position = preview_dialog_->previewSlider()->value();
    
    // Update to new type
    current_output_type_ = available_outputs_[index].type;
    
    // Ask core for equivalent index in new output type
    uint64_t new_position = preview_renderer_->get_equivalent_index(
        previous_type,
        current_position,
        current_output_type_
    );
    
    // Use helper function to update all viewer controls (slider range, step, preview, info)
    refreshViewerControls();
    
    // Set the calculated position (after refreshViewerControls updates the range)
    if (new_position >= 0 && new_position <= static_cast<uint64_t>(preview_dialog_->previewSlider()->maximum())) {
        preview_dialog_->previewSlider()->setValue(new_position);
    }
}

void MainWindow::onAspectRatioModeChanged(int index)
{
    if (!preview_renderer_) {
        return;
    }
    
    // Get available modes from core
    auto available_modes = preview_renderer_->get_available_aspect_ratio_modes();
    if (index < 0 || index >= static_cast<int>(available_modes.size())) {
        return;
    }
    
    // Update the aspect ratio mode in the renderer
    preview_renderer_->set_aspect_ratio_mode(available_modes[index].mode);
    
    // Get the correction factor from core (not calculated by GUI)
    double aspect_correction = available_modes[index].correction_factor;
    preview_dialog_->previewWidget()->setAspectCorrection(aspect_correction);
    
    // Refresh the display
    preview_dialog_->previewWidget()->update();
}

void MainWindow::updateWindowTitle()
{
    QString title = "Orc GUI";
    
    QString project_name = project_.projectName();
    if (!project_name.isEmpty()) {
        title = project_name;
        
        // Add source name if available
        if (project_.hasSource()) {
            QString source_name = project_.getSourceName();
            if (!source_name.isEmpty()) {
                title += " - " + source_name;
            }
        }
        
        // Add modified indicator
        if (project_.isModified()) {
            title += " *";
        }
    }
    
    setWindowTitle(title);
    
    // Force window manager to update the title bar immediately
    // This ensures the title updates even when the DAG editor window is active
    QApplication::processEvents();
}

void MainWindow::updatePreviewInfo()
{
    if (!preview_renderer_ || current_view_node_id_.empty()) {
        preview_dialog_->previewInfoLabel()->setText("No node selected");
        preview_dialog_->sliderMinLabel()->setText("");
        preview_dialog_->sliderMaxLabel()->setText("");
        return;
    }
    
    // Special handling for placeholder node
    if (current_view_node_id_ == "_no_preview") {
        preview_dialog_->previewInfoLabel()->setText("No source available");
        preview_dialog_->sliderMinLabel()->setText("");
        preview_dialog_->sliderMaxLabel()->setText("");
        return;
    }
    
    // Get detailed display info from core
    int current_index = preview_dialog_->previewSlider()->value();
    int total = preview_dialog_->previewSlider()->maximum() + 1;
    
    auto display_info = preview_renderer_->get_preview_item_display_info(
        current_output_type_,
        current_index,
        total
    );
    
    // Update slider labels with range
    preview_dialog_->sliderMinLabel()->setText(QString::number(1));
    preview_dialog_->sliderMaxLabel()->setText(QString::number(display_info.total_count));
    
    // Build compact info label
    QString info_text = QString("%1 %2")
        .arg(QString::fromStdString(display_info.type_name))
        .arg(display_info.current_number);
    
    // Add field info if relevant
    if (display_info.has_field_info) {
        info_text += QString(" (%1-%2)")
            .arg(display_info.first_field_number)
            .arg(display_info.second_field_number);
    }
    
    preview_dialog_->previewInfoLabel()->setText(info_text);
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (!preview_dialog_->previewSlider()->isEnabled()) {
        QMainWindow::keyPressEvent(event);
        return;
    }
    
    switch (event->key()) {
        case Qt::Key_Left:
            onNavigatePreview(-1);
            event->accept();
            break;
        case Qt::Key_Right:
            onNavigatePreview(1);
            event->accept();
            break;
        case Qt::Key_Home:
            preview_dialog_->previewSlider()->setValue(0);
            event->accept();
            break;
        case Qt::Key_End:
            preview_dialog_->previewSlider()->setValue(preview_dialog_->previewSlider()->maximum());
            event->accept();
            break;
        case Qt::Key_PageUp:
            onNavigatePreview(-10);
            event->accept();
            break;
        case Qt::Key_PageDown:
            onNavigatePreview(10);
            event->accept();
            break;
        default:
            QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::loadProjectDAG()
{
    if (!dag_model_) {
        return;
    }
    
    ORC_LOG_DEBUG("MainWindow: loading project DAG for visualization");
    
    // QtNodes model will automatically sync with Project via OrcGraphModel
    // Just need to refresh the model to update the view
    dag_model_->refresh();
    
    statusBar()->showMessage("Loaded DAG from project", 2000);
}

void MainWindow::onEditParameters(const std::string& node_id)
{
    ORC_LOG_DEBUG("Edit parameters requested for node: {}", node_id);
    
    // Find the node in the project
    const auto& nodes = project_.coreProject().get_nodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
        [&node_id](const orc::ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == nodes.end()) {
        QMessageBox::warning(this, "Edit Parameters",
            QString("Node '%1' not found").arg(QString::fromStdString(node_id)));
        return;
    }
    
    std::string stage_name = node_it->stage_name;
    
    // Get the stage instance to access parameter descriptors
    auto& registry = orc::StageRegistry::instance();
    if (!registry.has_stage(stage_name)) {
        QMessageBox::warning(this, "Edit Parameters",
            QString("Unknown stage type '%1'").arg(QString::fromStdString(stage_name)));
        return;
    }
    
    auto stage = registry.create_stage(stage_name);
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
    auto current_values = node_it->parameters;
    
    // Show parameter dialog
    StageParameterDialog dialog(stage_name, param_descriptors, current_values, this);
    
    if (dialog.exec() == QDialog::Accepted) {
        auto new_values = dialog.get_values();
        orc::project_io::set_node_parameters(project_.coreProject(), node_id, new_values);
        
        // Rebuild DAG to pick up the new parameter values
        project_.rebuildDAG();
        
        // Update the preview renderer with the new DAG
        updatePreviewRenderer();
        
        // Refresh QtNodes view
        dag_model_->refresh();
        
        // Update the preview to show the changes
        updatePreview();
        
        statusBar()->showMessage(
            QString("Updated parameters for node '%1'")
                .arg(QString::fromStdString(node_id)),
            3000
        );
    }
}

void MainWindow::onTriggerStage(const std::string& node_id)
{
    ORC_LOG_DEBUG("Trigger stage requested for node: {}", node_id);
    
    try {
        std::string status;
        statusBar()->showMessage("Triggering stage...");
        
        bool success = orc::project_io::trigger_node(project_.coreProject(), node_id, status);
        
        statusBar()->showMessage(QString::fromStdString(status), success ? 5000 : 10000);
        
        if (success) {
            QMessageBox::information(this, "Trigger Complete", 
                QString::fromStdString(status));
        } else {
            QMessageBox::warning(this, "Trigger Failed", 
                QString::fromStdString(status));
        }
        
    } catch (const std::exception& e) {
        QString msg = QString("Error triggering stage: %1").arg(e.what());
        ORC_LOG_ERROR("{}", msg.toStdString());
        statusBar()->showMessage(msg, 5000);
        QMessageBox::critical(this, "Trigger Error", msg);
    }
}

void MainWindow::onNodeSelectedForView(const std::string& node_id)
{
    ORC_LOG_DEBUG("Main window: switching view to node '{}'", node_id);
    
    // Get available outputs for this node first to check if it's viewable
    // Note: Core's placeholder nodes (e.g., "_no_preview") always have outputs,
    // but user-selected SINK nodes from DAG editor won't have outputs
    if (preview_renderer_) {
        try {
            // Get available outputs for this node
            auto outputs = preview_renderer_->get_available_outputs(node_id);
            if (outputs.empty()) {
                // Real sink node selected by user - show message but keep current view
                statusBar()->showMessage(QString("Cannot view node '%1' - it has no outputs (sink node)")
                    .arg(QString::fromStdString(node_id)), 5000);
                ORC_LOG_WARN("Cannot view sink node '{}' - no outputs", node_id);
                
                // Don't change current_view_node_id_ or clear the preview
                // Just keep showing what we were showing before
                return;
            }
            
            // Node is viewable - update which node is being viewed
            current_view_node_id_ = node_id;
            available_outputs_ = outputs;
            
            // Show preview dialog only if:
            // 1) Not already visible
            // 2) Node has valid outputs that are truly available (not placeholders)
            // 3) Node is not a placeholder (like "_no_preview")
            // 4) Auto-show setting is enabled
            bool is_real_node = (node_id != "_no_preview");
            bool has_valid_content = false;
            for (const auto& output : outputs) {
                if (output.is_available) {
                    has_valid_content = true;
                    break;
                }
            }
            
            bool auto_show_enabled = auto_show_preview_action_ && auto_show_preview_action_->isChecked();
            
            if (!preview_dialog_->isVisible() && is_real_node && has_valid_content && auto_show_enabled) {
                preview_dialog_->show();
                show_preview_action_->setEnabled(true);
            }
            
            // Update preview dialog to show current node
            preview_dialog_->setCurrentNode(QString::fromStdString(node_id));
            
            // Update status bar to show which node is being viewed
            QString node_display = QString::fromStdString(node_id);
            statusBar()->showMessage(QString("Viewing output from node: %1").arg(node_display), 5000);
            
            // Use helper to update all viewer controls
            refreshViewerControls();
            
            // Update UI state to enable aspect ratio and other controls
            updateUIState();
        } catch (const std::exception& e) {
            ORC_LOG_WARN("Failed to get field count for node '{}': {}", node_id, e.what());
        }
    }
}

void MainWindow::onDAGModified()
{
    // QtNodes model automatically updates the Project via OrcGraphModel
    // The model change triggers this slot, so Project is already updated
    
    // Rebuild DAG to reflect the changes (new nodes, edges, etc.)
    project_.rebuildDAG();
    
    // Update the preview renderer with new DAG structure
    updatePreviewRenderer();
    
    // Refresh the displayed preview to show the changes
    updatePreview();
}

void MainWindow::onArrangeDAGToGrid()
{
    if (!dag_model_) {
        return;
    }
    
    // Simple grid layout - arrange nodes in a grid pattern
    const auto& nodes = project_.coreProject().get_nodes();
    const double grid_spacing_x = 250.0;
    const double grid_spacing_y = 150.0;
    const int cols = std::max(1, static_cast<int>(std::sqrt(nodes.size())));
    
    int row = 0;
    int col = 0;
    
    for (const auto& node : nodes) {
        double x = col * grid_spacing_x;
        double y = row * grid_spacing_y;
        
        orc::project_io::set_node_position(project_.coreProject(), node.node_id, x, y);
        
        col++;
        if (col >= cols) {
            col = 0;
            row++;
        }
    }
    
    // Refresh the view
    dag_model_->refresh();
    statusBar()->showMessage("Arranged DAG to grid", 2000);
}

void MainWindow::updatePreview()
{
    // If we have a preview renderer and a selected node, render at that node
    if (!preview_renderer_ || current_view_node_id_.empty()) {
        ORC_LOG_DEBUG("updatePreview: no preview renderer or node selected, returning");
        preview_dialog_->previewWidget()->clearImage();
        return;
    }
    
    int current_index = preview_dialog_->previewSlider()->value();
    
    ORC_LOG_DEBUG("updatePreview: rendering output type {} index {} at node '{}'", 
                  static_cast<int>(current_output_type_), current_index, current_view_node_id_);
    
    auto result = preview_renderer_->render_output(current_view_node_id_, current_output_type_, current_index);
    
    if (result.success) {
        preview_dialog_->previewWidget()->setImage(result.image);
    } else {
        // Rendering failed - show in status bar
        preview_dialog_->previewWidget()->clearImage();
        statusBar()->showMessage(
            QString("Render ERROR at node %1: %2")
                .arg(QString::fromStdString(current_view_node_id_))
                .arg(QString::fromStdString(result.error_message)),
            5000
        );
    }
}

void MainWindow::updatePreviewModeCombo()
{
    // Block signals while updating combo box
    preview_dialog_->previewModeCombo()->blockSignals(true);
    
    // Clear existing items
    preview_dialog_->previewModeCombo()->clear();
    
    // Populate from available outputs
    int current_type_index = 0;
    for (size_t i = 0; i < available_outputs_.size(); ++i) {
        const auto& output = available_outputs_[i];
        preview_dialog_->previewModeCombo()->addItem(QString::fromStdString(output.display_name));
        
        // Track which index matches current output type
        if (output.type == current_output_type_) {
            current_type_index = static_cast<int>(i);
        }
    }
    
    // Set current selection to match current output type
    if (!available_outputs_.empty()) {
        preview_dialog_->previewModeCombo()->setCurrentIndex(current_type_index);
        preview_dialog_->previewModeCombo()->setEnabled(true);
    } else {
        preview_dialog_->previewModeCombo()->setEnabled(false);
    }
    
    // Restore signals
    preview_dialog_->previewModeCombo()->blockSignals(false);
}

void MainWindow::updateAspectRatioCombo()
{
    if (!preview_renderer_) {
        return;
    }
    
    // Block signals while updating combo box
    preview_dialog_->aspectRatioCombo()->blockSignals(true);
    
    // Clear existing items
    preview_dialog_->aspectRatioCombo()->clear();
    
    // Get available modes from core
    auto available_modes = preview_renderer_->get_available_aspect_ratio_modes();
    auto current_mode = preview_renderer_->get_aspect_ratio_mode();
    
    // Populate combo from core data
    int current_index = 0;
    for (size_t i = 0; i < available_modes.size(); ++i) {
        const auto& mode_info = available_modes[i];
        preview_dialog_->aspectRatioCombo()->addItem(QString::fromStdString(mode_info.display_name));
        
        // Track which index matches current mode
        if (mode_info.mode == current_mode) {
            current_index = static_cast<int>(i);
        }
    }
    
    // Set current selection
    if (!available_modes.empty()) {
        preview_dialog_->aspectRatioCombo()->setCurrentIndex(current_index);
    }
    
    // Restore signals
    preview_dialog_->aspectRatioCombo()->blockSignals(false);
}

void MainWindow::refreshViewerControls()
{
    // This helper updates all viewer controls based on current node's available outputs
    // Should be called after available_outputs_ is populated
    
    if (!preview_renderer_ || current_view_node_id_.empty() || available_outputs_.empty()) {
        ORC_LOG_DEBUG("refreshViewerControls: no renderer, node, or outputs");
        return;
    }
    
    // Update the preview mode combo box
    updatePreviewModeCombo();
    
    // Get count for current output type
    int new_total = 0;
    for (const auto& output : available_outputs_) {
        if (output.type == current_output_type_) {
            new_total = output.count;
            break;
        }
    }
    
    // Update slider range and labels
    if (new_total > 0) {
        preview_dialog_->previewSlider()->setRange(0, new_total - 1);
        
        // Clamp current slider position to new range
        if (preview_dialog_->previewSlider()->value() >= new_total) {
            preview_dialog_->previewSlider()->setValue(0);
        }
        
        preview_dialog_->previewSlider()->setEnabled(true);
    }
    
    // Update the preview image
    updatePreview();
    
    // Update the info label and slider range labels (all done in one helper)
    updatePreviewInfo();
}

void MainWindow::updatePreviewRenderer()
{
    ORC_LOG_DEBUG("Updating preview renderer");
    
    // Get the DAG - could be null for empty projects, that's fine
    auto dag = project_.hasSource() ? project_.getDAG() : nullptr;
    
    // Debug: show what we're working with
    if (dag) {
        const auto& dag_nodes = dag->nodes();
        ORC_LOG_DEBUG("DAG contains {} nodes:", dag_nodes.size());
        for (const auto& node : dag_nodes) {
            ORC_LOG_DEBUG("  - {}", node.node_id);
        }
    } else {
        ORC_LOG_DEBUG("No DAG (new/empty project)");
    }
    
    // Always create/update the preview renderer - it handles null DAGs
    try {
        if (preview_renderer_) {
            // Update existing renderer with new DAG
            preview_renderer_->update_dag(dag);
        } else {
            // Create new renderer - works with null DAG
            preview_renderer_ = std::make_unique<orc::PreviewRenderer>(dag);
            
            // Populate aspect ratio combo from core and initialize aspect correction
            updateAspectRatioCombo();
            auto aspect_info = preview_renderer_->get_current_aspect_ratio_mode_info();
            preview_dialog_->previewWidget()->setAspectCorrection(aspect_info.correction_factor);
        }
        
        // Check if current node is still valid, or if we need to switch
        bool need_to_switch = false;
        std::string target_node;
        
        if (current_view_node_id_.empty()) {
            // No node selected yet - use suggestion
            need_to_switch = true;
        } else {
            // Check if current node still exists in DAG
            bool current_exists = false;
            if (dag) {
                for (const auto& node : dag->nodes()) {
                    if (node.node_id == current_view_node_id_) {
                        current_exists = true;
                        break;
                    }
                }
            }
            
            // If current node was deleted or is placeholder when real nodes exist, switch
            if (!current_exists && current_view_node_id_ != "_no_preview") {
                need_to_switch = true;
            } else if (current_view_node_id_ == "_no_preview" && dag && !dag->nodes().empty()) {
                need_to_switch = true;
            }
        }
        
        if (need_to_switch) {
            // Get suggestion from core
            auto suggestion = preview_renderer_->get_suggested_view_node();
            ORC_LOG_INFO("Switching to suggested node: {} ({})", 
                        suggestion.node_id, suggestion.message);
            onNodeSelectedForView(suggestion.node_id);
            statusBar()->showMessage(QString::fromStdString(suggestion.message), 3000);
        } else {
            // Keep current node - just refresh outputs and preview
            // (node parameters may have changed)
            ORC_LOG_DEBUG("Keeping current node '{}', refreshing preview", current_view_node_id_);
            if (preview_renderer_ && !current_view_node_id_.empty()) {
                available_outputs_ = preview_renderer_->get_available_outputs(current_view_node_id_);
                refreshViewerControls();
            }
        }
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Error creating/updating preview renderer: {}", e.what());
        statusBar()->showMessage(
            QString("Error with preview renderer: %1").arg(e.what()),
            5000
        );
    }
}

void MainWindow::onExportPNG()
{
    if (!preview_renderer_ || current_view_node_id_.empty()) {
        QMessageBox::information(this, "Export PNG", "No preview available to export.");
        return;
    }
    
    // Get filename from user
    QString filename = QFileDialog::getSaveFileName(
        this,
        "Export Preview as PNG",
        getLastProjectDirectory(),
        "PNG Images (*.png);;All Files (*)"
    );
    
    if (filename.isEmpty()) {
        return;  // User cancelled
    }
    
    // Ensure .png extension
    if (!filename.endsWith(".png", Qt::CaseInsensitive)) {
        filename += ".png";
    }
    
    // Remember directory
    setLastProjectDirectory(QFileInfo(filename).absolutePath());
    
    int current_index = preview_dialog_->previewSlider()->value();
    
    // Export using preview renderer
    bool success = preview_renderer_->save_png(
        current_view_node_id_,
        current_output_type_,
        current_index,
        filename.toStdString()
    );
    
    if (success) {
        statusBar()->showMessage(
            QString("Exported to: %1").arg(filename),
            5000
        );
        ORC_LOG_INFO("Exported PNG: {}", filename.toStdString());
    } else {
        QMessageBox::critical(
            this,
            "Export Failed",
            QString("Failed to export PNG to:\n%1").arg(filename)
        );
        ORC_LOG_ERROR("Failed to export PNG: {}", filename.toStdString());
    }
}

// Settings helpers

QString MainWindow::getLastProjectDirectory() const
{
    QSettings settings("orc-project", "orc-gui");
    QString dir = settings.value("lastProjectDirectory", QString()).toString();
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        return QDir::homePath();
    }
    return dir;
}

void MainWindow::setLastProjectDirectory(const QString& path)
{
    QSettings settings("orc-project", "orc-gui");
    settings.setValue("lastProjectDirectory", path);
}

void MainWindow::onNodeContextMenu(QtNodes::NodeId nodeId, const QPointF& pos)
{
    ORC_LOG_DEBUG("Context menu requested for node: {}", nodeId);
    
    // The OrcGraphicsScene already handles context menus
    // This slot is here for future extension if needed
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    // Future: Add event filtering if needed
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onQtNodeSelected(QtNodes::NodeId nodeId)
{
    if (!dag_model_) {
        return;
    }
    
    // Convert QtNodes ID to ORC node ID
    std::string orc_node_id = dag_model_->getOrcNodeId(nodeId);
    if (!orc_node_id.empty()) {
        ORC_LOG_DEBUG("QtNode {} selected -> ORC node '{}'", nodeId, orc_node_id);
        onNodeSelectedForView(orc_node_id);
    }
}

void MainWindow::onInspectStage(const std::string& node_id)
{
    ORC_LOG_DEBUG("Inspect stage requested for node: {}", node_id);
    
    // Find the node in the project
    const auto& nodes = project_.coreProject().get_nodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
        [&node_id](const orc::ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == nodes.end()) {
        ORC_LOG_ERROR("Node '{}' not found in project", node_id);
        QMessageBox::warning(this, "Inspection Failed",
            QString("Node '%1' not found.").arg(QString::fromStdString(node_id)));
        return;
    }
    
    const std::string& stage_name = node_it->stage_name;
    
    // Create a stage instance to generate the report
    try {
        auto& stage_registry = orc::StageRegistry::instance();
        if (!stage_registry.has_stage(stage_name)) {
            ORC_LOG_ERROR("Stage type '{}' not found in registry", stage_name);
            QMessageBox::warning(this, "Inspection Failed",
                QString("Stage type '%1' not found in registry.").arg(QString::fromStdString(stage_name)));
            return;
        }
        
        auto stage = stage_registry.create_stage(stage_name);
        
        // Apply the node's parameters to the stage
        auto* param_stage = dynamic_cast<orc::ParameterizedStage*>(stage.get());
        if (param_stage) {
            param_stage->set_parameters(node_it->parameters);
        }
        
        auto report = stage->generate_report();
        
        if (!report.has_value()) {
            QMessageBox::information(this, "Stage Inspection",
                QString("Stage '%1' does not support inspection reporting.")
                    .arg(QString::fromStdString(stage_name)));
            return;
        }
        
        // Show inspection dialog
        orc::InspectionDialog dialog(report.value(), this);
        dialog.exec();
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Failed to inspect stage '{}': {}", stage_name, e.what());
        QMessageBox::warning(this, "Inspection Failed",
            QString("Failed to inspect stage: %1").arg(e.what()));
    }
}

void MainWindow::runAnalysisForNode(orc::AnalysisTool* tool, const std::string& node_id, const std::string& stage_name)
{
    ORC_LOG_DEBUG("Running analysis '{}' for node '{}'", tool->name(), node_id);
    
    // Create analysis context
    orc::AnalysisContext context;
    context.node_id = node_id;
    context.source_type = orc::AnalysisSourceType::LaserDisc;  // TODO: Detect from project
    context.project = std::make_shared<orc::Project>(project_.coreProject());
    
    // Create DAG from project for analysis
    context.dag = orc::project_to_dag(project_.coreProject());
    
    // Show analysis dialog which handles the actual analysis execution
    orc::gui::AnalysisDialog dialog(tool, context, this);
    
    // Connect apply signal to handle applying results to the node
    connect(&dialog, &orc::gui::AnalysisDialog::applyToGraph, 
            [this, tool, node_id](const orc::AnalysisResult& result) {
        ORC_LOG_INFO("Applying analysis results to node '{}'", node_id);
        
        try {
            bool success = tool->applyToGraph(result, project_.coreProject(), node_id);
            
            if (success) {
                // Rebuild DAG and update preview to reflect changes
                project_.rebuildDAG();
                updatePreviewRenderer();
                dag_model_->refresh();
                updatePreview();
                
                statusBar()->showMessage(
                    QString("Applied analysis results to node '%1'")
                        .arg(QString::fromStdString(node_id)),
                    5000
                );
                QMessageBox::information(this, "Analysis Applied",
                    "Analysis results successfully applied to node.");
            } else {
                QMessageBox::warning(this, "Apply Failed",
                    "Failed to apply analysis results to node.");
            }
        } catch (const std::exception& e) {
            ORC_LOG_ERROR("Failed to apply analysis results: {}", e.what());
            QMessageBox::warning(this, "Apply Failed",
                QString("Error applying results: %1").arg(e.what()));
        }
    });
    
    dialog.exec();
}
