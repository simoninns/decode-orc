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
#include "dageditorwindow.h"
#include "dagviewerwidget.h"
#include "tbc_video_field_representation.h"
#include "logging.h"
#include "../core/include/dag_executor.h"
#include "../core/include/dag_field_renderer.h"
#include "../core/include/field_id.h"

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
#include <iostream>
#include <QSlider>
#include <QPushButton>
#include <QMessageBox>
#include <QDebug>
#include <QKeyEvent>
#include <QComboBox>
#include <QTabWidget>
#include <QSplitter>
#include <QApplication>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , preview_widget_(nullptr)
    , dag_editor_window_(nullptr)
    , field_slider_(nullptr)
    , field_info_label_(nullptr)
    , toolbar_(nullptr)
    , preview_mode_combo_(nullptr)
    , dag_editor_action_(nullptr)
    , save_project_action_(nullptr)
    , save_project_as_action_(nullptr)
    , field_renderer_(nullptr)
    , current_view_node_id_()
    , current_preview_mode_(PreviewMode::SingleField)
{
    setupUI();
    setupMenus();
    setupToolbar();
    
    updateWindowTitle();
    resize(1280, 720);
    
    updateUIState();
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUI()
{
    // Central widget with field preview
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    
    // Field preview widget
    preview_widget_ = new FieldPreviewWidget(this);
    layout->addWidget(preview_widget_, 1);
    
    // Navigation controls at bottom
    auto* nav_layout = new QHBoxLayout();
    
    // Preview mode selector
    preview_mode_combo_ = new QComboBox(this);
    preview_mode_combo_->addItem("Field View");
    preview_mode_combo_->addItem("Frame (Even+Odd)");
    preview_mode_combo_->addItem("Frame (Odd+Even)");
    preview_mode_combo_->setCurrentIndex(0);
    connect(preview_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onPreviewModeChanged);
    nav_layout->addWidget(preview_mode_combo_);
    
    // Spacer
    nav_layout->addSpacing(20);
    
    // Previous field button
    auto* prev_button = new QPushButton("<", this);
    prev_button->setMaximumWidth(50);
    prev_button->setAutoRepeat(true);
    prev_button->setAutoRepeatDelay(250);  // 250ms initial delay
    prev_button->setAutoRepeatInterval(10);  // 10ms repeat interval (very fast)
    connect(prev_button, &QPushButton::clicked, this, [this]() { onNavigateField(-1); });
    nav_layout->addWidget(prev_button);
    
    // Next field button
    auto* next_button = new QPushButton(">", this);
    next_button->setMaximumWidth(50);
    next_button->setAutoRepeat(true);
    next_button->setAutoRepeatDelay(250);  // 250ms initial delay
    next_button->setAutoRepeatInterval(10);  // 10ms repeat interval (very fast)
    connect(next_button, &QPushButton::clicked, this, [this]() { onNavigateField(1); });
    nav_layout->addWidget(next_button);
    
    // Field slider
    field_slider_ = new QSlider(Qt::Horizontal, this);
    field_slider_->setEnabled(false);
    connect(field_slider_, &QSlider::valueChanged, this, &MainWindow::onFieldChanged);
    nav_layout->addWidget(field_slider_, 1);
    
    // Field info label
    field_info_label_ = new QLabel("No source loaded", this);
    field_info_label_->setMinimumWidth(200);
    nav_layout->addWidget(field_info_label_);
    
    layout->addLayout(nav_layout);
    
    setCentralWidget(central);
    
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
    
    auto* quit_action = file_menu->addAction("&Quit");
    quit_action->setShortcut(QKeySequence::Quit);
    connect(quit_action, &QAction::triggered, this, &QWidget::close);
    
    // Tools menu
    auto* tools_menu = menuBar()->addMenu("&Tools");
    
    dag_editor_action_ = tools_menu->addAction("&DAG Editor...");
    dag_editor_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    dag_editor_action_->setEnabled(false);  // Disabled until project is loaded/created
    connect(dag_editor_action_, &QAction::triggered, this, &MainWindow::onOpenDAGEditor);
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
    
    // Close DAG editor if open
    if (dag_editor_window_) {
        dag_editor_window_->close();
        dag_editor_window_ = nullptr;
    }
    
    // Clear existing project state
    project_.clear();
    preview_widget_->setRepresentation(nullptr);
    field_slider_->setEnabled(false);
    field_slider_->setValue(0);
    
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
    
    statusBar()->showMessage(QString("Created new project: %1").arg(project_name));
}

void MainWindow::openProject(const QString& filename)
{
    ORC_LOG_INFO("Loading project: {}", filename.toStdString());
    
    // Close DAG editor if open
    if (dag_editor_window_) {
        dag_editor_window_->close();
        dag_editor_window_ = nullptr;
    }
    
    // Clear existing project state
    project_.clear();
    preview_widget_->setRepresentation(nullptr);
    field_slider_->setEnabled(false);
    field_slider_->setValue(0);
    
    QString error;
    if (!project_.loadFromFile(filename, &error)) {
        ORC_LOG_ERROR("Failed to load project: {}", error.toStdString());
        QMessageBox::critical(this, "Error", error);
        return;
    }
    
    ORC_LOG_DEBUG("Project loaded: {}", project_.projectName().toStdString());
    
    // Load representation if source exists
    if (project_.hasSource()) {
        ORC_LOG_DEBUG("Loading source representation");
        auto representation = project_.getSourceRepresentation();
        if (!representation) {
            ORC_LOG_WARN("Failed to load TBC representation");
            QMessageBox::warning(this, "Warning", "Failed to load TBC representation");
        } else {
            // Get field range from core
            auto range = representation->field_range();
            
            ORC_LOG_INFO("Source loaded: {} fields ({}..{})", 
                         range.size(), range.start.value(), range.end.value());
            
            // Update UI
            field_slider_->setEnabled(true);
            field_slider_->setRange(0, range.size() - 1);
            field_slider_->setValue(0);
            
            // Set representation in preview widget
            preview_widget_->setRepresentation(representation);
            preview_widget_->setFieldIndex(range.start.value());
            
            updateFieldInfo();
        }
    } else {
        ORC_LOG_DEBUG("Project has no source");
    }
    
    updateWindowTitle();
    updateUIState();
    
    // Initialize field renderer with project DAG
    updateDAGRenderer();
    
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
    
    // Enable/disable actions based on project state
    if (save_project_action_) {
        save_project_action_->setEnabled(has_project && project_.isModified());
    }
    if (save_project_as_action_) {
        save_project_as_action_->setEnabled(has_project);
    }
    if (dag_editor_action_) {
        // Allow DAG editor on any project, even without sources
        dag_editor_action_->setEnabled(has_project);
    }
    
    // Update window title to reflect modified state
    updateWindowTitle();
}

void MainWindow::onFieldChanged(int field_index)
{
    auto representation = project_.getSourceRepresentation();
    if (!representation) {
        return;
    }
    
    auto range = representation->field_range();
    if (field_index < 0 || field_index >= static_cast<int>(range.size())) {
        return;
    }
    
    orc::FieldID field_id = range.start + field_index;
    
    preview_widget_->setFieldIndex(field_id.value());
    updateFieldInfo();
}

void MainWindow::onNavigateField(int delta)
{
    auto representation = project_.getSourceRepresentation();
    if (!representation) {
        return;
    }
    
    // In frame view modes, move by 2 fields at a time
    int step = (current_preview_mode_ == PreviewMode::Frame_EvenOdd ||
                current_preview_mode_ == PreviewMode::Frame_OddEven) ? 2 : 1;
    
    auto range = representation->field_range();
    int current_index = field_slider_->value();
    int new_index = current_index + (delta * step);
    
    if (new_index >= 0 && new_index < static_cast<int>(range.size())) {
        field_slider_->setValue(new_index);
    }
}

void MainWindow::onPreviewModeChanged(int index)
{
    PreviewMode mode;
    switch (index) {
        case 0:
            mode = PreviewMode::SingleField;
            break;
        case 1:
            mode = PreviewMode::Frame_EvenOdd;
            break;
        case 2:
            mode = PreviewMode::Frame_OddEven;
            break;
        default:
            mode = PreviewMode::SingleField;
    }
    
    current_preview_mode_ = mode;
    
    // Set slider step size based on preview mode
    // In frame modes, slider moves by 2 fields at a time
    if (mode == PreviewMode::Frame_EvenOdd || mode == PreviewMode::Frame_OddEven) {
        field_slider_->setSingleStep(2);
        field_slider_->setPageStep(10);  // 5 frames
    } else {
        field_slider_->setSingleStep(1);
        field_slider_->setPageStep(10);
    }
    
    preview_widget_->setPreviewMode(mode);
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

void MainWindow::updateFieldInfo()
{
    auto representation = project_.getSourceRepresentation();
    if (!representation) {
        field_info_label_->setText("No source loaded");
        return;
    }
    
    auto range = representation->field_range();
    int current_index = field_slider_->value();
    int total = range.size();
    orc::FieldID field_id = range.start + current_index;
    
    PreviewMode mode = preview_widget_->previewMode();
    
    if (mode == PreviewMode::SingleField) {
        // Single field: show one field ID
        field_info_label_->setText(
            QString("Field %1 / %2 (ID: %3)")
                .arg(current_index + 1)
                .arg(total)
                .arg(field_id.value())
        );
    } else {
        // Frame view: show both field IDs
        orc::FieldID next_field_id = field_id + 1;
        if (current_index + 1 < total) {
            field_info_label_->setText(
                QString("Field %1-%2 / %3 (IDs: %4+%5)")
                    .arg(current_index + 1)
                    .arg(current_index + 2)
                    .arg(total)
                    .arg(field_id.value())
                    .arg(next_field_id.value())
            );
        } else {
            // Last field, can't make frame
            field_info_label_->setText(
                QString("Field %1 / %2 (ID: %3) [no next field]")
                    .arg(current_index + 1)
                    .arg(total)
                    .arg(field_id.value())
            );
        }
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    auto representation = project_.getSourceRepresentation();
    if (!representation) {
        QMainWindow::keyPressEvent(event);
        return;
    }
    
    switch (event->key()) {
        case Qt::Key_Left:
            onNavigateField(-1);
            break;
        case Qt::Key_Right:
            onNavigateField(1);
            break;
        case Qt::Key_Home:
            field_slider_->setValue(0);
            break;
        case Qt::Key_End:
            field_slider_->setValue(field_slider_->maximum());
            break;
        case Qt::Key_PageUp:
            onNavigateField(-10);
            break;
        case Qt::Key_PageDown:
            onNavigateField(10);
            break;
        default:
            QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::onOpenDAGEditor()
{
    // QPointer automatically becomes null when window is deleted (WA_DeleteOnClose)
    if (!dag_editor_window_) {
        // Create new window
        dag_editor_window_ = new DAGEditorWindow(this);
        
        // Connect project to DAG editor
        dag_editor_window_->setProject(&project_);
        
        // Connect modification signal to update UI state
        connect(dag_editor_window_, &DAGEditorWindow::projectModified,
                this, &MainWindow::updateUIState);
        
        // Connect DAG modification signal to update renderer
        connect(dag_editor_window_, &DAGEditorWindow::projectModified,
                this, &MainWindow::onDAGModified);
        
        // Connect node selection signal for field rendering
        connect(dag_editor_window_->dagViewer(), &DAGViewerWidget::nodeSelected,
                this, &MainWindow::onNodeSelectedForView);
        
        // Load the project's DAG into the editor
        dag_editor_window_->loadProjectDAG();
    }
    
    // Show the window (safe even if it was just created or already exists)
    if (dag_editor_window_) {
        dag_editor_window_->show();
        dag_editor_window_->raise();
        dag_editor_window_->activateWindow();
    }
}

void MainWindow::onNodeSelectedForView(const std::string& node_id)
{
    // Update which node is being viewed
    current_view_node_id_ = node_id;
    
    ORC_LOG_DEBUG("Main window: switching view to node '{}'", node_id);
    
    // Update status bar to show which node is being viewed
    QString node_display = QString::fromStdString(node_id);
    statusBar()->showMessage(QString("Viewing output from node: %1").arg(node_display), 5000);
    
    // Try to get the field count from the new node by rendering the first field
    if (field_renderer_) {
        try {
            // Render first field to get the representation at this node
            auto result = field_renderer_->render_field_at_node(node_id, orc::FieldID(0));
            if (result.is_valid && result.representation) {
                auto range = result.representation->field_range();
                int new_total = range.size();
                
                // Update slider range if it changed
                if (field_slider_->maximum() != new_total - 1) {
                    ORC_LOG_DEBUG("Node '{}' has {} fields", node_id, new_total);
                    field_slider_->setRange(0, new_total - 1);
                    
                    // Clamp current slider position to new range
                    if (field_slider_->value() >= new_total) {
                        field_slider_->setValue(new_total - 1);
                    }
                }
            }
        } catch (const std::exception& e) {
            ORC_LOG_WARN("Failed to get field count for node '{}': {}", node_id, e.what());
        }
    }
    
    // Re-render current field at the new node
    updateFieldView();
}

void MainWindow::onDAGModified()
{
    // Export DAG from viewer and save back to project
    if (dag_editor_window_ && dag_editor_window_->dagViewer()) {
        auto gui_dag = dag_editor_window_->dagViewer()->exportDAG();
        
        // Convert GUIDAG nodes back to ProjectDAGNodes
        std::vector<orc::ProjectDAGNode> nodes;
        for (const auto& gui_node : gui_dag.nodes) {
            orc::ProjectDAGNode node;
            node.node_id = gui_node.node_id;
            node.stage_name = gui_node.stage_name;
            node.node_type = gui_node.node_type;
            node.display_name = gui_node.display_name;
            node.x_position = gui_node.x_position;
            node.y_position = gui_node.y_position;
            node.parameters = gui_node.parameters;
            nodes.push_back(node);
        }
        
        // Convert GUIDAG edges back to ProjectDAGEdges
        std::vector<orc::ProjectDAGEdge> edges;
        for (const auto& gui_edge : gui_dag.edges) {
            orc::ProjectDAGEdge edge;
            edge.source_node_id = gui_edge.source_node_id;
            edge.target_node_id = gui_edge.target_node_id;
            edges.push_back(edge);
        }
        
        // Update project with new DAG
        orc::project_io::update_project_dag(project_.coreProject(), nodes, edges);
        
        // Rebuild the DAG from the updated project structure
        project_.rebuildDAG();
    }
    
    // Update renderer to use the project's DAG
    updateDAGRenderer();
    
    // Update UI state (field count, slider range, etc.) from core
    auto representation = project_.getSourceRepresentation();
    if (representation) {
        auto range = representation->field_range();
        int total = range.size();
        
        // Enable slider and set range based on actual field count from core
        field_slider_->setEnabled(true);
        field_slider_->setRange(0, total - 1);
        
        // Clamp slider to valid range
        if (field_slider_->value() >= total) {
            field_slider_->setValue(0);
        }
        
        updateFieldInfo();
    }
    
    // Re-render current field with updated DAG
    updateFieldView();
}

void MainWindow::updateFieldView()
{
    auto representation = project_.getSourceRepresentation();
    if (!representation) {
        ORC_LOG_DEBUG("updateFieldView: no representation, returning");
        return;
    }
    
    auto range = representation->field_range();
    int current_index = field_slider_->value();
    
    if (current_index >= static_cast<int>(range.size())) {
        ORC_LOG_DEBUG("updateFieldView: slider value {} out of range, returning", current_index);
        return;
    }
    
    orc::FieldID field_id = range.start + current_index;
    
    ORC_LOG_DEBUG("updateFieldView: rendering field {} at node '{}'", field_id.value(), current_view_node_id_);
    ORC_LOG_DEBUG("updateFieldView: field_renderer_ = {}, current_view_node_id_ = '{}'", 
                  (void*)field_renderer_.get(), current_view_node_id_);
    
    // If we have a field renderer and a selected node, render at that node
    if (field_renderer_ && !current_view_node_id_.empty()) {
        ORC_LOG_DEBUG("updateFieldView: calling render_field_at_node");
        auto result = field_renderer_->render_field_at_node(current_view_node_id_, field_id);
        
        if (result.is_valid && result.representation) {
            preview_widget_->setRepresentation(result.representation);
            preview_widget_->setFieldIndex(field_id.value());
            
            if (result.from_cache) {
                statusBar()->showMessage("(from cache)", 1000);
            }
        } else {
            // Rendering failed - show in status bar
            statusBar()->showMessage(
                QString("Render ERROR at node %1: %2")
                    .arg(QString::fromStdString(current_view_node_id_))
                    .arg(QString::fromStdString(result.error_message)),
                5000
            );
            // Fall back to source representation
            preview_widget_->setRepresentation(representation);
            preview_widget_->setFieldIndex(field_id.value());
        }
    } else {
        // No renderer or no node selected - show source
        preview_widget_->setRepresentation(representation);
        preview_widget_->setFieldIndex(field_id.value());
    }
}

void MainWindow::updateDAGRenderer()
{
    if (!project_.hasSource()) {
        field_renderer_.reset();
        current_view_node_id_.clear();
        return;
    }
    
    ORC_LOG_DEBUG("Updating DAG renderer");
    
    // Get the project's owned DAG (single instance)
    auto dag = project_.getDAG();
    if (!dag) {
        ORC_LOG_ERROR("Failed to build executable DAG from project");
        field_renderer_.reset();
        current_view_node_id_.clear();
        statusBar()->showMessage("Failed to build executable DAG", 5000);
        return;
    }
    
    // Debug: show what nodes are in the DAG
    const auto& dag_nodes = dag->nodes();
    ORC_LOG_DEBUG("DAG contains {} nodes:", dag_nodes.size());
    for (const auto& node : dag_nodes) {
        ORC_LOG_DEBUG("  - {}", node.node_id);
    }
    
    // Create or update field renderer with the project's DAG
    try {
        if (field_renderer_) {
            ORC_LOG_DEBUG("Updating existing field renderer with new DAG");
            field_renderer_->update_dag(dag);
        } else {
            ORC_LOG_DEBUG("Creating new field renderer");
            field_renderer_ = std::make_unique<orc::DAGFieldRenderer>(dag);
            
            // Default to viewing first source node
            auto nodes = field_renderer_->get_renderable_nodes();
            if (!nodes.empty()) {
                current_view_node_id_ = nodes[0];
                ORC_LOG_INFO("DAG renderer initialized, viewing node: {}", current_view_node_id_);
                statusBar()->showMessage(
                    QString("Viewing output from node: %1")
                        .arg(QString::fromStdString(current_view_node_id_)),
                    3000
                );
            } else {
                ORC_LOG_WARN("No renderable nodes found in DAG");
            }
        }
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Error creating/updating DAG renderer: {}", e.what());
        statusBar()->showMessage(
            QString("Error creating renderer: %1").arg(e.what()),
            5000
        );
        field_renderer_.reset();
        current_view_node_id_.clear();
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
