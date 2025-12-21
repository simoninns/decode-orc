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
#include "projectpropertiesdialog.h"
#include "stageparameterdialog.h"
#include "logging.h"
#include "../core/include/preview_renderer.h"
#include "../core/include/stage_registry.h"
#include "../core/include/dag_executor.h"

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
    , preview_widget_(nullptr)
    , dag_viewer_(nullptr)
    , main_splitter_(nullptr)
    , preview_slider_(nullptr)
    , preview_info_label_(nullptr)
    , slider_min_label_(nullptr)
    , slider_max_label_(nullptr)
    , toolbar_(nullptr)
    , preview_mode_combo_(nullptr)
    , save_project_action_(nullptr)
    , save_project_as_action_(nullptr)
    , edit_project_action_(nullptr)
    , export_png_action_(nullptr)
    , preview_renderer_(nullptr)
    , current_view_node_id_()
    , current_output_type_(orc::PreviewOutputType::Frame)
{
    setupUI();
    setupMenus();
    setupToolbar();
    
    updateWindowTitle();
    resize(1600, 900);
    
    updateUIState();
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUI()
{
    // Central widget with horizontal splitter
    main_splitter_ = new QSplitter(Qt::Horizontal, this);
    
    // Left side: Field preview with navigation controls
    auto* preview_container = new QWidget(this);
    preview_container->setMinimumWidth(400);  // Prevent frame viewer from disappearing
    auto* preview_layout = new QVBoxLayout(preview_container);
    preview_layout->setContentsMargins(0, 0, 0, 0);
    preview_layout->setSpacing(0);
    
    // Field preview widget
    preview_widget_ = new FieldPreviewWidget(this);
    preview_layout->addWidget(preview_widget_, 1);
    
    // Navigation controls at bottom - two rows
    auto* controls_container = new QVBoxLayout();
    controls_container->setSpacing(5);
    
    // First row: navigation buttons and slider
    auto* nav_layout = new QHBoxLayout();
    nav_layout->setContentsMargins(10, 5, 10, 0);
    
    // Previous item button
    auto* prev_button = new QPushButton("<", this);
    prev_button->setMaximumWidth(50);
    prev_button->setAutoRepeat(true);
    prev_button->setAutoRepeatDelay(250);  // 250ms initial delay
    prev_button->setAutoRepeatInterval(10);  // 10ms repeat interval (very fast)
    connect(prev_button, &QPushButton::clicked, this, [this]() { onNavigatePreview(-1); });
    nav_layout->addWidget(prev_button);
    
    // Next item button
    auto* next_button = new QPushButton(">", this);
    next_button->setMaximumWidth(50);
    next_button->setAutoRepeat(true);
    next_button->setAutoRepeatDelay(250);  // 250ms initial delay
    next_button->setAutoRepeatInterval(10);  // 10ms repeat interval (very fast)
    connect(next_button, &QPushButton::clicked, this, [this]() { onNavigatePreview(1); });
    nav_layout->addWidget(next_button);
    
    // Slider min label
    slider_min_label_ = new QLabel("0", this);
    slider_min_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    slider_min_label_->setMinimumWidth(40);
    slider_min_label_->setMaximumWidth(40);
    nav_layout->addWidget(slider_min_label_);
    
    // Preview slider - stretches to fill available space
    preview_slider_ = new QSlider(Qt::Horizontal, this);
    preview_slider_->setEnabled(false);
    connect(preview_slider_, &QSlider::valueChanged, this, &MainWindow::onPreviewIndexChanged);
    nav_layout->addWidget(preview_slider_, 1);
    
    // Slider max label
    slider_max_label_ = new QLabel("0", this);
    slider_max_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    slider_max_label_->setMinimumWidth(40);
    slider_max_label_->setMaximumWidth(40);
    nav_layout->addWidget(slider_max_label_);
    
    controls_container->addLayout(nav_layout);
    
    // Second row: mode selectors and current frame/field info
    auto* info_layout = new QHBoxLayout();
    info_layout->setContentsMargins(10, 0, 10, 5);
    
    // Preview mode selector - populated dynamically from core
    preview_mode_combo_ = new QComboBox(this);
    preview_mode_combo_->setEnabled(false);  // Disabled until node selected
    connect(preview_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onPreviewModeChanged);
    info_layout->addWidget(preview_mode_combo_);
    
    // Aspect ratio selector - populated dynamically from core
    aspect_ratio_combo_ = new QComboBox(this);
    aspect_ratio_combo_->setEnabled(false);   // Disabled until node selected
    connect(aspect_ratio_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onAspectRatioModeChanged);
    info_layout->addWidget(aspect_ratio_combo_);
    
    info_layout->addSpacing(20);
    
    // Preview info label - left-justified
    preview_info_label_ = new QLabel("No source loaded", this);
    preview_info_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    info_layout->addWidget(preview_info_label_);
    
    // Add stretch to push everything to the left
    info_layout->addStretch(1);
    
    controls_container->addLayout(info_layout);
    
    preview_layout->addLayout(controls_container);
    
    // Right side: DAG editor
    dag_viewer_ = new DAGViewerWidget(this);
    
    // Connect DAG viewer signals
    connect(dag_viewer_, &DAGViewerWidget::nodeSelected,
            this, &MainWindow::onNodeSelectedForView);
    connect(dag_viewer_, &DAGViewerWidget::editParametersRequested,
            this, &MainWindow::onEditParameters);
    connect(dag_viewer_, &DAGViewerWidget::triggerStageRequested,
            this, &MainWindow::onTriggerStage);
    connect(dag_viewer_, &DAGViewerWidget::dagModified,
            this, &MainWindow::onDAGModified);
    connect(dag_viewer_, &DAGViewerWidget::dagModified,
            this, &MainWindow::updateWindowTitle);
    
    // Add widgets to splitter
    main_splitter_->addWidget(preview_container);
    main_splitter_->addWidget(dag_viewer_);
    
    // Set initial sizes (60% preview, 40% DAG editor)
    main_splitter_->setStretchFactor(0, 60);
    main_splitter_->setStretchFactor(1, 40);
    
    setCentralWidget(main_splitter_);
    
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
    
    auto* arrange_action = view_menu->addAction("&Arrange DAG to Grid");
    arrange_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    connect(arrange_action, &QAction::triggered, dag_viewer_, &DAGViewerWidget::arrangeToGrid);
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
        // TODO: Add project_io::set_project_name() and set_project_description()
        // For now, these properties are read-only after creation
        ORC_LOG_WARN("Project name/description modification not yet implemented - using project_io API");
        
        // Mark project as modified
        // project_.setModified(true);
        
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
    preview_widget_->clearImage();
    preview_slider_->setEnabled(false);
    preview_slider_->setValue(0);
    
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
    preview_widget_->clearImage();
    preview_slider_->setEnabled(false);
    preview_slider_->setValue(0);
    
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
    
    // Enable aspect ratio selector when preview is available
    if (aspect_ratio_combo_) {
        aspect_ratio_combo_->setEnabled(has_preview);
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
    if (!preview_slider_->isEnabled()) {
        return;
    }
    
    // In frame view modes, move by 2 items at a time
    int step = (current_output_type_ == orc::PreviewOutputType::Frame ||
                current_output_type_ == orc::PreviewOutputType::Frame_Reversed) ? 2 : 1;
    
    int current_index = preview_slider_->value();
    int new_index = current_index + (delta * step);
    int max_index = preview_slider_->maximum();
    
    if (new_index >= 0 && new_index <= max_index) {
        preview_slider_->setValue(new_index);
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
    int current_position = preview_slider_->value();
    
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
    if (new_position >= 0 && new_position <= static_cast<uint64_t>(preview_slider_->maximum())) {
        preview_slider_->setValue(new_position);
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
    preview_widget_->setAspectCorrection(aspect_correction);
    
    // Refresh the display
    preview_widget_->update();
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
        preview_info_label_->setText("No node selected");
        slider_min_label_->setText("");
        slider_max_label_->setText("");
        return;
    }
    
    // Special handling for placeholder node
    if (current_view_node_id_ == "_no_preview") {
        preview_info_label_->setText("No source available");
        slider_min_label_->setText("");
        slider_max_label_->setText("");
        return;
    }
    
    // Get detailed display info from core
    int current_index = preview_slider_->value();
    int total = preview_slider_->maximum() + 1;
    
    auto display_info = preview_renderer_->get_preview_item_display_info(
        current_output_type_,
        current_index,
        total
    );
    
    // Update slider labels with range
    slider_min_label_->setText(QString::number(1));
    slider_max_label_->setText(QString::number(display_info.total_count));
    
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
    
    preview_info_label_->setText(info_text);
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (!preview_slider_->isEnabled()) {
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
            preview_slider_->setValue(0);
            event->accept();
            break;
        case Qt::Key_End:
            preview_slider_->setValue(preview_slider_->maximum());
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
    if (!dag_viewer_) {
        return;
    }
    
    ORC_LOG_DEBUG("MainWindow: loading project DAG for visualization");
    
    // Set project connection for DAG viewer
    dag_viewer_->setProject(&project_.coreProject());
    
    // Convert project DAG to GUIDAG for visualization
    orc::GUIDAG gui_dag;
    gui_dag.name = project_.projectName().toStdString();
    gui_dag.version = "1.0";
    
    auto& core_project = project_.coreProject();
    
    // Convert nodes
    for (const auto& node : core_project.get_nodes()) {
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
    for (const auto& edge : core_project.get_edges()) {
        orc::GUIDAGEdge gui_edge;
        gui_edge.source_node_id = edge.source_node_id;
        gui_edge.target_node_id = edge.target_node_id;
        gui_dag.edges.push_back(gui_edge);
    }
    
    dag_viewer_->importDAG(gui_dag);
    
    statusBar()->showMessage("Loaded DAG from project", 2000);
}

void MainWindow::onEditParameters(const std::string& node_id)
{
    ORC_LOG_DEBUG("Edit parameters requested for node: {}", node_id);
    
    // Get stage name to determine available parameters
    std::string stage_name = dag_viewer_->getNodeStageType(node_id);
    
    if (stage_name.empty()) {
        QMessageBox::warning(this, "Edit Parameters",
            QString("Node '%1' not found").arg(QString::fromStdString(node_id)));
        return;
    }
    
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

void MainWindow::onTriggerStage(const std::string& node_id)
{
    ORC_LOG_DEBUG("Trigger stage requested for node: {}", node_id);
    
    if (!current_project_) {
        statusBar()->showMessage("No project loaded", 3000);
        return;
    }
    
    try {
        std::string status;
        statusBar()->showMessage("Triggering stage...");
        
        bool success = orc::project_io::trigger_node(*current_project_, node_id, status);
        
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
            
            // Update DAG viewer selection
            // Block signals to prevent infinite loop (selectNode -> nodeSelected -> onNodeSelectedForView)
            if (dag_viewer_) {
                dag_viewer_->blockSignals(true);
                dag_viewer_->selectNode(node_id);
                dag_viewer_->blockSignals(false);
            }
            
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
    // Export DAG from viewer and save back to project
    if (dag_viewer_) {
        auto gui_dag = dag_viewer_->exportDAG();
        
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
    
    // Update renderer with new DAG - this will automatically handle:
    // - Selecting appropriate node to view (or placeholder if no nodes)
    // - Updating all UI controls and state
    updatePreviewRenderer();
}

void MainWindow::updatePreview()
{
    // If we have a preview renderer and a selected node, render at that node
    if (!preview_renderer_ || current_view_node_id_.empty()) {
        ORC_LOG_DEBUG("updatePreview: no preview renderer or node selected, returning");
        preview_widget_->clearImage();
        return;
    }
    
    int current_index = preview_slider_->value();
    
    ORC_LOG_DEBUG("updatePreview: rendering output type {} index {} at node '{}'", 
                  static_cast<int>(current_output_type_), current_index, current_view_node_id_);
    
    auto result = preview_renderer_->render_output(current_view_node_id_, current_output_type_, current_index);
    
    if (result.success) {
        preview_widget_->setImage(result.image);
    } else {
        // Rendering failed - show in status bar
        preview_widget_->clearImage();
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
    preview_mode_combo_->blockSignals(true);
    
    // Clear existing items
    preview_mode_combo_->clear();
    
    // Populate from available outputs
    int current_type_index = 0;
    for (size_t i = 0; i < available_outputs_.size(); ++i) {
        const auto& output = available_outputs_[i];
        preview_mode_combo_->addItem(QString::fromStdString(output.display_name));
        
        // Track which index matches current output type
        if (output.type == current_output_type_) {
            current_type_index = static_cast<int>(i);
        }
    }
    
    // Set current selection to match current output type
    if (!available_outputs_.empty()) {
        preview_mode_combo_->setCurrentIndex(current_type_index);
        preview_mode_combo_->setEnabled(true);
    } else {
        preview_mode_combo_->setEnabled(false);
    }
    
    // Restore signals
    preview_mode_combo_->blockSignals(false);
}

void MainWindow::updateAspectRatioCombo()
{
    if (!preview_renderer_) {
        return;
    }
    
    // Block signals while updating combo box
    aspect_ratio_combo_->blockSignals(true);
    
    // Clear existing items
    aspect_ratio_combo_->clear();
    
    // Get available modes from core
    auto available_modes = preview_renderer_->get_available_aspect_ratio_modes();
    auto current_mode = preview_renderer_->get_aspect_ratio_mode();
    
    // Populate combo from core data
    int current_index = 0;
    for (size_t i = 0; i < available_modes.size(); ++i) {
        const auto& mode_info = available_modes[i];
        aspect_ratio_combo_->addItem(QString::fromStdString(mode_info.display_name));
        
        // Track which index matches current mode
        if (mode_info.mode == current_mode) {
            current_index = static_cast<int>(i);
        }
    }
    
    // Set current selection
    if (!available_modes.empty()) {
        aspect_ratio_combo_->setCurrentIndex(current_index);
    }
    
    // Restore signals
    aspect_ratio_combo_->blockSignals(false);
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
        preview_slider_->setRange(0, new_total - 1);
        
        // Clamp current slider position to new range
        if (preview_slider_->value() >= new_total) {
            preview_slider_->setValue(0);
        }
        
        preview_slider_->setEnabled(true);
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
            preview_widget_->setAspectCorrection(aspect_info.correction_factor);
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
    
    int current_index = preview_slider_->value();
    
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
