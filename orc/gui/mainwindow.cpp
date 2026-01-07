/*
 * File:        mainwindow.cpp
 * Module:      orc-gui
 * Purpose:     Main application window
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "mainwindow.h"
#include "version.h"
#include "fieldpreviewwidget.h"
#include "previewdialog.h"
#include "vbidialog.h"
#include "hintsdialog.h"
#include "pulldowndialog.h"
#include "dropoutanalysisdialog.h"
#include "snranalysisdialog.h"
#include "burstlevelanalysisdialog.h"
#include "masklineconfigdialog.h"
#include "qualitymetricsdialog.h"
#include "projectpropertiesdialog.h"
#include "stageparameterdialog.h"
#include "inspection_dialog.h"
#include "dropout_editor_dialog.h"
#include "analysis/analysis_dialog.h"
#include "analysis/vectorscope_dialog.h"
#include "orcgraphicsview.h"
#include "render_coordinator.h"
#include "logging.h"
#include "../core/include/preview_renderer.h"
#include "../core/include/vbi_decoder.h"
#include "../core/include/dag_field_renderer.h"
#include "../core/analysis/dropout/dropout_analysis_decoder.h"
#include "../core/analysis/snr/snr_analysis_decoder.h"
#include "../core/include/stage_registry.h"
#include "../core/include/node_type.h"
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
#include <QApplication>
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
#include <QRegularExpression>

#include <queue>
#include <map>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , preview_dialog_(nullptr)
    , vbi_dialog_(nullptr)
    , pulldown_dialog_(nullptr)
    , dag_view_(nullptr)
    , dag_model_(nullptr)
    , dag_scene_(nullptr)
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
    , current_aspect_ratio_mode_(orc::AspectRatioMode::DAR_4_3)  // Default to 4:3
    , preview_update_timer_(nullptr)
    , pending_preview_index_(-1)
    , preview_update_pending_(false)
    , last_update_was_sequential_(false)
    , trigger_progress_dialog_(nullptr)
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
    
    // Initialize aspect ratio mode to match default
    render_coordinator_->setAspectRatioMode(current_aspect_ratio_mode_);
    
    // Create preview update debounce timer
    // This prevents excessive rendering during slider scrubbing by only updating
    // after the slider has been stationary for a short period
    preview_update_timer_ = new QTimer(this);
    preview_update_timer_->setSingleShot(true);  // Single-shot for debounce behavior
    preview_update_timer_->setInterval(200);  // 200ms delay after last slider movement
    connect(preview_update_timer_, &QTimer::timeout, this, [this]() {
        if (preview_update_pending_) {
            updateAllPreviewComponents();
            preview_update_pending_ = false;
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
    if (pulldown_dialog_) {
        delete pulldown_dialog_;
        pulldown_dialog_ = nullptr;
    }
    if (preview_dialog_) {
        delete preview_dialog_;
        preview_dialog_ = nullptr;
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!checkUnsavedChanges()) {
        event->ignore();
        return;
    }
    
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
    
    // Create hints dialog (initially hidden)
    hints_dialog_ = new HintsDialog(this);
    
    // Create pulldown dialog (initially hidden)
    pulldown_dialog_ = new PulldownDialog(this);
    
    // Note: Dropout, SNR, and Burst Level analysis dialogs are now created per-stage
    // in runAnalysisForNode() to allow each stage to have its own independent dialog
    
    // Create quality metrics dialog (initially hidden)
    quality_metrics_dialog_ = new QualityMetricsDialog(this);
    quality_metrics_dialog_->setWindowTitle("Field/Frame Quality Metrics");
    quality_metrics_dialog_->setAttribute(Qt::WA_DeleteOnClose, false);
    
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
            this, &MainWindow::onPreviewDialogExportPNG);
    connect(preview_dialog_, &PreviewDialog::showDropoutsChanged,
            this, [this](bool show) {
                render_coordinator_->setShowDropouts(show);
                updatePreview();
            });
        connect(preview_dialog_, &PreviewDialog::showVBIDialogRequested,
            this, &MainWindow::onShowVBIDialog);
    connect(preview_dialog_, &PreviewDialog::showHintsDialogRequested,
            this, &MainWindow::onShowHintsDialog);
    connect(preview_dialog_, &PreviewDialog::showQualityMetricsDialogRequested,
            this, &MainWindow::onShowQualityMetricsDialog);
    connect(preview_dialog_, &PreviewDialog::showPulldownDialogRequested,
            this, &MainWindow::onShowPulldownDialog);
    
    // Create QtNodes DAG editor
    dag_view_ = new OrcGraphicsView(this);
    dag_model_ = new OrcGraphModel(project_.coreProject(), dag_view_);
    dag_scene_ = new OrcGraphicsScene(*dag_model_, dag_view_);
    
    dag_view_->setScene(dag_scene_);
    // Set alignment so (0,0) appears at top-left of view
    dag_view_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    
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
    
    // Help menu
    auto* help_menu = menuBar()->addMenu("&Help");
    
    auto* about_action = help_menu->addAction("&About Orc GUI...");
    connect(about_action, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::setupToolbar()
{
    // Toolbar removed - was creating blank bar between menu and DAG editor
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
    // Check for unsaved changes before showing file dialog
    if (!checkUnsavedChanges()) {
        return;
    }
    
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
        updateUIState();
        
        statusBar()->showMessage("Project properties updated", 3000);
    }
}


void MainWindow::newProject(orc::VideoSystem video_format)
{
    // Check for unsaved changes before creating new project
    if (!checkUnsavedChanges()) {
        return;
    }
    
    // Construct a default filename for the new project dialog
    QDir dir(getLastProjectDirectory());
    QString defaultPath = dir.filePath("Untitled.orcprj");
    
    QString filename = QFileDialog::getSaveFileName(
        this,
        "New Project",
        defaultPath,  // Full path with default filename
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
    
    // Project loaded - user can select a stage in the DAG editor for viewing
    if (project_.hasSource()) {
        ORC_LOG_DEBUG("Source loaded - select a stage in DAG editor for viewing");
        
        // Show helpful message
        statusBar()->showMessage("Project loaded - select a stage in DAG editor to view", 5000);
    } else {
        ORC_LOG_DEBUG("Project has no source");
    }
    
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
    updateUIState();  // Update UI state after save (disable save button)
    statusBar()->showMessage("Project saved");
}

void MainWindow::saveProjectAs()
{
    // Determine the full default path (directory + filename)
    QString defaultPath;
    
    ORC_LOG_DEBUG("saveProjectAs: project path = '{}'", project_.projectPath().toStdString());
    ORC_LOG_DEBUG("saveProjectAs: project name = '{}'", project_.projectName().toStdString());
    
    if (!project_.projectPath().isEmpty()) {
        // Use existing project path if available
        defaultPath = project_.projectPath();
        ORC_LOG_DEBUG("saveProjectAs: using existing path: '{}'", defaultPath.toStdString());
    } else {
        // Construct filename from project name
        QString projectName = project_.projectName();
        if (projectName.isEmpty()) {
            projectName = "Untitled";
        }
        // Remove any characters that might be problematic in filenames
        projectName = projectName.replace(QRegularExpression("[<>:\"/\\\\|?*]"), "_");
        // Use QDir to properly construct the full path
        QDir dir(getLastProjectDirectory());
        defaultPath = dir.filePath(projectName + ".orcprj");
        ORC_LOG_DEBUG("saveProjectAs: constructed path: '{}'", defaultPath.toStdString());
    }
    
    ORC_LOG_DEBUG("saveProjectAs: final default path = '{}'", defaultPath.toStdString());
    
    QString filename = QFileDialog::getSaveFileName(
        this,
        "Save Project As",
        defaultPath,  // Full path including filename
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
    // Debounce preview updates to avoid excessive rendering during slider scrubbing
    // The timer will only fire after the slider has been stationary for 200ms
    // This provides smooth scrubbing while still allowing updates when paused
    
    pending_preview_index_ = index;
    preview_update_pending_ = true;
    
    // Restart the debounce timer - this cancels any pending update
    // and schedules a new one for 200ms from now
    preview_update_timer_->start();
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
    
    // Update dropouts button state based on the new output's availability
    bool dropouts_available = available_outputs_[index].dropouts_available;
    if (preview_dialog_ && preview_dialog_->dropoutsButton()) {
        if (!dropouts_available) {
            // Disable and turn off dropouts for outputs where they're not available
            preview_dialog_->dropoutsButton()->setEnabled(false);
            preview_dialog_->dropoutsButton()->setChecked(false);
            render_coordinator_->setShowDropouts(false);
        } else {
            // Re-enable dropouts button for outputs that support it
            preview_dialog_->dropoutsButton()->setEnabled(true);
        }
    }
    
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
    
    // Remember the selected mode
    current_aspect_ratio_mode_ = available_modes[index].mode;
    
    // Update core's aspect ratio mode - it will apply scaling in render_output
    render_coordinator_->setAspectRatioMode(current_aspect_ratio_mode_);
    
    // Trigger a refresh of the current preview
    updatePreview();
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

bool MainWindow::checkUnsavedChanges()
{
    // Check if project has unsaved changes
    if (!project_.isModified()) {
        return true;  // No unsaved changes, safe to proceed
    }
    
    // Show confirmation dialog
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Unsaved Changes");
    msgBox.setText("The project has unsaved changes.");
    msgBox.setInformativeText("Do you want to save your changes?");
    msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Save);
    msgBox.setIcon(QMessageBox::Question);
    
    int ret = msgBox.exec();
    
    switch (ret) {
        case QMessageBox::Save:
            // Always show SaveAs dialog to make it clear what's being saved
            saveProjectAs();
            // After saving, return to editor (don't proceed with the operation)
            // User must explicitly choose the action again after saving
            return false;
            
        case QMessageBox::Discard:
            // Discard changes and proceed
            return true;
            
        case QMessageBox::Cancel:
        default:
            // Cancel the operation
            return false;
    }
}

void MainWindow::updatePreviewInfo()
{
    if (current_view_node_id_.is_valid() == false) {
        preview_dialog_->previewInfoLabel()->setText("No stage selected");
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
    
    // Update pulldown observer menu item availability (only for NTSC)
    // Pulldown is NTSC-specific (3:2 pulldown for film conversion)
    auto video_format = project_.coreProject().get_video_format();
    bool is_ntsc = (video_format == orc::VideoSystem::NTSC);
    preview_dialog_->pulldownAction()->setEnabled(is_ntsc);
    
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
    display_info.current_number = current_index;
    display_info.total_count = total;
    display_info.has_field_info = false;
    
    ORC_LOG_DEBUG("updatePreviewInfo: display_info.type_name='{}', has_field_info={}",
                  display_info.type_name, display_info.has_field_info);
    
    // Update slider labels with range (0-indexed)
    preview_dialog_->sliderMinLabel()->setText(QString::number(0));
    preview_dialog_->sliderMaxLabel()->setText(QString::number(display_info.total_count - 1));
    
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
    
    // Position view to show top-left node
    positionViewToTopLeft();
    
    statusBar()->showMessage("Loaded DAG from project", 2000);
}

void MainWindow::positionViewToTopLeft()
{
    if (!dag_view_ || !dag_scene_) {
        return;
    }
    
    const auto& nodes = project_.coreProject().get_nodes();
    if (nodes.empty()) {
        return;
    }
    
    // Find the minimum x and y coordinates to determine top-left position
    double min_x = std::numeric_limits<double>::max();
    double min_y = std::numeric_limits<double>::max();
    
    for (const auto& node : nodes) {
        if (node.x_position < min_x) min_x = node.x_position;
        if (node.y_position < min_y) min_y = node.y_position;
    }
    
    // Add padding from viewport edges
    const double viewport_padding = 20.0;
    
    // Calculate the center point needed to show top-left node at viewport's top-left
    // We want min_x, min_y to appear at the top-left with padding, so we center on a point
    // that's offset by half the viewport size minus the padding
    QRectF viewportRect = dag_view_->viewport()->rect();
    QPointF centerPoint(min_x + viewportRect.width() / 2 - viewport_padding, 
                       min_y + viewportRect.height() / 2 - viewport_padding);
    dag_view_->centerOn(centerPoint);
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
            QString("Stage '%1' not found").arg(QString::fromStdString(node_id.to_string())));
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
    
    // Get display name for the dialog title
    std::string display_name = stage_name;  // Fallback to stage_name
    const orc::NodeTypeInfo* type_info = orc::get_node_type_info(stage_name);
    if (type_info && !type_info->display_name.empty()) {
        display_name = type_info->display_name;
    }
    
    // Show parameter dialog
    StageParameterDialog dialog(display_name, param_descriptors, current_values, this);
    
    if (dialog.exec() == QDialog::Accepted) {
        auto new_values = dialog.get_values();
        
        try {
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
                QString("Updated parameters for stage '%1'")
                    .arg(QString::fromStdString(node_id.to_string())),
                3000
            );
        } catch (const std::exception& e) {
            // Parameter validation failed - show error and reset parameters to empty
            QMessageBox::critical(this, "Parameter Validation Error",
                QString("Failed to set parameters: %1\n\nParameters have been reset.")
                    .arg(QString::fromStdString(e.what())));
            
            // Reset parameters to empty map
            std::map<std::string, orc::ParameterValue> empty_params;
            try {
                orc::project_io::set_node_parameters(project_.coreProject(), node_id, empty_params);
                
                // Rebuild DAG with empty parameters
                project_.rebuildDAG();
                updatePreviewRenderer();
                dag_model_->refresh();
                updatePreview();
            } catch (const std::exception& reset_error) {
                // If reset also fails, log it but don't crash
                ORC_LOG_ERROR("Failed to reset parameters after validation error: {}", reset_error.what());
            }
        }
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
    
    // Update UI state to reflect modified project (window title, save button, etc.)
    updateUIState();
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
    
    const double grid_spacing_x = 225.0;
    const double grid_spacing_y = 125.0;
    const double grid_offset_x = 50.0;  // Small offset from edges
    const double grid_offset_y = 50.0;  // Small offset from edges
    
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
        double x = grid_offset_x + depth * grid_spacing_x;
        
        for (size_t i = 0; i < nodes_with_order.size(); ++i) {
            const auto& node_id = nodes_with_order[i].second;
            double y = grid_offset_y + i * grid_spacing_y;
            node_y_position[node_id] = y;  // Remember position for next layer
            orc::project_io::set_node_position(project_.coreProject(), node_id, x, y);
        }
    }
    
    // Refresh the view
    dag_model_->refresh();
    
    // Position view to show top-left corner (where grid starts)
    positionViewToTopLeft();
    
    // Mark project as modified since we changed node positions
    project_.setModified(true);
    
    // Update UI to reflect modified state
    updateUIState();
    
    statusBar()->showMessage("Arranged DAG to grid", 2000);
}

void MainWindow::onAbout()
{
    QMessageBox about_box(this);
    about_box.setWindowTitle("About Orc GUI");
    about_box.setIconPixmap(QPixmap(":/orc-gui/icon.png").scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    
    QString about_text = QString(
        "<h2>Orc GUI</h2>"
        "<p><b>Version:</b> %1</p>"
        "<p>Decode Orchestration GUI</p>"
        "<p><b>Copyright:</b> © 2026 Simon Inns</p>"
        "<p><b>License:</b> GNU General Public License v3.0 or later</p>"
        "<p>This program is free software: you can redistribute it and/or modify "
        "it under the terms of the GNU General Public License as published by "
        "the Free Software Foundation, either version 3 of the License, or "
        "(at your option) any later version.</p>"
        "<p>This program is distributed in the hope that it will be useful, "
        "but WITHOUT ANY WARRANTY; without even the implied warranty of "
        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the "
        "GNU General Public License for more details.</p>"
        "<p>You should have received a copy of the GNU General Public License "
        "along with this program. If not, see "
        "<a href='https://www.gnu.org/licenses/'>https://www.gnu.org/licenses/</a>.</p>"
    ).arg(ORC_VERSION);
    
    about_box.setText(about_text);
    about_box.setTextFormat(Qt::RichText);
    about_box.setTextInteractionFlags(Qt::TextBrowserInteraction);
    about_box.exec();
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
        field_number = static_cast<uint64_t>(preview_dialog_->previewSlider()->value()) * 2;
    } else {
        field_number = static_cast<uint64_t>(preview_dialog_->previewSlider()->value());
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
    
    // Block signals while updating combo box
    preview_dialog_->aspectRatioCombo()->blockSignals(true);
    
    // Clear existing items
    preview_dialog_->aspectRatioCombo()->clear();
    
    // Populate combo from available modes
    int current_index = 0;
    for (size_t i = 0; i < available_modes.size(); ++i) {
        const auto& mode_info = available_modes[i];
        preview_dialog_->aspectRatioCombo()->addItem(QString::fromStdString(mode_info.display_name));
        
        // Track which index matches current mode
        if (mode_info.mode == current_aspect_ratio_mode_) {
            current_index = static_cast<int>(i);
        }
    }
    
    // Set current selection to match the current mode
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
    
    // Get count for current output type and option_id
    int new_total = 0;
    for (const auto& output : available_outputs_) {
        if (output.type == current_output_type_ && output.option_id == current_option_id_) {
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

void MainWindow::onPreviewDialogExportPNG()
{
    if (current_view_node_id_.is_valid() == false) {
        QMessageBox::information(this, "Export PNG", "No preview available to export.");
        return;
    }
    
    // Get filename from user
    QString filename = QFileDialog::getSaveFileName(
        this,
        "Export Preview as PNG",
        getLastExportDirectory(),
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
    setLastExportDirectory(QFileInfo(filename).absolutePath());
    
    int current_index = preview_dialog_->previewSlider()->value();
    
    // Request PNG save via coordinator (delegates to core)
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

QString MainWindow::getLastExportDirectory() const
{
    QSettings settings("orc-project", "orc-gui");
    QString dir = settings.value("lastExportDirectory", QString()).toString();
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        return QDir::homePath();
    }
    return dir;
}

void MainWindow::setLastExportDirectory(const QString& path)
{
    QSettings settings("orc-project", "orc-gui");
    settings.setValue("lastExportDirectory", path);
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
        ORC_LOG_ERROR("Stage '{}' not found in project", node_id);
        QMessageBox::warning(this, "Inspection Failed",
            QString("Stage '%1' not found.").arg(QString::fromStdString(node_id.to_string())));
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

    // Special-case: Mask Line Configuration uses a custom rules-based config dialog
    if (tool->id() == "mask_line_config") {
        ORC_LOG_DEBUG("Opening mask line configuration dialog for node '{}'", node_id.to_string());
        
        // Get current parameters from the node
        auto& project = project_.coreProject();
        auto node_it = std::find_if(project.get_nodes().begin(), project.get_nodes().end(),
            [&node_id](const orc::ProjectDAGNode& n) { return n.node_id == node_id; });
        
        if (node_it == project.get_nodes().end()) {
            QMessageBox::warning(this, "Error",
                "Could not find node in project.");
            return;
        }
        
        // Create and show the config dialog
        MaskLineConfigDialog dialog(this);
        
        // Load current parameters
        dialog.set_parameters(node_it->parameters);
        
        // Show dialog and apply if accepted
        if (dialog.exec() == QDialog::Accepted) {
            auto new_params = dialog.get_parameters();
            
            ORC_LOG_DEBUG("Mask line config accepted, applying parameters");
            
            try {
                // Update node parameters using project_io
                orc::project_io::set_node_parameters(project, node_id, new_params);
                
                // Mark project as modified
                project_.setModified(true);
                
                // Update UI to reflect modified state
                updateUIState();
                
                // Rebuild DAG to pick up the new parameter values
                project_.rebuildDAG();
                
                // Update the preview renderer with the new DAG
                updatePreviewRenderer();
                
                // Refresh QtNodes view
                dag_model_->refresh();
                
                // Update the preview to show the changes
                updatePreview();
                
                statusBar()->showMessage(
                    QString("Applied mask line configuration to '%1'")
                        .arg(QString::fromStdString(node_id.to_string())),
                    3000
                );
            } catch (const std::exception& e) {
                QMessageBox::critical(this, "Configuration Error",
                    QString("Failed to apply configuration: %1")
                        .arg(QString::fromStdString(e.what())));
            }
        }
        
        return;
    }

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

        // Trigger an immediate update to populate the vectorscope
        // If not currently previewing this node, switch to it
        if (current_view_node_id_ != node_id) {
            onNodeSelectedForView(node_id);
        } else {
            updatePreview();
        }
        return;
    }
    
    // Special-case: Dropout Editor opens interactive editor dialog
    if (tool->id() == "dropout_editor") {
        // Get the project
        if (!dag_model_) {
            ORC_LOG_ERROR("No DAG model available for dropout editor");
            return;
        }

        // Get the core project via the GUI project wrapper
        orc::Project& project = project_.coreProject();
        
        // Get node parameters
        const auto& nodes = project.get_nodes();
        auto node_it = std::find_if(nodes.begin(), nodes.end(),
            [&node_id](const auto& n) { return n.node_id == node_id; });
        
        if (node_it == nodes.end()) {
            ORC_LOG_ERROR("Node '{}' not found in project", node_id.to_string());
            return;
        }

        // Get source representation by executing the DAG up to the input node
        std::shared_ptr<const orc::VideoFieldRepresentation> source_repr;
        
        // Get the DAG and execute it
        auto dag = project_.getDAG();
        if (!dag) {
            QMessageBox::warning(this, "Error", "Failed to build project DAG");
            return;
        }

        // Execute the DAG to get all artifacts
        try {
            // Find the input node for this dropout_map stage
            const auto& edges = project.get_edges();
            orc::NodeID input_node_id;
            for (const auto& edge : edges) {
                if (edge.target_node_id == node_id) {
                    input_node_id = edge.source_node_id;
                    break;
                }
            }
            
            if (!input_node_id.is_valid()) {
                QMessageBox::warning(this, "Error",
                    "Dropout map stage has no input connected. Please connect a source.");
                return;
            }
            
            // Execute the DAG up to the input node
            orc::DAGExecutor executor;
            auto node_outputs = executor.execute_to_node(*dag, input_node_id);
            
            // Get the output artifacts from the input node
            auto it = node_outputs.find(input_node_id);
            if (it != node_outputs.end() && !it->second.empty()) {
                // The first output is the VideoFieldRepresentation
                source_repr = std::dynamic_pointer_cast<const orc::VideoFieldRepresentation>(
                    it->second[0]);
            }
        } catch (const std::exception& e) {
            ORC_LOG_ERROR("Failed to execute DAG: {}", e.what());
            QMessageBox::warning(this, "Error",
                QString("Failed to execute project graph: %1").arg(e.what()));
            return;
        }

        if (!source_repr) {
            QMessageBox::warning(this, "Error",
                "Could not get video field data from input. "
                "Ensure the dropout_map stage has a valid video source connected.");
            return;
        }

        // Get current dropout map from node parameters
        std::string dropout_map_str;
        if (node_it->parameters.count("dropout_map")) {
            dropout_map_str = std::get<std::string>(node_it->parameters.at("dropout_map"));
        }

        // Parse existing dropout map
        auto existing_map = orc::DropoutMapStage::parse_dropout_map(dropout_map_str);

        // Open the editor dialog as a non-modal independent window
        auto* dialog = new DropoutEditorDialog(source_repr, existing_map, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        
        // Connect to handle the accepted signal
        connect(dialog, &QDialog::accepted, this, [this, dialog, node_id, &project, dropout_map_str]() {
            // Get the edited dropout map
            auto edited_map = dialog->getDropoutMap();
            
            ORC_LOG_DEBUG("Dropout editor returned map with {} field entries", edited_map.size());
            
            // Encode back to string
            std::string new_dropout_map_str = orc::DropoutMapStage::encode_dropout_map(edited_map);
            
            ORC_LOG_DEBUG("Encoded dropout map: original='{}' new='{}'", dropout_map_str, new_dropout_map_str);
            
            // Only update if the dropout map actually changed
            if (new_dropout_map_str == dropout_map_str) {
                ORC_LOG_DEBUG("Dropout map unchanged for node '{}'", node_id.to_string());
                return;
            }
            
            // Update node parameters using project_io
            std::map<std::string, orc::ParameterValue> new_params;
            new_params["dropout_map"] = new_dropout_map_str;
            
            orc::project_io::set_node_parameters(project, node_id, new_params);
            
            // Mark project as modified
            project_.setModified(true);
            
            // Update UI to reflect modified state
            updateUIState();
            
            // Rebuild DAG
            project_.rebuildDAG();
            
            // Update the preview renderer with the new DAG (contains updated parameters)
            updatePreviewRenderer();
            
            ORC_LOG_DEBUG("Dropout map updated for node '{}'", node_id.to_string());
            
            // Trigger preview update to show the changes
            updatePreview();
        });
        
        // Show as non-modal window
        dialog->show();
        return;
    }
    
    // Special-case: Dropout Analysis triggers batch processing and shows dialog
    if (tool->id() == "dropout_analysis") {
        // Get node info from project
        auto& project = project_.coreProject();
        auto node_it = std::find_if(project.get_nodes().begin(), project.get_nodes().end(),
            [&node_id](const orc::ProjectDAGNode& n) { return n.node_id == node_id; });
        
        QString node_label = QString::fromStdString(stage_name);
        if (node_it != project.get_nodes().end()) {
            node_label = QString::fromStdString(
                node_it->user_label.empty() ? node_it->display_name : node_it->user_label);
        }
        QString title = QString("Dropout Analysis - %1 (%2)")
            .arg(node_label)
            .arg(QString::fromStdString(node_id.to_string()));
        
        // Get or create dialog for this stage
        DropoutAnalysisDialog* dialog = nullptr;
        auto it = dropout_analysis_dialogs_.find(node_id);
        if (it == dropout_analysis_dialogs_.end()) {
            // Create new dialog for this stage
            dialog = new DropoutAnalysisDialog(this);
            dialog->setWindowTitle(title);
            dialog->setAttribute(Qt::WA_DeleteOnClose, true);
            
            // Connect mode changed to re-request data
            connect(dialog, &DropoutAnalysisDialog::modeChanged,
                    [this, node_id, dialog]() {
                        if (dialog->isVisible()) {
                            // Show progress dialog for this stage
                            auto& prog_dialog = dropout_progress_dialogs_[node_id];
                            if (prog_dialog) {
                                delete prog_dialog;
                            }
                            prog_dialog = new QProgressDialog("Loading dropout analysis data...", QString(), 0, 100, this);
                            prog_dialog->setWindowTitle(dialog->windowTitle());
                            prog_dialog->setWindowModality(Qt::WindowModal);
                            prog_dialog->setMinimumDuration(0);
                            prog_dialog->setCancelButton(nullptr);
                            prog_dialog->setValue(0);
                            prog_dialog->show();
                            
                            auto mode = dialog->getCurrentMode();
                            uint64_t request_id = render_coordinator_->requestDropoutData(node_id, mode);
                            pending_dropout_requests_[request_id] = node_id;
                        }
                    });
            
            // Connect destroyed signal to clean up map entry
            connect(dialog, &QObject::destroyed, [this, node_id]() {
                dropout_analysis_dialogs_.erase(node_id);
                dropout_progress_dialogs_.erase(node_id);
            });
            
            dropout_analysis_dialogs_[node_id] = dialog;
        } else {
            dialog = it->second;
        }
        
        // Create and show progress dialog for this stage
        auto& prog_dialog = dropout_progress_dialogs_[node_id];
        if (prog_dialog) {
            delete prog_dialog;
        }
        prog_dialog = new QProgressDialog("Loading dropout analysis data...", QString(), 0, 100, this);
        prog_dialog->setWindowTitle(dialog->windowTitle());
        prog_dialog->setWindowModality(Qt::WindowModal);
        prog_dialog->setMinimumDuration(0);
        prog_dialog->setCancelButton(nullptr);
        prog_dialog->setValue(0);
        prog_dialog->show();
        
        // Show the dialog (but it will be empty until data arrives)
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        
        // Request dropout data from coordinator (triggers batch processing)
        auto mode = dialog->getCurrentMode();
        uint64_t request_id = render_coordinator_->requestDropoutData(node_id, mode);
        pending_dropout_requests_[request_id] = node_id;
        
        ORC_LOG_DEBUG("Requested dropout analysis data for node '{}', mode {}, request_id={}",
                      node_id.to_string(), static_cast<int>(mode), request_id);
        return;
    }
    
    // Special-case: SNR Analysis triggers batch processing and shows dialog
    if (tool->id() == "snr_analysis") {
        // Get node info from project
        auto& project = project_.coreProject();
        auto node_it = std::find_if(project.get_nodes().begin(), project.get_nodes().end(),
            [&node_id](const orc::ProjectDAGNode& n) { return n.node_id == node_id; });
        
        QString node_label = QString::fromStdString(stage_name);
        if (node_it != project.get_nodes().end()) {
            node_label = QString::fromStdString(
                node_it->user_label.empty() ? node_it->display_name : node_it->user_label);
        }
        QString title = QString("SNR Analysis - %1 (%2)")
            .arg(node_label)
            .arg(QString::fromStdString(node_id.to_string()));
        
        // Get or create dialog for this stage
        SNRAnalysisDialog* dialog = nullptr;
        auto it = snr_analysis_dialogs_.find(node_id);
        if (it == snr_analysis_dialogs_.end()) {
            // Create new dialog for this stage
            dialog = new SNRAnalysisDialog(this);
            dialog->setWindowTitle(title);
            dialog->setAttribute(Qt::WA_DeleteOnClose, true);
            
            // Connect mode changed to re-request data
            connect(dialog, &SNRAnalysisDialog::modeChanged,
                    [this, node_id, dialog]() {
                        if (dialog->isVisible()) {
                            // Show progress dialog for this stage
                            auto& prog_dialog = snr_progress_dialogs_[node_id];
                            if (prog_dialog) {
                                delete prog_dialog;
                            }
                            prog_dialog = new QProgressDialog("Loading SNR analysis data...", QString(), 0, 100, this);
                            prog_dialog->setWindowTitle(dialog->windowTitle());
                            prog_dialog->setWindowModality(Qt::WindowModal);
                            prog_dialog->setMinimumDuration(0);
                            prog_dialog->setCancelButton(nullptr);
                            prog_dialog->setValue(0);
                            prog_dialog->show();
                            
                            auto mode = dialog->getCurrentMode();
                            uint64_t request_id = render_coordinator_->requestSNRData(node_id, mode);
                            pending_snr_requests_[request_id] = node_id;
                        }
                    });
            
            // Connect destroyed signal to clean up map entry
            connect(dialog, &QObject::destroyed, [this, node_id]() {
                snr_analysis_dialogs_.erase(node_id);
                snr_progress_dialogs_.erase(node_id);
            });
            
            snr_analysis_dialogs_[node_id] = dialog;
        } else {
            dialog = it->second;
        }
        
        // Create and show progress dialog for this stage
        auto& prog_dialog = snr_progress_dialogs_[node_id];
        if (prog_dialog) {
            delete prog_dialog;
        }
        prog_dialog = new QProgressDialog("Loading SNR analysis data...", QString(), 0, 100, this);
        prog_dialog->setWindowTitle(dialog->windowTitle());
        prog_dialog->setWindowModality(Qt::WindowModal);
        prog_dialog->setMinimumDuration(0);
        prog_dialog->setCancelButton(nullptr);
        prog_dialog->setValue(0);
        prog_dialog->show();
        
        // Show the dialog (but it will be empty until data arrives)
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        
        // Request SNR data from coordinator (triggers batch processing)
        auto mode = dialog->getCurrentMode();
        uint64_t request_id = render_coordinator_->requestSNRData(node_id, mode);
        pending_snr_requests_[request_id] = node_id;
        
        ORC_LOG_DEBUG("Requested SNR analysis data for node '{}', mode {}, request_id={}",
                      node_id.to_string(), static_cast<int>(mode), request_id);
        return;
    }
    
    // Special-case: Burst Level Analysis triggers batch processing and shows dialog
    if (tool->id() == "burst_level_analysis") {
        // Get node info from project
        auto& project = project_.coreProject();
        auto node_it = std::find_if(project.get_nodes().begin(), project.get_nodes().end(),
            [&node_id](const orc::ProjectDAGNode& n) { return n.node_id == node_id; });
        
        QString node_label = QString::fromStdString(stage_name);
        if (node_it != project.get_nodes().end()) {
            node_label = QString::fromStdString(
                node_it->user_label.empty() ? node_it->display_name : node_it->user_label);
        }
        QString title = QString("Burst Level Analysis - %1 (%2)")
            .arg(node_label)
            .arg(QString::fromStdString(node_id.to_string()));
        
        // Get or create dialog for this stage
        BurstLevelAnalysisDialog* dialog = nullptr;
        auto it = burst_level_analysis_dialogs_.find(node_id);
        if (it == burst_level_analysis_dialogs_.end()) {
            // Create new dialog for this stage
            dialog = new BurstLevelAnalysisDialog(this);
            dialog->setWindowTitle(title);
            dialog->setAttribute(Qt::WA_DeleteOnClose, true);
            
            // Connect destroyed signal to clean up map entry
            connect(dialog, &QObject::destroyed, [this, node_id]() {
                burst_level_analysis_dialogs_.erase(node_id);
                burst_level_progress_dialogs_.erase(node_id);
            });
            
            burst_level_analysis_dialogs_[node_id] = dialog;
        } else {
            dialog = it->second;
        }
        
        // Create and show progress dialog for this stage
        auto& prog_dialog = burst_level_progress_dialogs_[node_id];
        if (prog_dialog) {
            delete prog_dialog;
        }
        prog_dialog = new QProgressDialog("Loading burst level analysis data...", QString(), 0, 100, this);
        prog_dialog->setWindowTitle(dialog->windowTitle());
        prog_dialog->setWindowModality(Qt::WindowModal);
        prog_dialog->setMinimumDuration(0);
        prog_dialog->setCancelButton(nullptr);
        prog_dialog->setValue(0);
        prog_dialog->show();
        
        // Show the dialog (but it will be empty until data arrives)
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        
        // Request burst level data from coordinator (triggers batch processing)
        uint64_t request_id = render_coordinator_->requestBurstLevelData(node_id);
        pending_burst_level_requests_[request_id] = node_id;
        
        ORC_LOG_DEBUG("Requested burst level analysis data for node '{}', request_id={}",
                      node_id.to_string(), request_id);
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
        ORC_LOG_DEBUG("Applying analysis results to node '{}'", node_id.to_string());
        
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

void MainWindow::onShowHintsDialog()
{
    if (!hints_dialog_) {
        return;
    }
    
    // Show the dialog first
    hints_dialog_->show();
    hints_dialog_->raise();
    hints_dialog_->activateWindow();
    
    // Update hints information after showing
    updateHintsDialog();
}

void MainWindow::onShowQualityMetricsDialog()
{
    if (!quality_metrics_dialog_) {
        return;
    }
    
    // Show the dialog first
    quality_metrics_dialog_->show();
    quality_metrics_dialog_->raise();
    quality_metrics_dialog_->activateWindow();
    
    // Update quality metrics information after showing
    updateQualityMetricsDialog();
}

void MainWindow::onShowPulldownDialog()
{
    if (!pulldown_dialog_) {
        return;
    }
    
    // Show the dialog first
    pulldown_dialog_->show();
    pulldown_dialog_->raise();
    pulldown_dialog_->activateWindow();
    
    // Update pulldown information after showing
    updatePulldownDialog();
}

void MainWindow::updateQualityMetricsDialog()
{
    // Only update if dialog is visible
    if (!quality_metrics_dialog_ || !quality_metrics_dialog_->isVisible()) {
        return;
    }
    
    // Get current field/frame being displayed
    if (!current_view_node_id_.is_valid()) {
        quality_metrics_dialog_->clearMetrics();
        return;
    }
    
    // Get the current index from the preview slider
    int current_index = preview_dialog_->previewSlider()->value();
    
    // Check if we're in frame mode (any mode that shows two fields)
    bool is_frame_mode = (current_output_type_ == orc::PreviewOutputType::Frame ||
                         current_output_type_ == orc::PreviewOutputType::Frame_Reversed ||
                         current_output_type_ == orc::PreviewOutputType::Split);
    
    // Get field IDs based on preview mode
    orc::FieldID field1_id = is_frame_mode ? orc::FieldID(current_index * 2) : orc::FieldID(current_index);
    orc::FieldID field2_id = is_frame_mode ? orc::FieldID(current_index * 2 + 1) : orc::FieldID(0);
    
    // Get field representations from the current DAG/node
    // Note: This is a synchronous access - we'll create a temporary renderer
    try {
        auto dag = project_.getDAG();
        if (!dag) {
            quality_metrics_dialog_->clearMetrics();
            return;
        }
        
        // Create a temporary renderer to get the field representation(s)
        orc::DAGFieldRenderer renderer(dag);
        
        if (is_frame_mode) {
            // Render both fields for frame mode
            auto render_result1 = renderer.render_field_at_node(current_view_node_id_, field1_id);
            auto render_result2 = renderer.render_field_at_node(current_view_node_id_, field2_id);
            
            if (!render_result1.is_valid || !render_result1.representation ||
                !render_result2.is_valid || !render_result2.representation) {
                quality_metrics_dialog_->clearMetrics();
                return;
            }
            
            // Update dialog with both fields
            quality_metrics_dialog_->updateMetricsForFrame(
                render_result1.representation, field1_id,
                render_result2.representation, field2_id
            );
        } else {
            // Render single field
            auto render_result = renderer.render_field_at_node(current_view_node_id_, field1_id);
            
            if (!render_result.is_valid || !render_result.representation) {
                quality_metrics_dialog_->clearMetrics();
                return;
            }
            
            // Update dialog with single field
            quality_metrics_dialog_->updateMetrics(render_result.representation, field1_id);
        }
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Failed to update quality metrics: {}", e.what());
        quality_metrics_dialog_->clearMetrics();
    }
}

// Removed Vectorscope dialog launcher from Preview; vectorscope opens from node context menu

void MainWindow::updateAllPreviewComponents()
{
    updatePreview();
    updatePreviewInfo();
    updateVBIDialog();
    updateHintsDialog();
    updateQualityMetricsDialog();
    updatePulldownDialog();
    
    // For analysis dialogs, only update the frame marker position (not the full data)
    // Full data is only loaded when the dialog is first opened
    int32_t current_frame = 1;
    if (preview_dialog_ && preview_dialog_->previewSlider()) {
        current_frame = static_cast<int32_t>(preview_dialog_->previewSlider()->value()) + 1;
    }
    
    // Update all visible dropout analysis dialogs
    for (auto& pair : dropout_analysis_dialogs_) {
        if (pair.second && pair.second->isVisible()) {
            pair.second->updateFrameMarker(current_frame);
        }
    }
    
    // Update all visible SNR analysis dialogs
    for (auto& pair : snr_analysis_dialogs_) {
        if (pair.second && pair.second->isVisible()) {
            pair.second->updateFrameMarker(current_frame);
        }
    }
    
    // Update all visible burst level analysis dialogs
    for (auto& pair : burst_level_analysis_dialogs_) {
        if (pair.second && pair.second->isVisible()) {
            pair.second->updateFrameMarker(current_frame);
        }
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

void MainWindow::updateHintsDialog()
{
    // Only update if hints dialog is visible
    if (!hints_dialog_ || !hints_dialog_->isVisible()) {
        return;
    }
    
    // Get current field being displayed
    if (!current_view_node_id_.is_valid()) {
        hints_dialog_->clearHints();
        return;
    }
    
    // Get the current index from the preview slider
    int current_index = preview_dialog_->previewSlider()->value();
    
    // Check if we're in frame mode (any mode that shows two fields)
    bool is_frame_mode = (current_output_type_ == orc::PreviewOutputType::Frame ||
                         current_output_type_ == orc::PreviewOutputType::Frame_Reversed ||
                         current_output_type_ == orc::PreviewOutputType::Split);
    
    // For hints, we'll show the first field's hints (or single field in field mode)
    orc::FieldID field_id = is_frame_mode ? orc::FieldID(current_index * 2) : orc::FieldID(current_index);
    
    // Get hints from the current DAG/node
    // Note: This is a synchronous access - we'll create a temporary renderer
    try {
        auto dag = project_.getDAG();
        if (!dag) {
            hints_dialog_->clearHints();
            return;
        }
        
        // Create a temporary renderer to get the field representation
        orc::DAGFieldRenderer renderer(dag);
        
        // Render the field at the current node
        auto render_result = renderer.render_field_at_node(current_view_node_id_, field_id);
        
        if (!render_result.is_valid || !render_result.representation) {
            hints_dialog_->clearHints();
            return;
        }
        
        // Get hints from the field representation
        auto parity_hint = render_result.representation->get_field_parity_hint(field_id);
        auto phase_hint = render_result.representation->get_field_phase_hint(field_id);
        auto active_line_hint = render_result.representation->get_active_line_hint();
        auto video_params = render_result.representation->get_video_parameters();
        
        // Update the dialog
        hints_dialog_->updateFieldParityHint(parity_hint);
        hints_dialog_->updateFieldPhaseHint(phase_hint);
        hints_dialog_->updateActiveLineHint(active_line_hint);
        hints_dialog_->updateVideoParameters(video_params);
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Failed to get hints: {}", e.what());
        hints_dialog_->clearHints();
    }
}

void MainWindow::updatePulldownDialog()
{
    // Only update if pulldown dialog is visible
    if (!pulldown_dialog_ || !pulldown_dialog_->isVisible()) {
        return;
    }
    
    // Get current field being displayed
    if (!current_view_node_id_.is_valid()) {
        pulldown_dialog_->clearPulldownInfo();
        return;
    }
    
    // Get the current index from the preview slider
    int current_index = preview_dialog_->previewSlider()->value();
    
    // Check if we're in frame mode (any mode that shows two fields)
    bool is_frame_mode = (current_output_type_ == orc::PreviewOutputType::Frame ||
                         current_output_type_ == orc::PreviewOutputType::Frame_Reversed ||
                         current_output_type_ == orc::PreviewOutputType::Split);
    
    // For pulldown, we'll show the first field's observation (or single field in field mode)
    orc::FieldID field_id = is_frame_mode ? orc::FieldID(current_index * 2) : orc::FieldID(current_index);
    
    // Get pulldown observation from the current DAG/node
    try {
        auto dag = project_.getDAG();
        if (!dag) {
            pulldown_dialog_->clearPulldownInfo();
            return;
        }
        
        // Create a temporary renderer to get the field representation
        orc::DAGFieldRenderer renderer(dag);
        
        // Render the field at the current node
        auto render_result = renderer.render_field_at_node(current_view_node_id_, field_id);
        
        if (!render_result.is_valid || !render_result.representation) {
            pulldown_dialog_->clearPulldownInfo();
            return;
        }
        
        // Get observations from the field representation
        auto observations = render_result.representation->get_observations(field_id);
        
        // Find pulldown observation
        std::shared_ptr<orc::PulldownObservation> pulldown_obs = nullptr;
        for (const auto& obs : observations) {
            if (obs->observation_type() == "Pulldown") {
                pulldown_obs = std::dynamic_pointer_cast<orc::PulldownObservation>(obs);
                break;
            }
        }
        
        // Update the dialog
        if (pulldown_obs) {
            pulldown_dialog_->updatePulldownObservation(pulldown_obs);
        } else {
            // No pulldown observation found - might be PAL or CLV format
            pulldown_dialog_->clearPulldownInfo();
        }
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Failed to get pulldown observation: {}", e.what());
        pulldown_dialog_->clearPulldownInfo();
    }
}
