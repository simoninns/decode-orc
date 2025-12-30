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
#include "snranalysisdialog.h"
#include "burstlevelanalysisdialog.h"
#include "projectpropertiesdialog.h"
#include "stageparameterdialog.h"
#include "inspection_dialog.h"
#include "analysis/analysis_dialog.h"
#include "analysis/vectorscope_dialog.h"
#include "orcgraphicsview.h"
#include "render_coordinator.h"
#include "logging.h"
#include "../core/include/preview_renderer.h"
#include "../core/include/vbi_decoder.h"
#include "../core/analysis/dropout/dropout_analysis_decoder.h"
#include "../core/analysis/snr/snr_analysis_decoder.h"
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
    , render_coordinator_(nullptr)
    , current_view_node_id_()
    , last_dropout_node_id_()
    , last_dropout_mode_(orc::DropoutAnalysisMode::FULL_FIELD)
    , last_dropout_output_type_(orc::PreviewOutputType::Frame)
    , last_snr_node_id_()
    , last_snr_mode_(orc::SNRAnalysisMode::WHITE_SNR)
    , last_snr_output_type_(orc::PreviewOutputType::Frame)
    , current_output_type_(orc::PreviewOutputType::Frame)
    , current_option_id_("frame")  // Default to "Frame (Y)" option
    , preview_update_timer_(nullptr)
    , pending_preview_index_(-1)
    , preview_update_pending_(false)
    , last_preview_update_time_(0)
    , last_update_was_sequential_(false)
    , trigger_progress_dialog_(nullptr)
    , dropout_progress_dialog_(nullptr)
    , snr_progress_dialog_(nullptr)
{
    // Create and start render coordinator
    render_coordinator_ = std::make_unique<RenderCoordinator>(this);
    
    // Connect coordinator signals
    connect(render_coordinator_.get(), &RenderCoordinator::previewReady,
            this, &MainWindow::onPreviewReady);
    connect(render_coordinator_.get(), &RenderCoordinator::vbiDataReady,
            this, &MainWindow::onVBIDataReady);
    connect(render_coordinator_.get(), &RenderCoordinator::availableOutputsReady,
            this, &MainWindow::onAvailableOutputsReady);
    connect(render_coordinator_.get(), &RenderCoordinator::dropoutDataReady,
            this, &MainWindow::onDropoutDataReady);
    connect(render_coordinator_.get(), &RenderCoordinator::dropoutProgress,
            this, &MainWindow::onDropoutProgress);
    connect(render_coordinator_.get(), &RenderCoordinator::snrDataReady,
            this, &MainWindow::onSNRDataReady);
    connect(render_coordinator_.get(), &RenderCoordinator::snrProgress,
            this, &MainWindow::onSNRProgress);
    connect(render_coordinator_.get(), &RenderCoordinator::burstLevelDataReady,
            this, &MainWindow::onBurstLevelDataReady);
    connect(render_coordinator_.get(), &RenderCoordinator::burstLevelProgress,
            this, &MainWindow::onBurstLevelProgress);
    connect(render_coordinator_.get(), &RenderCoordinator::triggerProgress,
            this, &MainWindow::onTriggerProgress);
    connect(render_coordinator_.get(), &RenderCoordinator::triggerComplete,
            this, &MainWindow::onTriggerComplete);
    connect(render_coordinator_.get(), &RenderCoordinator::error,
            this, &MainWindow::onCoordinatorError);
    
    // Start the coordinator worker thread
    render_coordinator_->start();
    
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
    dropout_analysis_dialog_->setWindowTitle("Dropout Analysis");
    dropout_analysis_dialog_->setAttribute(Qt::WA_DeleteOnClose, false);
    // Note: modeChanged will trigger re-request of data when user changes mode
    connect(dropout_analysis_dialog_, &DropoutAnalysisDialog::modeChanged,
            [this]() {
                // Re-request data with new mode when dialog is visible
                if (dropout_analysis_dialog_->isVisible() && current_view_node_id_.is_valid()) {
                    // Show progress dialog
                    if (dropout_progress_dialog_) {
                        delete dropout_progress_dialog_;
                    }
                    dropout_progress_dialog_ = new QProgressDialog("Loading dropout analysis data...", QString(), 0, 100, this);
                    dropout_progress_dialog_->setWindowTitle("Dropout Analysis");
                    dropout_progress_dialog_->setWindowModality(Qt::WindowModal);
                    dropout_progress_dialog_->setMinimumDuration(0);
                    dropout_progress_dialog_->setCancelButton(nullptr);
                    dropout_progress_dialog_->setValue(0);
                    dropout_progress_dialog_->show();
                    
                    auto mode = dropout_analysis_dialog_->getCurrentMode();
                    pending_dropout_request_id_ = render_coordinator_->requestDropoutData(current_view_node_id_, mode);
                }
            });
    
    // Create SNR analysis dialog (initially hidden)
    snr_analysis_dialog_ = new SNRAnalysisDialog(this);
    snr_analysis_dialog_->setWindowTitle("SNR Analysis");
    snr_analysis_dialog_->setAttribute(Qt::WA_DeleteOnClose, false);
    // Note: modeChanged will trigger re-request of data when user changes mode
    connect(snr_analysis_dialog_, &SNRAnalysisDialog::modeChanged,
            [this]() {
                // Re-request data with new mode when dialog is visible
                if (snr_analysis_dialog_->isVisible() && current_view_node_id_.is_valid()) {
                    // Show progress dialog
                    if (snr_progress_dialog_) {
                        delete snr_progress_dialog_;
                    }
                    snr_progress_dialog_ = new QProgressDialog("Loading SNR analysis data...", QString(), 0, 100, this);
                    snr_progress_dialog_->setWindowTitle("SNR Analysis");
                    snr_progress_dialog_->setWindowModality(Qt::WindowModal);
                    snr_progress_dialog_->setMinimumDuration(0);
                    snr_progress_dialog_->setCancelButton(nullptr);
                    snr_progress_dialog_->setValue(0);
                    snr_progress_dialog_->show();
                    
                    auto mode = snr_analysis_dialog_->getCurrentMode();
                    pending_snr_request_id_ = render_coordinator_->requestSNRData(current_view_node_id_, mode);
                }
            });
    
    // Create burst level analysis dialog (initially hidden)
    burst_level_analysis_dialog_ = new BurstLevelAnalysisDialog(this);
    burst_level_analysis_dialog_->setWindowTitle("Burst Level Analysis");
    burst_level_analysis_dialog_->setAttribute(Qt::WA_DeleteOnClose, false);
    
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
    bool has_preview = current_view_node_id_.is_valid();
    
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
    
    // Simple conversion for index (just use same position)
    uint64_t new_position = current_position;
    
    // Use helper function to update all viewer controls (slider range, step, preview, info)
    refreshViewerControls();
    
    // Set the calculated position (after refreshViewerControls updates the range)
    if (new_position >= 0 && new_position <= static_cast<uint64_t>(preview_dialog_->previewSlider()->maximum())) {
        preview_dialog_->previewSlider()->setValue(new_position);
    }
}

void MainWindow::onAspectRatioModeChanged(int index)
{
    // Use hardcoded aspect ratio modes for simplicity
    // Note: Could request from coordinator if dynamic modes are needed in the future,
    // but these two modes (SAR 1:1 and DAR 4:3) cover all current use cases
    static std::vector<orc::AspectRatioModeInfo> available_modes = {
        {orc::AspectRatioMode::SAR_1_1, "1:1 (Square)", 1.0},
        {orc::AspectRatioMode::DAR_4_3, "4:3 (Display)", 0.7}
    };
    if (index < 0 || index >= static_cast<int>(available_modes.size())) {
        return;
    }
    
    // Note: Aspect ratio is applied client-side, no need to update coordinator
    
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
    if (current_view_node_id_.is_valid() == false) {
        preview_dialog_->previewInfoLabel()->setText("No node selected");
        preview_dialog_->sliderMinLabel()->setText("");
        preview_dialog_->sliderMaxLabel()->setText("");
        return;
    }
    
    // Special handling for placeholder node  
    if (current_view_node_id_ == NodeID(-999)) {
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
    
    // Build display info client-side
    orc::PreviewItemDisplayInfo display_info;
    // Get display name from available outputs
    display_info.type_name = "Item";  // Default fallback
    for (const auto& output : available_outputs_) {
        if (output.type == current_output_type_) {
            display_info.type_name = output.display_name;
            break;
        }
    }
    display_info.current_number = current_index + 1;
    display_info.total_count = total;
    display_info.has_field_info = false;
    
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

void MainWindow::onEditParameters(const orc::NodeID& node_id)
{
    ORC_LOG_DEBUG("Edit parameters requested for node: {}", node_id);
    
    // Find the node in the project
    const auto& nodes = project_.coreProject().get_nodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
        [&node_id](const orc::ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == nodes.end()) {
        QMessageBox::warning(this, "Edit Parameters",
            QString("Node '%1' not found").arg(QString::fromStdString(node_id.to_string())));
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
    
    // Create a new temporary instance for getting parameter descriptors
    // DO NOT use DAG stage - that would cause data races with worker thread!
    // The worker thread has exclusive ownership of the DAG.
    auto stage = registry.create_stage(stage_name);
    auto param_stage = dynamic_cast<orc::ParameterizedStage*>(stage.get());
    ORC_LOG_DEBUG("Using new stage instance for parameter descriptors (thread-safe)");
    
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
                .arg(QString::fromStdString(node_id.to_string())),
            3000
        );
    }
}

void MainWindow::onTriggerStage(const orc::NodeID& node_id)
{
    ORC_LOG_DEBUG("Trigger stage requested for node: {}", node_id.to_string());
    
    try {
        // Create progress dialog
        trigger_progress_dialog_ = new QProgressDialog("Starting trigger...", "Cancel", 0, 100, this);
        trigger_progress_dialog_->setWindowTitle("Processing");
        trigger_progress_dialog_->setWindowModality(Qt::WindowModal);
        trigger_progress_dialog_->setMinimumDuration(0);
        trigger_progress_dialog_->setValue(0);
        
        // Connect cancel button
        connect(trigger_progress_dialog_, &QProgressDialog::canceled,
                this, [this]() {
            render_coordinator_->cancelTrigger();
        });
        
        // Request trigger from coordinator (async, thread-safe)
        pending_trigger_request_id_ = render_coordinator_->requestTrigger(node_id);
        
    } catch (const std::exception& e) {
        QString msg = QString("Error starting trigger: %1").arg(e.what());
        ORC_LOG_ERROR("{}", msg.toStdString());
        QMessageBox::critical(this, "Trigger Error", msg);
        
        if (trigger_progress_dialog_) {
            delete trigger_progress_dialog_;
            trigger_progress_dialog_ = nullptr;
        }
    }
}

void MainWindow::onNodeSelectedForView(const orc::NodeID& node_id)
{
    ORC_LOG_DEBUG("Main window: switching view to node '{}'", node_id.to_string());
    
    // Update which node is being viewed
    current_view_node_id_ = node_id;
    
    // Request available outputs from coordinator
    pending_outputs_request_id_ = render_coordinator_->requestAvailableOutputs(node_id);
    
    // Note: Analysis dialogs (dropout/SNR) are triggered from stage context menu,
    // not automatically updated when switching nodes
    
    // The rest will happen in onAvailableOutputsReady callback
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
    std::map<orc::NodeID, std::vector<orc::NodeID>> forward_edges;
    std::map<orc::NodeID, int> in_degree;
    
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
    std::map<orc::NodeID, int> node_depth;
    std::queue<orc::NodeID> queue;
    
    // Start with source nodes (in-degree 0)
    for (const auto& [node_id, degree] : in_degree) {
        if (degree == 0) {
            node_depth[node_id] = 0;
            queue.push(node_id);
        }
    }
    
    // BFS to assign depths
    while (!queue.empty()) {
        orc::NodeID current_id = queue.front();
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
    std::map<int, std::vector<orc::NodeID>> levels;
    for (const auto& [node_id, depth] : node_depth) {
        levels[depth].push_back(node_id);
    }
    
    // Build reverse edges (target -> sources) for ordering
    std::map<orc::NodeID, std::vector<orc::NodeID>> reverse_edges;
    for (const auto& edge : edges) {
        reverse_edges[edge.target_node_id].push_back(edge.source_node_id);
    }
    
    // Order nodes within each level to minimize edge crossings
    // We'll use a simple heuristic: order by median position of connected nodes in previous/next layer
    std::map<orc::NodeID, double> node_y_position;
    
    for (const auto& [depth, level_nodes] : levels) {
        std::vector<std::pair<double, orc::NodeID>> nodes_with_order;
        
        for (const auto& node_id : level_nodes) {
            double order_key = 0.0;
            int count = 0;
            
            // Consider inputs from previous layer
            if (reverse_edges.count(node_id) > 0) {
                for (const auto& input_id : reverse_edges[node_id]) {
                    if (node_y_position.count(input_id) > 0) {
                        order_key += node_y_position[input_id];
                        count++;
                    }
                }
            }
            
            // Consider outputs to next layer (also helps with ordering)
            if (forward_edges.count(node_id) > 0) {
                for (const auto& output_id : forward_edges[node_id]) {
                    if (node_y_position.count(output_id) > 0) {
                        order_key += node_y_position[output_id];
                        count++;
                    }
                }
            }
            
            // Use median position of connected nodes as ordering key
            if (count > 0) {
                order_key /= count;
            } else {
                // No connections yet, use arbitrary order
                order_key = static_cast<double>(nodes_with_order.size());
            }
            
            nodes_with_order.push_back({order_key, node_id});
        }
        
        // Sort nodes by their order key
        std::sort(nodes_with_order.begin(), nodes_with_order.end());
        
        // Position nodes: each depth level gets a column
        double x = depth * grid_spacing_x;
        
        for (size_t i = 0; i < nodes_with_order.size(); ++i) {
            const auto& node_id = nodes_with_order[i].second;
            double y = i * grid_spacing_y;
            node_y_position[node_id] = y;  // Remember position for next layer
            orc::project_io::set_node_position(project_.coreProject(), node_id, x, y);
        }
    }
    
    // Refresh the view
    dag_model_->refresh();
    statusBar()->showMessage("Arranged DAG to grid", 2000);
}

void MainWindow::updatePreview()
{
    // If no node selected, clear display
    if (current_view_node_id_.is_valid() == false) {
        ORC_LOG_DEBUG("updatePreview: no node selected, returning");
        preview_dialog_->previewWidget()->clearImage();
        return;
    }
    
    int current_index = preview_dialog_->previewSlider()->value();
    
    ORC_LOG_DEBUG("updatePreview: rendering output type {} index {} at node '{}'", 
                  static_cast<int>(current_output_type_), current_index, current_view_node_id_.to_string());
    
    // Request preview from coordinator (async, thread-safe)
    pending_preview_request_id_ = render_coordinator_->requestPreview(
        current_view_node_id_,
        current_output_type_,
        current_index,
        current_option_id_
    );
    
    // Reset the sequential flag after use
    last_update_was_sequential_ = false;
}

void MainWindow::updateVectorscope(const orc::NodeID& node_id, const orc::PreviewImage& image)
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
    // Populate aspect ratio combo (client-side, no coordinator needed)
    static std::vector<orc::AspectRatioModeInfo> available_modes = {
        {orc::AspectRatioMode::SAR_1_1, "1:1 (Square)", 1.0},
        {orc::AspectRatioMode::DAR_4_3, "4:3 (Display)", 0.7}
    };
    
    orc::AspectRatioMode current_mode = orc::AspectRatioMode::DAR_4_3;  // Default to 4:3
    
    // Block signals while updating combo box
    preview_dialog_->aspectRatioCombo()->blockSignals(true);
    
    // Clear existing items
    preview_dialog_->aspectRatioCombo()->clear();
    
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
    
    if (current_view_node_id_.is_valid() == false || available_outputs_.empty()) {
        ORC_LOG_DEBUG("refreshViewerControls: no node or outputs");
        return;
    }
    
    // Update the preview mode combo box
    updatePreviewModeCombo();
    
    // Update the aspect ratio combo box
    updateAspectRatioCombo();
    
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
    
    // Update aspect correction (always apply for now)
    // Note: Aspect mode tracking could be added as a member variable (current_aspect_mode_)
    // and synchronized with the combo box selection for more explicit state management
    preview_dialog_->previewWidget()->setAspectCorrection(dar_correction);
    ORC_LOG_DEBUG("Updated aspect correction to {} for current output", dar_correction);
    
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
    
    // Send DAG update to coordinator (thread-safe)
    render_coordinator_->updateDAG(dag);
    
    // Check if current node is still valid, or if we need to switch
    bool need_to_switch = false;
    std::string target_node;
    
    if (current_view_node_id_.is_valid() == false) {
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
        if (!current_exists && current_view_node_id_ != NodeID(-999)) {
            need_to_switch = true;
        } else if (current_view_node_id_ == NodeID(-999) && dag && !dag->nodes().empty()) {
            need_to_switch = true;
        }
    }
    
    // Node selection logic: The coordinator now handles renderer updates internally.
    // When we need to switch nodes (e.g., current was deleted), we'll request
    // outputs for the new node, and onAvailableOutputsReady will handle the rest.
    if (need_to_switch) {
        // Note: Could implement coordinator->requestSuggestedViewNode() to get a smart
        // suggestion (e.g., prefer sink nodes, or nodes with most connections).
        // Current approach: Simple fallback to first node is adequate for typical workflows.
        ORC_LOG_DEBUG("Node switching needed - using first node fallback");
        if (current_view_node_id_.is_valid() == false && dag && !dag->nodes().empty()) {
            // Pick first node as temporary fallback
            current_view_node_id_ = dag->nodes()[0].node_id;
            pending_outputs_request_id_ = render_coordinator_->requestAvailableOutputs(current_view_node_id_);
        }
    } else {
        // Keep current node - request fresh outputs in case parameters changed
        if (current_view_node_id_.is_valid()) {
            ORC_LOG_DEBUG("Keeping current node '{}', refreshing outputs", current_view_node_id_.to_string());
            pending_outputs_request_id_ = render_coordinator_->requestAvailableOutputs(current_view_node_id_);
        }
    }
}

void MainWindow::onExportPNG()
{
    if (current_view_node_id_.is_valid() == false) {
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
    
    // Request PNG save via coordinator
    ORC_LOG_INFO("Requesting PNG export to: {}", filename.toStdString());
    
    render_coordinator_->requestSavePNG(
        current_view_node_id_,
        current_output_type_,
        current_index,
        filename.toStdString(),
        current_option_id_
    );
    
    statusBar()->showMessage(QString("Exporting preview to %1...").arg(filename), 2000);
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
    NodeID orc_node_id = dag_model_->getOrcNodeId(nodeId);
    if (orc_node_id.is_valid()) {
        ORC_LOG_DEBUG("QtNode {} selected -> ORC node '{}'", nodeId, orc_node_id);
        onNodeSelectedForView(orc_node_id);
    }
}

void MainWindow::onInspectStage(const NodeID& node_id)
{
    ORC_LOG_DEBUG("Inspect stage requested for node: {}", node_id);
    
    // Find the node in the project
    const auto& nodes = project_.coreProject().get_nodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
        [&node_id](const orc::ProjectDAGNode& n) { return n.node_id == node_id; });
    
    if (node_it == nodes.end()) {
        ORC_LOG_ERROR("Node '{}' not found in project", node_id);
        QMessageBox::warning(this, "Inspection Failed",
            QString("Node '%1' not found.").arg(QString::fromStdString(node_id.to_string())));
        return;
    }
    
    const std::string& stage_name = node_it->stage_name;
    
    // Try to get the stage from the DAG if available (preserves execution state)
    orc::DAGStagePtr stage;
    auto dag = project_.getDAG();
    if (dag) {
        const auto& dag_nodes = dag->nodes();
        auto dag_node_it = std::find_if(dag_nodes.begin(), dag_nodes.end(),
            [&node_id](const orc::DAGNode& n) { return n.node_id == node_id; });
        if (dag_node_it != dag_nodes.end()) {
            stage = dag_node_it->stage;
            ORC_LOG_DEBUG("Using stage from DAG (preserves execution state)");
        }
    }
    
    // If not in DAG, create a fresh instance
    if (!stage) {
        ORC_LOG_DEBUG("Creating fresh stage instance (no execution state)");
        try {
            auto& stage_registry = orc::StageRegistry::instance();
            if (!stage_registry.has_stage(stage_name)) {
                ORC_LOG_ERROR("Stage type '{}' not found in registry", stage_name);
                QMessageBox::warning(this, "Inspection Failed",
                    QString("Stage type '%1' not found in registry.").arg(QString::fromStdString(stage_name)));
                return;
            }
            
            stage = stage_registry.create_stage(stage_name);
            
            // Apply the node's parameters to the stage
            auto* param_stage = dynamic_cast<orc::ParameterizedStage*>(stage.get());
            if (param_stage) {
                param_stage->set_parameters(node_it->parameters);
            }
        } catch (const std::exception& e) {
            ORC_LOG_ERROR("Failed to create stage '{}': {}", stage_name, e.what());
            QMessageBox::warning(this, "Inspection Failed",
                QString("Failed to create stage: %1").arg(e.what()));
            return;
        }
    }
    
    // Generate report from the stage
    try {
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

void MainWindow::runAnalysisForNode(orc::AnalysisTool* tool, const orc::NodeID& node_id, const std::string& stage_name)
{
    ORC_LOG_DEBUG("Running analysis '{}' for node '{}'", tool->name(), node_id.to_string());

    // Special-case: Vectorscope is a live visualization dialog, not a batch analysis
    if (tool->id() == "vectorscope") {
        // Note: Vectorscope data comes from preview images (thread-safe),
        // so we don't need to access the DAG or stage here.
        
        // Reuse existing dialog for this node if present; else create new
        VectorscopeDialog* dialog = nullptr;
        auto dit = vectorscope_dialogs_.find(node_id);
        if (dit != vectorscope_dialogs_.end()) {
            dialog = dit->second;
        } else {
            dialog = new VectorscopeDialog(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose, true);
            dialog->setStage(node_id);
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

        // Show the dialog
        dialog->show();
        dialog->raise();
        dialog->activateWindow();

        // If we're currently previewing this node, trigger an immediate update
        if (current_view_node_id_ == node_id) {
            updatePreview();
        }
        return;
    }
    
    // Special-case: Dropout Analysis triggers batch processing and shows dialog
    if (tool->id() == "dropout_analysis") {
        if (!dropout_analysis_dialog_) {
            ORC_LOG_ERROR("Dropout analysis dialog not initialized");
            return;
        }
        
        // Set the node for analysis
        current_view_node_id_ = node_id;
        
        // Create and show progress dialog
        createAnalysisProgressDialog("Dropout Analysis", "Loading dropout analysis data...", dropout_progress_dialog_);
        
        // Show the dialog (but it will be empty until data arrives)
        dropout_analysis_dialog_->show();
        dropout_analysis_dialog_->raise();
        dropout_analysis_dialog_->activateWindow();
        
        // Request dropout data from coordinator (triggers batch processing)
        auto mode = dropout_analysis_dialog_->getCurrentMode();
        pending_dropout_request_id_ = render_coordinator_->requestDropoutData(node_id, mode);
        
        ORC_LOG_DEBUG("Requested dropout analysis data for node '{}', mode {}, request_id={}",
                      node_id.to_string(), static_cast<int>(mode), pending_dropout_request_id_);
        return;
    }
    
    // Special-case: SNR Analysis triggers batch processing and shows dialog
    if (tool->id() == "snr_analysis") {
        if (!snr_analysis_dialog_) {
            ORC_LOG_ERROR("SNR analysis dialog not initialized");
            return;
        }
        
        // Set the node for analysis
        current_view_node_id_ = node_id;
        
        // Create and show progress dialog
        createAnalysisProgressDialog("SNR Analysis", "Loading SNR analysis data...", snr_progress_dialog_);
        
        // Show the dialog (but it will be empty until data arrives)
        snr_analysis_dialog_->show();
        snr_analysis_dialog_->raise();
        snr_analysis_dialog_->activateWindow();
        
        // Request SNR data from coordinator (triggers batch processing)
        auto mode = snr_analysis_dialog_->getCurrentMode();
        pending_snr_request_id_ = render_coordinator_->requestSNRData(node_id, mode);
        
        ORC_LOG_DEBUG("Requested SNR analysis data for node '{}', mode {}, request_id={}",
                      node_id.to_string(), static_cast<int>(mode), pending_snr_request_id_);
        return;
    }
    
    // Special-case: Burst Level Analysis triggers batch processing and shows dialog
    if (tool->id() == "burst_level_analysis") {
        if (!burst_level_analysis_dialog_) {
            ORC_LOG_ERROR("Burst level analysis dialog not initialized");
            return;
        }
        
        // Set the node for analysis
        current_view_node_id_ = node_id;
        
        // Create and show progress dialog
        createAnalysisProgressDialog("Burst Level Analysis", "Loading burst level analysis data...", burst_level_progress_dialog_);
        
        // Show the dialog (but it will be empty until data arrives)
        burst_level_analysis_dialog_->show();
        burst_level_analysis_dialog_->raise();
        burst_level_analysis_dialog_->activateWindow();
        
        // Request burst level data from coordinator (triggers batch processing)
        pending_burst_level_request_id_ = render_coordinator_->requestBurstLevelData(node_id);
        
        ORC_LOG_DEBUG("Requested burst level analysis data for node '{}', request_id={}",
                      node_id.to_string(), pending_burst_level_request_id_);
        return;
    }
    
    // Create analysis context
    orc::AnalysisContext context;
    context.node_id = node_id;
    // Detect source type from project video format
    // Default to LaserDisc for NTSC/PAL, as most TBC files come from LaserDisc sources
    context.source_type = orc::AnalysisSourceType::LaserDisc;
    if (project_.coreProject().get_video_format() == orc::VideoSystem::Unknown) {
        context.source_type = orc::AnalysisSourceType::Other;
    }
    context.project = std::make_shared<orc::Project>(project_.coreProject());
    
    // Create DAG from project for analysis
    context.dag = orc::project_to_dag(project_.coreProject());
    
    // Default path: show analysis dialog which handles batch analysis and apply
    orc::gui::AnalysisDialog dialog(tool, context, this);
    
    // Connect apply signal to handle applying results to the node
    connect(&dialog, &orc::gui::AnalysisDialog::applyToGraph, 
            [this, tool, node_id](const orc::AnalysisResult& result) {
        ORC_LOG_INFO("Applying analysis results to node '{}'", node_id.to_string());
        
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
                        .arg(QString::fromStdString(node_id.to_string())),
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

QProgressDialog* MainWindow::createAnalysisProgressDialog(const QString& title, const QString& message, QPointer<QProgressDialog>& existingDialog)
{
    if (existingDialog) {
        delete existingDialog;
    }
    
    auto* dialog = new QProgressDialog(message, QString(), 0, 100, this);
    dialog->setWindowTitle(title);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setMinimumDuration(0);
    dialog->setCancelButton(nullptr);  // No cancel for now
    dialog->setValue(0);
    dialog->show();
    
    existingDialog = dialog;
    return dialog;
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
    
    // For analysis dialogs, only update the frame marker position (not the full data)
    // Full data is only loaded when the dialog is first opened
    if (dropout_analysis_dialog_ && dropout_analysis_dialog_->isVisible()) {
        int32_t current_frame = 1;
        if (preview_dialog_ && preview_dialog_->previewSlider()) {
            current_frame = static_cast<int32_t>(preview_dialog_->previewSlider()->value()) + 1;
        }
        dropout_analysis_dialog_->updateFrameMarker(current_frame);
    }
    
    if (snr_analysis_dialog_ && snr_analysis_dialog_->isVisible()) {
        int32_t current_frame = 1;
        if (preview_dialog_ && preview_dialog_->previewSlider()) {
            current_frame = static_cast<int32_t>(preview_dialog_->previewSlider()->value()) + 1;
        }
        snr_analysis_dialog_->updateFrameMarker(current_frame);
    }
    
    if (burst_level_analysis_dialog_ && burst_level_analysis_dialog_->isVisible()) {
        int32_t current_frame = 1;
        if (preview_dialog_ && preview_dialog_->previewSlider()) {
            current_frame = static_cast<int32_t>(preview_dialog_->previewSlider()->value()) + 1;
        }
        burst_level_analysis_dialog_->updateFrameMarker(current_frame);
    }
}

void MainWindow::updateVBIDialog()
{
    // Only update if VBI dialog is visible
    if (!vbi_dialog_ || !vbi_dialog_->isVisible()) {
        return;
    }
    
    // Get current field being displayed
    if (!current_view_node_id_.is_valid()) {
        vbi_dialog_->clearVBIInfo();
        return;
    }
    
    // Get the current index from the preview slider
    int current_index = preview_dialog_->previewSlider()->value();
    
    // Check if we're in frame mode (any mode that shows two fields)
    bool is_frame_mode = (current_output_type_ == orc::PreviewOutputType::Frame ||
                         current_output_type_ == orc::PreviewOutputType::Frame_Reversed ||
                         current_output_type_ == orc::PreviewOutputType::Split);
    
    // Request VBI data from coordinator
    if (is_frame_mode) {
        // Frame mode - request both fields
        orc::FieldID field1_id(current_index * 2);
        orc::FieldID field2_id(current_index * 2 + 1);
        // Request first field - VBI dialog will need enhancement to display both fields
        pending_vbi_request_id_ = render_coordinator_->requestVBIData(current_view_node_id_, field1_id);
        // Second field support: Would require VBIDialog to handle dual-field display
        // For now, showing first field is sufficient for most use cases
    } else {
        // Field mode - request single field
        orc::FieldID field_id(current_index);
        pending_vbi_request_id_ = render_coordinator_->requestVBIData(current_view_node_id_, field_id);
    }
}

