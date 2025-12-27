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
#include "vbidialog.h"
#include "dropoutanalysisdialog.h"
#include "projectpropertiesdialog.h"
#include "stageparameterdialog.h"
#include "inspection_dialog.h"
#include "analysis/analysis_dialog.h"
#include "analysis/vectorscope_dialog.h"
#include "orcgraphicsview.h"
#include "logging.h"
#include "../core/include/preview_renderer.h"
#include "../core/include/vbi_decoder.h"
#include "../core/include/dropout_analysis_decoder.h"
#include "../core/include/stage_registry.h"
#include "../core/include/dag_executor.h"
#include "../core/include/project_to_dag.h"
#include "../core/stages/chroma_sink/chroma_sink_stage.h"
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
#include <QCloseEvent>
#include <QShowEvent>
#include <QDateTime>
#include <QTimer>
#include <QMoveEvent>
#include <QResizeEvent>

#include <queue>
#include <map>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , preview_dialog_(nullptr)
    , vbi_dialog_(nullptr)
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
    , vbi_decoder_(nullptr)
    , dropout_decoder_(nullptr)
    , current_view_node_id_()
    , last_dropout_node_id_()
    , last_dropout_mode_(orc::DropoutAnalysisMode::FULL_FIELD)
    , last_dropout_output_type_(orc::PreviewOutputType::Frame)
    , current_output_type_(orc::PreviewOutputType::Frame)
    , current_option_id_("frame")  // Default to "Frame (Y)" option
    , preview_update_timer_(nullptr)
    , pending_preview_index_(-1)
    , preview_update_pending_(false)
    , last_preview_update_time_(0)
    , last_update_was_sequential_(false)
{
    // Create preview update throttling timer
    preview_update_timer_ = new QTimer(this);
    preview_update_timer_->setSingleShot(false);  // Repeating for smooth throttling
    preview_update_timer_->setInterval(33);  // ~30fps max update rate for smooth scrubbing
    connect(preview_update_timer_, &QTimer::timeout, this, [this]() {
        if (preview_update_pending_) {
            updateAllPreviewComponents();
            preview_update_pending_ = false;
            last_preview_update_time_ = QDateTime::currentMSecsSinceEpoch();
        }
    });
    
    setupUI();
    setupMenus();
    setupToolbar();
    
    updateWindowTitle();
    
    // Restore window geometry and state from settings
    restoreSettings();
    
    updateUIState();
}

MainWindow::~MainWindow()
{
    // Explicitly disconnect and delete DAG scene/model/view to avoid Qt teardown assertions
    if (dag_scene_) {
        dag_scene_->disconnect();
        delete dag_scene_;
        dag_scene_ = nullptr;
    }
    if (dag_model_) {
        delete dag_model_;
        dag_model_ = nullptr;
    }
    if (dag_view_) {
        delete dag_view_;
        dag_view_ = nullptr;
    }
    
    if (vbi_dialog_) {
        delete vbi_dialog_;
        vbi_dialog_ = nullptr;
    }
    if (preview_dialog_) {
        delete preview_dialog_;
        preview_dialog_ = nullptr;
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::saveSettings()
{
    QSettings settings("orc-project", "orc-gui");
    
    // Save main window geometry and state
    settings.setValue("mainwindow/geometry", saveGeometry());
    settings.setValue("mainwindow/state", saveState());
    
    // Save preview dialog geometry (ld-analyse pattern)
    settings.setValue("previewdialog/geometry", preview_dialog_->saveGeometry());
}

void MainWindow::restoreSettings()
{
    QSettings settings("orc-project", "orc-gui");
    
    // Restore main window geometry and state
    if (settings.contains("mainwindow/geometry")) {
        restoreGeometry(settings.value("mainwindow/geometry").toByteArray());
    } else {
        resize(1200, 800);
    }
    
    if (settings.contains("mainwindow/state")) {
        restoreState(settings.value("mainwindow/state").toByteArray());
    }
    
    // Restore preview dialog geometry (ld-analyse pattern)
    if (settings.contains("previewdialog/geometry")) {
        preview_dialog_->restoreGeometry(settings.value("previewdialog/geometry").toByteArray());
    }
}

void MainWindow::setupUI()
{
    // Create preview dialog (initially hidden)
    preview_dialog_ = new PreviewDialog(this);
    
    // Create VBI dialog (initially hidden)
    vbi_dialog_ = new VBIDialog(this);
    
    // Create dropout analysis dialog (initially hidden)
    dropout_analysis_dialog_ = new DropoutAnalysisDialog(this);
    
    // Connect dropout analysis dialog signals
    connect(dropout_analysis_dialog_, &DropoutAnalysisDialog::modeChanged,
            this, &MainWindow::updateDropoutAnalysisDialog);
    
    // Connect preview dialog signals
    connect(preview_dialog_, &PreviewDialog::previewIndexChanged,
            this, &MainWindow::onPreviewIndexChanged);
    connect(preview_dialog_, &PreviewDialog::sequentialPreviewRequested,
            this, [this](int) { last_update_was_sequential_ = true; });
    connect(preview_dialog_, &PreviewDialog::previewModeChanged,
            this, &MainWindow::onPreviewModeChanged);
    connect(preview_dialog_, &PreviewDialog::aspectRatioModeChanged,
            this, &MainWindow::onAspectRatioModeChanged);
    connect(preview_dialog_, &PreviewDialog::exportPNGRequested,
            this, &MainWindow::onExportPNG);
        connect(preview_dialog_, &PreviewDialog::showVBIDialogRequested,
            this, &MainWindow::onShowVBIDialog);
    connect(preview_dialog_, &PreviewDialog::showDropoutAnalysisDialogRequested,
            this, &MainWindow::onShowDropoutAnalysisDialog);
    
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
    
    // New Project submenu with NTSC and PAL options
    auto* new_project_menu = file_menu->addMenu("&New Project");
    
    auto* new_ntsc_project_action = new_project_menu->addAction("New &NTSC Project...");
    new_ntsc_project_action->setShortcut(QKeySequence::New);
    connect(new_ntsc_project_action, &QAction::triggered, this, &MainWindow::onNewNTSCProject);
    
    auto* new_pal_project_action = new_project_menu->addAction("New &PAL Project...");
    connect(new_pal_project_action, &QAction::triggered, this, &MainWindow::onNewPALProject);
    
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
    connect(show_preview_action_, &QAction::triggered, this, [this]() {
        preview_dialog_->show();
    });
    
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
    // Default to NTSC for backward compatibility
    newProject(orc::VideoSystem::NTSC);
}

void MainWindow::onNewNTSCProject()
{
    newProject(orc::VideoSystem::NTSC);
}

void MainWindow::onNewPALProject()
{
    newProject(orc::VideoSystem::PAL);
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


void MainWindow::newProject(orc::VideoSystem video_format)
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
    if (!project_.newEmptyProject(project_name, video_format, &error)) {
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
    // Throttle preview updates to avoid excessive rendering during slider scrubbing
    // Use throttling (not debouncing) for smooth real-time feedback
    
    pending_preview_index_ = index;
    preview_update_pending_ = true;
    
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 time_since_last_update = now - last_preview_update_time_;
    
    // If enough time has passed, update immediately
    if (time_since_last_update >= preview_update_timer_->interval()) {
        updateAllPreviewComponents();
        preview_update_pending_ = false;
        last_preview_update_time_ = now;
    } else if (!preview_update_timer_->isActive()) {
        // Start timer if not already running
        preview_update_timer_->start();
    }
    // Otherwise, timer is already running and will pick up the pending update
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
    
    // Update to new type and option ID
    current_output_type_ = available_outputs_[index].type;
    current_option_id_ = available_outputs_[index].option_id;
    
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
    
    // Get the correction factor - use per-output DAR correction if in DAR mode
    double aspect_correction = available_modes[index].correction_factor;
    if (available_modes[index].mode == orc::AspectRatioMode::DAR_4_3 && !available_outputs_.empty()) {
        // Find current output and use its dar_aspect_correction
        for (const auto& output : available_outputs_) {
            if (output.option_id == current_option_id_) {
                aspect_correction = output.dar_aspect_correction;
                ORC_LOG_DEBUG("Using per-output DAR correction: {}", aspect_correction);
                break;
            }
        }
    }
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
    
    ORC_LOG_DEBUG("updatePreviewInfo: current_output_type={}, index={}, total={}",
                  static_cast<int>(current_output_type_), current_index, total);
    
    auto display_info = preview_renderer_->get_preview_item_display_info(
        current_output_type_,
        current_index,
        total
    );
    
    ORC_LOG_DEBUG("updatePreviewInfo: display_info.type_name='{}', has_field_info={}",
                  display_info.type_name, display_info.has_field_info);
    
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
    
    // Try to get the stage instance from the DAG first (so it has cached input data)
    // If that fails, create a new temporary instance
    std::shared_ptr<orc::DAGStage> stage;
    orc::ParameterizedStage* param_stage = nullptr;
    
    auto dag = project_.getDAG();
    if (dag) {
        auto dag_nodes = dag->nodes();
        auto dag_node_it = std::find_if(dag_nodes.begin(), dag_nodes.end(),
            [&node_id](const orc::DAGNode& n) { return n.node_id == node_id; });
        
        if (dag_node_it != dag_nodes.end()) {
            stage = dag_node_it->stage;
            param_stage = dynamic_cast<orc::ParameterizedStage*>(stage.get());
            ORC_LOG_DEBUG("Using DAG stage instance for parameter descriptors (has cached data)");
        }
    }
    
    // Fallback to creating new instance if DAG stage not available
    if (!param_stage) {
        stage = registry.create_stage(stage_name);
        param_stage = dynamic_cast<orc::ParameterizedStage*>(stage.get());
        ORC_LOG_DEBUG("Using new stage instance for parameter descriptors (no cached data)");
    }
    
    if (!param_stage) {
        QMessageBox::information(this, "Edit Parameters",
            QString("Stage '%1' does not have configurable parameters")
                .arg(QString::fromStdString(stage_name)));
        return;
    }
    
    // Get parameter descriptors with project video format context
    auto param_descriptors = param_stage->get_parameter_descriptors(project_.coreProject().get_video_format());
    
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
            
            // Try to preserve the current option_id when switching nodes
            // This allows users to stay in the same preview mode (e.g., "frame") across different stages
            ORC_LOG_DEBUG("onNodeClicked: trying to preserve option_id '{}'", current_option_id_);
            bool found_match = false;
            for (const auto& output : outputs) {
                if (output.option_id == current_option_id_) {
                    current_output_type_ = output.type;
                    found_match = true;
                    ORC_LOG_DEBUG("onNodeClicked: found match for option_id '{}', output_type={}", 
                                  current_option_id_, static_cast<int>(current_output_type_));
                    break;
                }
            }
            
            // If current option not available, try to find a sensible default
            if (!found_match && !outputs.empty()) {
                // Prefer "frame" (Frame (Y)) if available, otherwise use first output
                bool found_frame = false;
                for (const auto& output : outputs) {
                    if (output.option_id == "frame") {
                        current_output_type_ = output.type;
                        current_option_id_ = output.option_id;
                        found_frame = true;
                        break;
                    }
                }
                if (!found_frame) {
                    current_output_type_ = outputs[0].type;
                    current_option_id_ = outputs[0].option_id;
                }
            }
            
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
            
            // Enable the Show Preview menu action whenever there's valid content
            if (is_real_node && has_valid_content) {
                show_preview_action_->setEnabled(true);
            }
            
            // Auto-show the preview dialog only if the setting is enabled
            if (!preview_dialog_->isVisible() && is_real_node && has_valid_content && auto_show_enabled) {
                preview_dialog_->show();
            }
            
            // Update preview dialog to show current node
            // Get node label from project (prefer user_label, fallback to display_name)
            const auto& nodes = project_.coreProject().get_nodes();
            auto node_it = std::find_if(nodes.begin(), nodes.end(),
                [&node_id](const orc::ProjectDAGNode& n) { return n.node_id == node_id; });
            QString node_label;
            if (node_it != nodes.end()) {
                if (!node_it->user_label.empty()) {
                    node_label = QString::fromStdString(node_it->user_label);
                } else if (!node_it->display_name.empty()) {
                    node_label = QString::fromStdString(node_it->display_name);
                } else {
                    node_label = QString::fromStdString(node_id);
                }
            } else {
                node_label = QString::fromStdString(node_id);
            }
            preview_dialog_->setCurrentNode(node_label, QString::fromStdString(node_id));
            
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
    
    // Hierarchical layout - arrange nodes left to right based on topological order
    const auto& nodes = project_.coreProject().get_nodes();
    const auto& edges = project_.coreProject().get_edges();
    
    if (nodes.empty()) {
        return;
    }
    
    const double grid_spacing_x = 300.0;
    const double grid_spacing_y = 150.0;
    
    // Build adjacency list (forward edges: source -> targets)
    std::map<std::string, std::vector<std::string>> forward_edges;
    std::map<std::string, int> in_degree;
    
    // Initialize in-degree for all nodes
    for (const auto& node : nodes) {
        in_degree[node.node_id] = 0;
        forward_edges[node.node_id] = {};
    }
    
    // Count in-degrees and build forward edge list
    for (const auto& edge : edges) {
        in_degree[edge.target_node_id]++;
        forward_edges[edge.source_node_id].push_back(edge.target_node_id);
    }
    
    // Calculate depth level for each node (BFS from sources)
    std::map<std::string, int> node_depth;
    std::queue<std::string> queue;
    
    // Start with source nodes (in-degree 0)
    for (const auto& [node_id, degree] : in_degree) {
        if (degree == 0) {
            node_depth[node_id] = 0;
            queue.push(node_id);
        }
    }
    
    // BFS to assign depths
    while (!queue.empty()) {
        std::string current_id = queue.front();
        queue.pop();
        
        int current_depth = node_depth[current_id];
        
        // Process all targets of this node
        for (const auto& target_id : forward_edges[current_id]) {
            // Update depth if not set or if we found a longer path
            if (node_depth.find(target_id) == node_depth.end()) {
                node_depth[target_id] = current_depth + 1;
                queue.push(target_id);
            } else if (node_depth[target_id] < current_depth + 1) {
                node_depth[target_id] = current_depth + 1;
                queue.push(target_id);
            }
        }
    }
    
    // Group nodes by depth level
    std::map<int, std::vector<std::string>> levels;
    for (const auto& [node_id, depth] : node_depth) {
        levels[depth].push_back(node_id);
    }
    
    // Position nodes: each depth level gets a column
    for (const auto& [depth, level_nodes] : levels) {
        double x = depth * grid_spacing_x;
        
        // Center nodes vertically within this level
        for (size_t i = 0; i < level_nodes.size(); ++i) {
            double y = i * grid_spacing_y;
            orc::project_io::set_node_position(project_.coreProject(), level_nodes[i], x, y);
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
    
    // Determine navigation hint based on how the update was triggered
    orc::PreviewNavigationHint hint = last_update_was_sequential_ ? 
        orc::PreviewNavigationHint::Sequential : orc::PreviewNavigationHint::Random;
    
    ORC_LOG_DEBUG("updatePreview: rendering output type {} option_id '{}' index {} at node '{}' hint={}", 
                  static_cast<int>(current_output_type_), current_option_id_, current_index, current_view_node_id_,
                  (hint == orc::PreviewNavigationHint::Sequential ? "Sequential" : "Random"));
    
    auto result = preview_renderer_->render_output(current_view_node_id_, current_output_type_, current_index, current_option_id_, hint);
    
    // Reset the sequential flag after use
    last_update_was_sequential_ = false;
    
    if (result.success) {
        preview_dialog_->previewWidget()->setImage(result.image);
        updateVectorscope(current_view_node_id_, result.image);
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
    
    // Also update vectorscope dialogs for other nodes (not the current view node)
    for (const auto& [vs_node_id, vs_dialog] : vectorscope_dialogs_) {
        if (vs_node_id == current_view_node_id_) continue;  // Already updated above
        if (!vs_dialog || !vs_dialog->isVisible()) continue;
        
        // Render this node's preview for the vectorscope
        auto vs_result = preview_renderer_->render_output(vs_node_id, orc::PreviewOutputType::Frame, current_index, "frame", hint);
        if (vs_result.success) {
            updateVectorscope(vs_node_id, vs_result.image);
        }
    }
}

void MainWindow::updateVectorscope(const std::string& node_id, const orc::PreviewImage& image)
{
    auto it = vectorscope_dialogs_.find(node_id);
    if (it == vectorscope_dialogs_.end()) return;
    auto* dialog = it->second;
    if (!dialog || !dialog->isVisible()) return;
    if (!image.vectorscope_data.has_value()) return;

    uint64_t field_number = 0;
    if (current_output_type_ == orc::PreviewOutputType::Frame ||
        current_output_type_ == orc::PreviewOutputType::Frame_Reversed ||
        current_output_type_ == orc::PreviewOutputType::Split) {
        field_number = static_cast<uint64_t>(preview_dialog_->previewSlider()->value()) * 2 + 1;
    } else {
        field_number = static_cast<uint64_t>(preview_dialog_->previewSlider()->value()) + 1;
    }
    dialog->updateForField(field_number, &*image.vectorscope_data);
}

void MainWindow::updatePreviewModeCombo()
{
    ORC_LOG_DEBUG("updatePreviewModeCombo: current_output_type={}, current_option_id='{}'",
                  static_cast<int>(current_output_type_), current_option_id_);
    
    // Block signals while updating combo box
    preview_dialog_->previewModeCombo()->blockSignals(true);
    
    // Clear existing items
    preview_dialog_->previewModeCombo()->clear();
    
    // Populate from available outputs
    int current_type_index = 0;
    for (size_t i = 0; i < available_outputs_.size(); ++i) {
        const auto& output = available_outputs_[i];
        preview_dialog_->previewModeCombo()->addItem(QString::fromStdString(output.display_name));
        
        ORC_LOG_DEBUG("  output[{}]: type={}, option_id='{}', display_name='{}'",
                      i, static_cast<int>(output.type), output.option_id, output.display_name);
        
        // Track which index matches current output type AND option_id
        // (multiple outputs can have same type, e.g., Split (Y) and Split (Raw))
        if (output.type == current_output_type_ && output.option_id == current_option_id_) {
            current_type_index = static_cast<int>(i);
            ORC_LOG_DEBUG("  -> MATCH at index {}", i);
        }
    }
    
    ORC_LOG_DEBUG("updatePreviewModeCombo: setting combo to index {}", current_type_index);
    
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
    
    // Get count for current output type and option_id, and update aspect correction
    int new_total = 0;
    double dar_correction = 0.7;  // Default fallback
    for (const auto& output : available_outputs_) {
        if (output.type == current_output_type_ && output.option_id == current_option_id_) {
            new_total = output.count;
            dar_correction = output.dar_aspect_correction;
            break;
        }
    }
    
    // Update aspect correction if in DAR mode
    if (preview_renderer_->get_aspect_ratio_mode() == orc::AspectRatioMode::DAR_4_3) {
        preview_dialog_->previewWidget()->setAspectCorrection(dar_correction);
        ORC_LOG_DEBUG("Updated DAR correction to {} for current output", dar_correction);
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
    
    // Update all preview-related components (image, info, VBI dialog)
    updateAllPreviewComponents();
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
            
            // Populate aspect ratio combo from core
            // Note: aspect correction will be set when first node is selected
            updateAspectRatioCombo();
        }
        
        // Create or update VBI decoder
        if (vbi_decoder_) {
            vbi_decoder_->update_dag(dag);
        } else {
            vbi_decoder_ = std::make_unique<orc::VBIDecoder>(dag);
        }
        
        if (dropout_decoder_) {
            dropout_decoder_->update_dag(dag);
        } else {
            dropout_decoder_ = std::make_unique<orc::DropoutAnalysisDecoder>(dag);
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

    // Special-case: Vectorscope is a live visualization dialog, not a batch analysis
    if (tool->id() == "vectorscope") {
        // Look up the stage for this node and ensure it's a chroma sink
        auto dag = project_.getDAG();
        if (!dag) {
            ORC_LOG_WARN("No DAG available to open vectorscope for node '{}'", node_id);
            return;
        }
        const auto& dag_nodes = dag->nodes();
        auto it = std::find_if(dag_nodes.begin(), dag_nodes.end(), [&node_id](const auto& n){ return n.node_id == node_id; });
        if (it == dag_nodes.end()) {
            ORC_LOG_WARN("Node '{}' not found to open vectorscope", node_id);
            return;
        }
        auto* chroma = dynamic_cast<orc::ChromaSinkStage*>(it->stage.get());
        if (!chroma) {
            ORC_LOG_WARN("Vectorscope requested for non-chroma stage '{}'", stage_name);
            return;
        }

        // Reuse existing dialog for this node if present; else create new
        VectorscopeDialog* dialog = nullptr;
        auto dit = vectorscope_dialogs_.find(node_id);
        if (dit != vectorscope_dialogs_.end()) {
            dialog = dit->second;
        } else {
            dialog = new VectorscopeDialog(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose, true);
            dialog->setStage(node_id, chroma);
            // Remove from map when closed/destroyed
            connect(dialog, &VectorscopeDialog::closed, [this, node_id]() {
                auto it2 = vectorscope_dialogs_.find(node_id);
                if (it2 != vectorscope_dialogs_.end()) {
                    vectorscope_dialogs_.erase(it2);
                }
            });
            connect(dialog, &QObject::destroyed, [this, node_id]() {
                auto it2 = vectorscope_dialogs_.find(node_id);
                if (it2 != vectorscope_dialogs_.end()) {
                    vectorscope_dialogs_.erase(it2);
                }
            });
            vectorscope_dialogs_[node_id] = dialog;
        }

        // Ensure correct stage context set (node might have changed)
        dialog->setStage(node_id, chroma);
        dialog->show();
        dialog->raise();
        dialog->activateWindow();

        // If we're currently previewing this node, trigger an immediate update
        if (current_view_node_id_ == node_id) {
            updatePreview();
        }
        return;
    }
    
    // Create analysis context
    orc::AnalysisContext context;
    context.node_id = node_id;
    context.source_type = orc::AnalysisSourceType::LaserDisc;  // TODO: Detect from project
    context.project = std::make_shared<orc::Project>(project_.coreProject());
    
    // Create DAG from project for analysis
    context.dag = orc::project_to_dag(project_.coreProject());
    
    // Default path: show analysis dialog which handles batch analysis and apply
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

void MainWindow::onShowVBIDialog()
{
    if (!vbi_dialog_) {
        return;
    }
    
    // Show the dialog first
    vbi_dialog_->show();
    vbi_dialog_->raise();
    vbi_dialog_->activateWindow();
    
    // Update VBI information after showing
    updateVBIDialog();
}

// Removed Vectorscope dialog launcher from Preview; vectorscope opens from node context menu

void MainWindow::updateAllPreviewComponents()
{
    updatePreview();
    updatePreviewInfo();
    updateVBIDialog();
    updateDropoutAnalysisDialog();
}

void MainWindow::updateVBIDialog()
{
    // Only update if VBI dialog is visible
    if (!vbi_dialog_ || !vbi_dialog_->isVisible() || !vbi_decoder_) {
        return;
    }
    
    // Get current field being displayed
    if (current_view_node_id_.empty() || !preview_renderer_) {
        vbi_dialog_->clearVBIInfo();
        return;
    }
    
    // Get the current index from the preview slider
    int current_index = preview_dialog_->previewSlider()->value();
    
    // Check if we're in frame mode (any mode that shows two fields)
    bool is_frame_mode = (current_output_type_ == orc::PreviewOutputType::Frame ||
                         current_output_type_ == orc::PreviewOutputType::Frame_Reversed ||
                         current_output_type_ == orc::PreviewOutputType::Split);
    
    if (is_frame_mode) {
        // Frame mode - slider index represents frames, convert to field IDs
        // Frame 0 = fields 0,1; Frame 1 = fields 2,3; etc.
        orc::FieldID field1_id(current_index * 2);
        orc::FieldID field2_id(current_index * 2 + 1);
        
        auto vbi_info1 = vbi_decoder_->get_vbi_for_field(current_view_node_id_, field1_id);
        auto vbi_info2 = vbi_decoder_->get_vbi_for_field(current_view_node_id_, field2_id);
        
        if (vbi_info1.has_value() && vbi_info2.has_value()) {
            vbi_dialog_->updateVBIInfoFrame(vbi_info1.value(), vbi_info2.value());
        } else if (vbi_info1.has_value()) {
            vbi_dialog_->updateVBIInfo(vbi_info1.value());
        } else if (vbi_info2.has_value()) {
            vbi_dialog_->updateVBIInfo(vbi_info2.value());
        } else {
            vbi_dialog_->clearVBIInfo();
        }
    } else {
        // Field mode - get VBI from single field
        orc::FieldID field_id(current_index);
        
        auto vbi_info = vbi_decoder_->get_vbi_for_field(current_view_node_id_, field_id);
        
        if (vbi_info.has_value()) {
            vbi_dialog_->updateVBIInfo(vbi_info.value());
        } else {
            vbi_dialog_->clearVBIInfo();
        }
    }
}
void MainWindow::onShowDropoutAnalysisDialog()
{
    if (!dropout_analysis_dialog_) {
        return;
    }
    
    // Show the dialog first
    dropout_analysis_dialog_->show();
    dropout_analysis_dialog_->raise();
    dropout_analysis_dialog_->activateWindow();
    
    // Update dropout analysis after showing
    updateDropoutAnalysisDialog();
}

void MainWindow::updateDropoutAnalysisDialog()
{
    // Only update if dialog is visible
    if (!dropout_analysis_dialog_ || !dropout_analysis_dialog_->isVisible()) {
        return;
    }
    
    // Get current node being displayed
    if (current_view_node_id_.empty() || !preview_renderer_ || !dropout_decoder_) {
        return;
    }
    
    try {
        // Get current mode from the dialog
        auto mode = dropout_analysis_dialog_->getCurrentMode();
        
        // Get current index to show marker
        int current_index = preview_dialog_->previewSlider()->value();
        
        // Check if we need to reload data (node, mode, or output type changed)
        bool need_reload = (current_view_node_id_ != last_dropout_node_id_ ||
                           mode != last_dropout_mode_ ||
                           current_output_type_ != last_dropout_output_type_);
        
        if (need_reload) {
            ORC_LOG_INFO("Updating dropout analysis for node '{}' in {} mode",
                        current_view_node_id_,
                        mode == orc::DropoutAnalysisMode::FULL_FIELD ? "FULL_FIELD" : "VISIBLE_AREA");
            
            // Check if we're in frame mode
            bool is_frame_mode = (current_output_type_ == orc::PreviewOutputType::Frame ||
                                 current_output_type_ == orc::PreviewOutputType::Frame_Reversed ||
                                 current_output_type_ == orc::PreviewOutputType::Split);
            
            if (is_frame_mode) {
                // Get frame-based dropout stats
                auto frame_stats = dropout_decoder_->get_dropout_by_frames(current_view_node_id_, mode);
                
                if (!frame_stats.empty()) {
                    dropout_analysis_dialog_->startUpdate(frame_stats.size());
                    
                    for (const auto& stats : frame_stats) {
                        if (stats.has_data) {
                            dropout_analysis_dialog_->addDataPoint(
                                stats.frame_number,
                                stats.total_dropout_length);
                        }
                    }
                    
                    // Pass 1-based frame number for marker positioning
                    int32_t current_frame = current_index + 1;
                    dropout_analysis_dialog_->finishUpdate(current_frame);
                    
                    ORC_LOG_DEBUG("Dropout analysis: Loaded {} frames, marker at frame {} (slider index {})", 
                                 frame_stats.size(), current_frame, current_index);
                } else {
                    ORC_LOG_WARN("No dropout data available for node '{}'", current_view_node_id_);
                    dropout_analysis_dialog_->showNoDataMessage("Node produces no video output");
                }
            } else {
                // Field mode - get field-based stats but display as if they were frames
                auto field_stats = dropout_decoder_->get_dropout_for_all_fields(current_view_node_id_, mode);
                
                if (!field_stats.empty()) {
                    dropout_analysis_dialog_->startUpdate(field_stats.size());
                    
                    for (const auto& stats : field_stats) {
                        if (stats.has_data) {
                            // Use field index + 1 as the "frame number" for display
                            dropout_analysis_dialog_->addDataPoint(
                                stats.field_id.value() + 1,
                                stats.total_dropout_length);
                        }
                    }
                    
                    // Pass 1-based field number for marker positioning
                    int32_t current_field = current_index + 1;
                    dropout_analysis_dialog_->finishUpdate(current_field);
                    
                    ORC_LOG_DEBUG("Dropout analysis: Loaded {} fields, marker at field {} (slider index {})", 
                                 field_stats.size(), current_field, current_index);
                } else {
                    ORC_LOG_WARN("No dropout data available for node '{}'", current_view_node_id_);
                    dropout_analysis_dialog_->showNoDataMessage("Node produces no video output");
                }
            }
            
            // Update tracking state
            last_dropout_node_id_ = current_view_node_id_;
            last_dropout_mode_ = mode;
            last_dropout_output_type_ = current_output_type_;
            
        } else {
            // Just update the marker position without reloading data
            int32_t marker_position = current_index + 1;  // 1-based
            dropout_analysis_dialog_->updateFrameMarker(marker_position);
            ORC_LOG_DEBUG("Dropout marker updated to position {} (slider index {})", marker_position, current_index);
        }
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Error updating dropout analysis dialog: {}", e.what());
    }
}
