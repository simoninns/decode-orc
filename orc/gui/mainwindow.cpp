/*
 * File:        mainwindow.cpp
 * Module:      orc-gui
 * Purpose:     Main application window
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "mainwindow.h"
#include "version.h"
#include "fieldpreviewwidget.h"
#include "previewdialog.h"
#include "linescopedialog.h"
#include "fieldtimingdialog.h"
#include "fieldtimingwidget.h"
#include "vbidialog.h"
#include "hintsdialog.h"
#include "ntscobserverdialog.h"
#include "dropoutanalysisdialog.h"
#include "snranalysisdialog.h"
#include "burstlevelanalysisdialog.h"
#include "masklineconfigdialog.h"
#include "ffmpegpresetdialog.h"
#include "qualitymetricsdialog.h"
#include "projectpropertiesdialog.h"
#include "stageparameterdialog.h"
#include "inspection_dialog.h"
#include "dropout_editor_dialog.h"
#include "generic_analysis_dialog.h"
#include "analysis/vectorscope_dialog.h"  // Safe now - uses pimpl to hide core types
#include "orcgraphicsview.h"
#include "render_coordinator.h"
#include "logging.h"
#include "presenters/include/hints_view_models.h"
#include "presenters/include/hints_presenter.h"
#include "presenters/include/vbi_presenter.h"
#include "presenters/include/ntsc_observation_presenter.h"
#include "presenters/include/project_presenter.h"
#include "presenters/include/render_presenter.h"
#include "presenters/include/analysis_presenter.h"
#include <node_type.h>
#include <common_types.h>

// Forward declarations for core types used via opaque pointers
namespace orc {
    class DAG;
    class Project;
    class VideoFieldRepresentation;
    class ObservationContext;
}

#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QDir>
#include <QMenuBar>
#include <QToolBar>
#include <QInputDialog>
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
#include <limits>

#include <queue>
#include <map>

// Helper functions to convert between common types and presenter types
namespace {
    orc::presenters::VideoFormat toPresenterVideoFormat(orc::VideoSystem system) {
        switch (system) {
            case orc::VideoSystem::NTSC: return orc::presenters::VideoFormat::NTSC;
            case orc::VideoSystem::PAL: return orc::presenters::VideoFormat::PAL;
            case orc::VideoSystem::Unknown: return orc::presenters::VideoFormat::Unknown;
        }
        return orc::presenters::VideoFormat::Unknown;
    }
    
    orc::presenters::SourceType toPresenterSourceType(orc::SourceType type) {
        switch (type) {
            case orc::SourceType::Composite: return orc::presenters::SourceType::Composite;
            case orc::SourceType::YC: return orc::presenters::SourceType::YC;
            case orc::SourceType::Unknown: return orc::presenters::SourceType::Unknown;
        }
        return orc::presenters::SourceType::Unknown;
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , preview_dialog_(nullptr)
    , vbi_dialog_(nullptr)
    , ntsc_observer_dialog_(nullptr)
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
    , last_snr_mode_(orc::SNRAnalysisMode::WHITE)
    , last_snr_output_type_(orc::PreviewOutputType::Frame)
    , current_output_type_(orc::PreviewOutputType::Frame)
    , current_option_id_("frame")  // Default to "Frame (Y)" option
    , current_aspect_ratio_mode_(orc::AspectRatioMode::DAR_4_3)  // Default to 4:3
    , preview_update_timer_(nullptr)
    , pending_preview_index_(-1)
    , preview_update_pending_(false)
    , trigger_progress_dialog_(nullptr)
{
    // Create and start render coordinator
    render_coordinator_ = std::make_unique<RenderCoordinator>(this);

    // Presenter for hint data (uses DAG provider to fetch hints via core renderer)
    hints_presenter_ = std::make_unique<orc::presenters::HintsPresenter>(
        std::function<std::shared_ptr<void>()>([this]() -> std::shared_ptr<void> {
            return project_.getDAG();
        })
    );

    // Presenter for VBI observations
    vbi_presenter_ = std::make_unique<orc::presenters::VbiPresenter>([this]() -> std::shared_ptr<void> {
        return project_.getDAG();
    });
    
    // Presenter for dropout editing (uses ProjectPresenter for delegation)
    dropout_presenter_ = std::make_unique<orc::presenters::DropoutPresenter>(
        *project_.presenter()
    );
    
    // Connect coordinator signals (emitted from worker thread; queue to GUI thread)
    connect(render_coordinator_.get(), &RenderCoordinator::previewReady,
            this, &MainWindow::onPreviewReady, Qt::QueuedConnection);
    connect(render_coordinator_.get(), &RenderCoordinator::vbiDataReady,
            this, &MainWindow::onVBIDataReady, Qt::QueuedConnection);
    connect(render_coordinator_.get(), &RenderCoordinator::availableOutputsReady,
            this, &MainWindow::onAvailableOutputsReady, Qt::QueuedConnection);
    connect(render_coordinator_.get(), &RenderCoordinator::lineSamplesReady,
            this, &MainWindow::onLineSamplesReady, Qt::QueuedConnection);
    connect(render_coordinator_.get(), &RenderCoordinator::fieldTimingDataReady,
            this, &MainWindow::onFieldTimingDataReady, Qt::QueuedConnection);
    connect(render_coordinator_.get(), &RenderCoordinator::dropoutDataReady,
            this, &MainWindow::onDropoutDataReady, Qt::QueuedConnection);
    connect(render_coordinator_.get(), &RenderCoordinator::dropoutProgress,
            this, &MainWindow::onDropoutProgress, Qt::QueuedConnection);
    connect(render_coordinator_.get(), &RenderCoordinator::snrDataReady,
            this, &MainWindow::onSNRDataReady, Qt::QueuedConnection);
    connect(render_coordinator_.get(), &RenderCoordinator::snrProgress,
            this, &MainWindow::onSNRProgress, Qt::QueuedConnection);
    connect(render_coordinator_.get(), &RenderCoordinator::burstLevelDataReady,
            this, &MainWindow::onBurstLevelDataReady, Qt::QueuedConnection);
    connect(render_coordinator_.get(), &RenderCoordinator::burstLevelProgress,
            this, &MainWindow::onBurstLevelProgress, Qt::QueuedConnection);
    connect(render_coordinator_.get(), &RenderCoordinator::triggerProgress,
            this, &MainWindow::onTriggerProgress, Qt::QueuedConnection);
    connect(render_coordinator_.get(), &RenderCoordinator::triggerComplete,
            this, &MainWindow::onTriggerComplete, Qt::QueuedConnection);
    connect(render_coordinator_.get(), &RenderCoordinator::frameLineNavigationReady,
            this, &MainWindow::onFrameLineNavigationReady, Qt::QueuedConnection);
    connect(render_coordinator_.get(), &RenderCoordinator::error,
            this, &MainWindow::onCoordinatorError, Qt::QueuedConnection);
    
    // Set the project for the render coordinator (required before updateDAG)
    render_coordinator_->setProject(project_.presenter()->getCoreProjectHandle());
    
    // Start the coordinator worker thread
    render_coordinator_->start();
    
    // Aspect ratio display is handled exclusively in GUI (no core scaling)
    
    // Create preview update debounce timer
    // This prevents excessive rendering during slider scrubbing by only updating
    // after the slider has been stationary for a short period
    preview_update_timer_ = new QTimer(this);
    preview_update_timer_->setSingleShot(true);  // Single-shot for debounce behavior
    preview_update_timer_->setInterval(100);  // 100ms delay for debounce (slider scrubbing)
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
    if (ntsc_observer_dialog_) {
        delete ntsc_observer_dialog_;
        ntsc_observer_dialog_ = nullptr;
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
    
    // Create NTSC observer dialog (initially hidden)
    ntsc_observer_dialog_ = new NtscObserverDialog(this);
    
    // Note: Dropout, SNR, and Burst Level analysis dialogs are now created per-stage
    // in runAnalysisForNode() to allow each stage to have its own independent dialog
    
    // Create quality metrics dialog (initially hidden)
    quality_metrics_dialog_ = new QualityMetricsDialog(this);
    quality_metrics_dialog_->setWindowTitle("Field/Frame Quality Metrics");
    quality_metrics_dialog_->setAttribute(Qt::WA_DeleteOnClose, false);
    
    // Connect preview dialog signals
    connect(preview_dialog_, &PreviewDialog::previewIndexChanged,
            this, &MainWindow::onPreviewIndexChanged);
    // For sequential navigation (button clicks), implement intelligent throttling:
    // Cache the latest requested index. If no render is in-flight, send immediately.
    // If a render is in-flight, the cached index will be processed when it completes.
    connect(preview_dialog_, &PreviewDialog::sequentialPreviewRequested,
            this, [this](int index) {
                // Skip debounce timer for button clicks
                preview_update_timer_->stop();
                preview_update_pending_ = false;
                
                // Always update the latest requested index (cache the latest click)
                latest_requested_preview_index_ = index;
                
                if (!preview_render_in_flight_) {
                    // No render in progress - send request immediately
                    pending_preview_index_ = index;
                    updateAllPreviewComponents();
                }
                // If render is in-flight, onPreviewReady will check if latest_requested differs
                // from what was just rendered and issue a new request if needed
            });
    connect(preview_dialog_, &PreviewDialog::previewModeChanged,
            this, &MainWindow::onPreviewModeChanged);
    connect(preview_dialog_, &PreviewDialog::signalChanged,
            this, [this](int) { updatePreview(); });  // Re-render when signal selection changes
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
    connect(preview_dialog_, &PreviewDialog::showNtscObserverDialogRequested,
            this, &MainWindow::onShowNtscObserverDialog);
    connect(preview_dialog_, &PreviewDialog::lineScopeRequested,
            this, &MainWindow::onLineScopeRequested);
    connect(preview_dialog_, &PreviewDialog::lineNavigationRequested,
            this, &MainWindow::onLineNavigation);
    connect(preview_dialog_, &PreviewDialog::sampleMarkerMovedInLineScope,
            this, &MainWindow::onSampleMarkerMoved);
    connect(preview_dialog_, &PreviewDialog::fieldTimingRequested,
            this, &MainWindow::onFieldTimingRequested);
    
    // Connect preview frame changed signal to line scope
    // When frame changes, line scope should refresh samples at its current field/line
    auto line_scope = preview_dialog_->lineScopeDialog();
    if (line_scope) {
        connect(preview_dialog_, &PreviewDialog::previewFrameChanged,
                this, &MainWindow::onLineScopeRefreshAtFieldLine);
        connect(line_scope, &LineScopeDialog::dialogClosed,
                this, &MainWindow::onLineScopeDialogClosed);
    }
    
    // Connect preview frame changed signal to field timing
    // When frame changes, field timing should refresh if visible
    auto field_timing = preview_dialog_->fieldTimingDialog();
    if (field_timing) {
        connect(preview_dialog_, &PreviewDialog::previewFrameChanged,
                this, [this]() {
                    auto* dialog = preview_dialog_->fieldTimingDialog();
                    if (dialog && dialog->isVisible()) {
                        onFieldTimingRequested();
                    }
                });
        connect(field_timing, &FieldTimingDialog::refreshRequested,
                this, &MainWindow::onFieldTimingRequested);
        connect(field_timing, &FieldTimingDialog::setCrosshairsRequested,
                this, &MainWindow::onSetCrosshairsFromFieldTiming);
    }
    
    // Create QtNodes DAG editor
    dag_view_ = new OrcGraphicsView(this);
    dag_model_ = new OrcGraphModel(*project_.presenter(), dag_view_);
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
    
    // New Project action - opens dialog for all four project types
    auto* new_project_action = file_menu->addAction("&New Project...");
    new_project_action->setShortcut(QKeySequence::New);
    connect(new_project_action, &QAction::triggered, this, &MainWindow::onNewProject);
    
    auto* quick_project_action = file_menu->addAction("&Quick Project...");
    quick_project_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
    connect(quick_project_action, &QAction::triggered, this, &MainWindow::onQuickProject);
    
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
    // Show selection dialog for all four project types
    newProject();
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
    dialog.setProjectName(QString::fromStdString(project_.presenter()->getProjectName()));
    dialog.setProjectDescription(QString::fromStdString(project_.presenter()->getProjectDescription()));
    
    if (dialog.exec() == QDialog::Accepted) {
        // Update project with new values
        QString new_name = dialog.projectName();
        QString new_description = dialog.projectDescription();
        
        if (new_name.isEmpty()) {
            QMessageBox::warning(this, "Invalid Input", "Project name cannot be empty.");
            return;
        }
        
        // Update project using presenter
        project_.presenter()->setProjectName(new_name.toStdString());
        project_.presenter()->setProjectDescription(new_description.toStdString());
        
        ORC_LOG_INFO("Project properties updated: name='{}', description='{}'", 
                     new_name.toStdString(), new_description.toStdString());
        
        // Update UI to reflect changes
        updateUIState();
        
        statusBar()->showMessage("Project properties updated", 3000);
    }
}

void MainWindow::onQuickProject()
{
    // Open file dialog to select TBC/TBCC/TBCY file
    QString filename = QFileDialog::getOpenFileName(
        this,
        "Quick Project - Select Video File",
        getLastSourceDirectory(),
        "Video Files (*.tbc *.tbcc *.tbcy);;TBC Files (*.tbc);;TBCC Files (*.tbcc);;TBCY Files (*.tbcy);;All Files (*)"
    );
    
    if (filename.isEmpty()) {
        ORC_LOG_DEBUG("Quick project creation cancelled");
        return;
    }
    
    // Delegate to quickProject() to do the actual work
    quickProject(filename);
}


void MainWindow::closeAllDialogs()
{
    // Close main dialogs (these are persistent, not deleted on close)
    if (preview_dialog_) {
        preview_dialog_->closeChildDialogs();  // Close child dialogs like line scope
        if (preview_dialog_->isVisible()) {
            preview_dialog_->hide();
        }
    }
    if (vbi_dialog_ && vbi_dialog_->isVisible()) {
        vbi_dialog_->hide();
    }
    if (hints_dialog_ && hints_dialog_->isVisible()) {
        hints_dialog_->hide();
    }
    if (ntsc_observer_dialog_ && ntsc_observer_dialog_->isVisible()) {
        ntsc_observer_dialog_->hide();
    }
    if (quality_metrics_dialog_ && quality_metrics_dialog_->isVisible()) {
        quality_metrics_dialog_->hide();
    }
    
    // Close and delete all per-node analysis dialogs
    // These are set to WA_DeleteOnClose, so closing them will trigger deletion
    for (auto& pair : dropout_analysis_dialogs_) {
        if (pair.second && pair.second->isVisible()) {
            pair.second->close();
        }
    }
    dropout_analysis_dialogs_.clear();
    
    for (auto& pair : snr_analysis_dialogs_) {
        if (pair.second && pair.second->isVisible()) {
            pair.second->close();
        }
    }
    snr_analysis_dialogs_.clear();
    
    for (auto& pair : burst_level_analysis_dialogs_) {
        if (pair.second && pair.second->isVisible()) {
            pair.second->close();
        }
    }
    burst_level_analysis_dialogs_.clear();
    
    for (auto& pair : vectorscope_dialogs_) {
        if (pair.second && pair.second->isVisible()) {
            pair.second->close();
        }
    }
    vectorscope_dialogs_.clear();
    
    // Close all progress dialogs
    if (trigger_progress_dialog_) {
        trigger_progress_dialog_->close();
        delete trigger_progress_dialog_;
        trigger_progress_dialog_ = nullptr;
    }
    
    for (auto& pair : dropout_progress_dialogs_) {
        if (pair.second) {
            pair.second->close();
            delete pair.second;
        }
    }
    dropout_progress_dialogs_.clear();
    
    for (auto& pair : snr_progress_dialogs_) {
        if (pair.second) {
            pair.second->close();
            delete pair.second;
        }
    }
    snr_progress_dialogs_.clear();
    
    for (auto& pair : burst_level_progress_dialogs_) {
        if (pair.second) {
            pair.second->close();
            delete pair.second;
        }
    }
    burst_level_progress_dialogs_.clear();
}

void MainWindow::createAndShowAnalysisDialog(const orc::NodeID& node_id, const std::string& stage_name)
{
    // Get node label
    QString node_label = QString::fromStdString(node_id.to_string());
    const auto nodes = project_.presenter()->getNodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
        [&node_id](const orc::presenters::NodeInfo& n) { return n.node_id == node_id; });
    if (node_it != nodes.end()) {
        if (!node_it->label.empty()) {
            node_label = QString::fromStdString(node_it->label);
        } else if (!node_it->stage_name.empty()) {
            node_label = QString::fromStdString(node_it->stage_name);
        }
    }
    
    if (stage_name == "burst_level_analysis_sink") {
        BurstLevelAnalysisDialog* dialog = nullptr;
        auto it = burst_level_analysis_dialogs_.find(node_id);
        if (it == burst_level_analysis_dialogs_.end()) {
            dialog = new BurstLevelAnalysisDialog(this);
            dialog->setWindowTitle(QString("Burst Level Analysis - %1").arg(node_label));
            dialog->setAttribute(Qt::WA_DeleteOnClose, true);
            connect(dialog, &QObject::destroyed, [this, node_id]() {
                burst_level_analysis_dialogs_.erase(node_id);
                burst_level_progress_dialogs_.erase(node_id);
            });
            burst_level_analysis_dialogs_[node_id] = dialog;
        } else {
            dialog = it->second;
        }
        
        auto& prog_dialog = burst_level_progress_dialogs_[node_id];
        if (prog_dialog) {
            delete prog_dialog;
        }
        prog_dialog = new QProgressDialog("Loading burst level analysis data...", QString(), 0, 100, this);
        prog_dialog->setWindowTitle(dialog->windowTitle());
        prog_dialog->setWindowModality(Qt::ApplicationModal);
        prog_dialog->setMinimumDuration(0);
        prog_dialog->setCancelButton(nullptr);
        prog_dialog->setValue(0);
        prog_dialog->show();
        prog_dialog->raise();
        prog_dialog->activateWindow();
        
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    }
    else if (stage_name == "dropout_analysis_sink") {
        DropoutAnalysisDialog* dialog = nullptr;
        auto it = dropout_analysis_dialogs_.find(node_id);
        if (it == dropout_analysis_dialogs_.end()) {
            dialog = new DropoutAnalysisDialog(this);
            dialog->setWindowTitle(QString("Dropout Analysis - %1").arg(node_label));
            dialog->setAttribute(Qt::WA_DeleteOnClose, true);
            connect(dialog, &QObject::destroyed, [this, node_id]() {
                dropout_analysis_dialogs_.erase(node_id);
                dropout_progress_dialogs_.erase(node_id);
            });
            dropout_analysis_dialogs_[node_id] = dialog;
        } else {
            dialog = it->second;
        }
        
        auto& prog_dialog = dropout_progress_dialogs_[node_id];
        if (prog_dialog) {
            delete prog_dialog;
        }
        prog_dialog = new QProgressDialog("Loading dropout analysis data...", QString(), 0, 100, this);
        prog_dialog->setWindowTitle(dialog->windowTitle());
        prog_dialog->setWindowModality(Qt::ApplicationModal);
        prog_dialog->setMinimumDuration(0);
        prog_dialog->setCancelButton(nullptr);
        prog_dialog->setValue(0);
        prog_dialog->show();
        prog_dialog->raise();
        prog_dialog->activateWindow();
        
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    }
    else if (stage_name == "snr_analysis_sink") {
        SNRAnalysisDialog* dialog = nullptr;
        auto it = snr_analysis_dialogs_.find(node_id);
        if (it == snr_analysis_dialogs_.end()) {
            dialog = new SNRAnalysisDialog(this);
            dialog->setWindowTitle(QString("SNR Analysis - %1").arg(node_label));
            dialog->setAttribute(Qt::WA_DeleteOnClose, true);
            connect(dialog, &SNRAnalysisDialog::modeChanged, [this, node_id](orc::SNRAnalysisMode mode) {
                pending_snr_requests_.clear();
                uint64_t request_id = render_coordinator_->requestSNRData(node_id, mode);
                pending_snr_requests_[request_id] = node_id;
            });
            connect(dialog, &QObject::destroyed, [this, node_id]() {
                snr_analysis_dialogs_.erase(node_id);
                snr_progress_dialogs_.erase(node_id);
            });
            snr_analysis_dialogs_[node_id] = dialog;
        } else {
            dialog = it->second;
        }
        
        auto& prog_dialog = snr_progress_dialogs_[node_id];
        if (prog_dialog) {
            delete prog_dialog;
        }
        prog_dialog = new QProgressDialog("Loading SNR analysis data...", QString(), 0, 100, this);
        prog_dialog->setWindowTitle(dialog->windowTitle());
        prog_dialog->setWindowModality(Qt::ApplicationModal);
        prog_dialog->setMinimumDuration(0);
        prog_dialog->setCancelButton(nullptr);
        prog_dialog->setValue(0);
        prog_dialog->show();
        prog_dialog->raise();
        prog_dialog->activateWindow();
        
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    }
}

void MainWindow::newProject(orc::VideoSystem video_format, orc::SourceType source_format)
{
    // Check for unsaved changes before creating new project
    if (!checkUnsavedChanges()) {
        return;
    }
    
    // Show dialog to choose project type if not specified
    if (video_format == orc::VideoSystem::Unknown || source_format == orc::SourceType::Unknown) {
        bool ok;
        
        // Create dialog with project type options
        QStringList items;
        items << "NTSC Composite" << "NTSC YC" << "PAL Composite" << "PAL YC";
        
        QString item = QInputDialog::getItem(this, tr("New Project"),
                                            tr("Select project type:"), items, 0, false, &ok);
        if (!ok || item.isEmpty()) {
            return;  // User cancelled
        }
        
        // Parse selection
        if (item == "NTSC Composite") {
            video_format = orc::VideoSystem::NTSC;
            source_format = orc::SourceType::Composite;
        } else if (item == "NTSC YC") {
            video_format = orc::VideoSystem::NTSC;
            source_format = orc::SourceType::YC;
        } else if (item == "PAL Composite") {
            video_format = orc::VideoSystem::PAL;
            source_format = orc::SourceType::Composite;
        } else if (item == "PAL YC") {
            video_format = orc::VideoSystem::PAL;
            source_format = orc::SourceType::YC;
        }
    }
    
    ORC_LOG_INFO("Creating new project");
    
    // Close all dialogs before clearing project
    closeAllDialogs();
    
    // Clear existing project state
    project_.clear();
    preview_dialog_->previewWidget()->clearImage();
    preview_dialog_->previewSlider()->setEnabled(false);
    preview_dialog_->previewSlider()->setValue(0);
    
    // Create project with default "Untitled" name
    QString project_name = "Untitled";
    
    QString error;
    if (!project_.newEmptyProject(project_name, 
                                  toPresenterVideoFormat(video_format), 
                                  toPresenterSourceType(source_format), 
                                  &error)) {
        ORC_LOG_ERROR("Failed to create project: {}", error.toStdString());
        QMessageBox::critical(this, "Error", error);
        return;
    }
    
    // Update render coordinator with new project pointer (presenter was recreated)
    render_coordinator_->setProject(project_.presenter()->getCoreProjectHandle());
    
    // Recreate DAG model/scene since the presenter has changed
    if (dag_model_) {
        delete dag_scene_;
        delete dag_model_;
        dag_model_ = new OrcGraphModel(*project_.presenter(), dag_view_);
        dag_scene_ = new OrcGraphicsScene(*dag_model_, dag_view_);
        dag_view_->setScene(dag_scene_);
        
        // Reconnect signals
        connect(dag_model_, &QtNodes::AbstractGraphModel::connectionCreated,
                this, &MainWindow::onDAGModified);
        connect(dag_model_, &QtNodes::AbstractGraphModel::connectionDeleted,
                this, &MainWindow::onDAGModified);
        connect(dag_model_, &QtNodes::AbstractGraphModel::nodeCreated,
                this, &MainWindow::onDAGModified);
        connect(dag_model_, &QtNodes::AbstractGraphModel::nodeDeleted,
                this, &MainWindow::onDAGModified);
        connect(dag_scene_, &OrcGraphicsScene::nodeSelected,
                this, &MainWindow::onQtNodeSelected);
        connect(dag_scene_, &OrcGraphicsScene::editParametersRequested,
                this, &MainWindow::onEditParameters);
        connect(dag_scene_, &OrcGraphicsScene::triggerStageRequested,
                this, &MainWindow::onTriggerStage);
        connect(dag_scene_, &OrcGraphicsScene::inspectStageRequested,
                this, &MainWindow::onInspectStage);
        connect(dag_scene_, &OrcGraphicsScene::runAnalysisRequested,
                this, &MainWindow::runAnalysisForNode);
    }
    
    // Don't set a project path - leave it empty so user must use "Save As"
    // Project is marked as modified by create_empty_project()
    
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
    
    // Close all dialogs before clearing project
    closeAllDialogs();
    
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
    
    // Update render coordinator with new project pointer (presenter was recreated)
    render_coordinator_->setProject(project_.presenter()->getCoreProjectHandle());
    
    // Recreate DAG model/scene since the presenter has changed
    if (dag_model_) {
        delete dag_scene_;
        delete dag_model_;
        dag_model_ = new OrcGraphModel(*project_.presenter(), dag_view_);
        dag_scene_ = new OrcGraphicsScene(*dag_model_, dag_view_);
        dag_view_->setScene(dag_scene_);
        
        // Reconnect signals
        connect(dag_model_, &QtNodes::AbstractGraphModel::connectionCreated,
                this, &MainWindow::onDAGModified);
        connect(dag_model_, &QtNodes::AbstractGraphModel::connectionDeleted,
                this, &MainWindow::onDAGModified);
        connect(dag_model_, &QtNodes::AbstractGraphModel::nodeCreated,
                this, &MainWindow::onDAGModified);
        connect(dag_model_, &QtNodes::AbstractGraphModel::nodeDeleted,
                this, &MainWindow::onDAGModified);
        connect(dag_scene_, &OrcGraphicsScene::nodeSelected,
                this, &MainWindow::onQtNodeSelected);
        connect(dag_scene_, &OrcGraphicsScene::editParametersRequested,
                this, &MainWindow::onEditParameters);
        connect(dag_scene_, &OrcGraphicsScene::triggerStageRequested,
                this, &MainWindow::onTriggerStage);
        connect(dag_scene_, &OrcGraphicsScene::inspectStageRequested,
                this, &MainWindow::onInspectStage);
        connect(dag_scene_, &OrcGraphicsScene::runAnalysisRequested,
                this, &MainWindow::runAnalysisForNode);
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
    
    // Automatically select the source stage with the lowest node ID
    selectLowestSourceStage();
    
    statusBar()->showMessage(QString("Opened project: %1").arg(project_.projectName()));
}

void MainWindow::quickProject(const QString& filename)
{
    // Check for unsaved changes before creating new project
    if (!checkUnsavedChanges()) {
        return;
    }
    
    // Validate file exists
    if (!QFileInfo::exists(filename)) {
        QMessageBox::critical(this, "Error", 
            QString("File not found: %1").arg(filename));
        return;
    }
    
    QFileInfo file_info(filename);
    QString base_path = file_info.absolutePath() + "/" + file_info.completeBaseName();
    QString ext = file_info.suffix().toLower();
    
    // Determine source type from file extension
    orc::SourceType source_type = orc::SourceType::Unknown;
    std::string primary_file = filename.toStdString();
    std::string secondary_file;
    
    if (ext == "tbc") {
        source_type = orc::SourceType::Composite;
        primary_file = filename.toStdString();
    } else if (ext == "tbcc") {
        source_type = orc::SourceType::YC;
        primary_file = filename.toStdString();
        // Look for corresponding .tbcy file
        QString tbcy_path = base_path + ".tbcy";
        if (!QFileInfo::exists(tbcy_path)) {
            QMessageBox::warning(this, "Missing File", 
                QString("Could not find corresponding Y (luma) file: %1").arg(tbcy_path));
            return;
        }
        secondary_file = tbcy_path.toStdString();
    } else if (ext == "tbcy") {
        source_type = orc::SourceType::YC;
        primary_file = filename.toStdString();
        // Look for corresponding .tbcc file
        QString tbcc_path = base_path + ".tbcc";
        if (!QFileInfo::exists(tbcc_path)) {
            QMessageBox::warning(this, "Missing File", 
                QString("Could not find corresponding C (chroma) file: %1").arg(tbcc_path));
            return;
        }
        secondary_file = tbcc_path.toStdString();
    } else {
        QMessageBox::warning(this, "Invalid File", 
            QString("Please provide a .tbc, .tbcc, or .tbcy file. Got: %1").arg(ext));
        return;
    }
    
    // Determine metadata file
    QString db_path = base_path + ".tbc.db";
    if (!QFileInfo::exists(db_path)) {
        QMessageBox::warning(this, "Missing Metadata File", 
            QString("Could not find metadata file: %1").arg(db_path));
        return;
    }
    
    // Read metadata to determine video format (NTSC or PAL)
    ORC_LOG_INFO("Reading metadata from: {}", db_path.toStdString());
    
    auto video_params_opt = orc::presenters::ProjectPresenter::readVideoParameters(db_path.toStdString());
    if (!video_params_opt) {
        QMessageBox::critical(this, "Error", 
            QString("Failed to read video parameters from metadata file: %1").arg(db_path));
        return;
    }
    
    orc::VideoSystem video_format = video_params_opt->system;
    
    ORC_LOG_INFO("Detected format: {}, Source type: {}", 
                 (video_format == orc::VideoSystem::NTSC ? "NTSC" : "PAL"),
                 (source_type == orc::SourceType::Composite ? "Composite" : "YC"));
    
    // Close all dialogs before clearing project
    closeAllDialogs();
    
    // Clear existing project state
    project_.clear();
    preview_dialog_->previewWidget()->clearImage();
    preview_dialog_->previewSlider()->setEnabled(false);
    preview_dialog_->previewSlider()->setValue(0);
    
    // Determine stage names based on format and source type
    std::string source_stage_name;
    if (video_format == orc::VideoSystem::NTSC) {
        source_stage_name = (source_type == orc::SourceType::Composite) ? "NTSC_Comp_Source" : "NTSC_YC_Source";
    } else {
        source_stage_name = (source_type == orc::SourceType::Composite) ? "PAL_Comp_Source" : "PAL_YC_Source";
    }
    
    // Create empty project
    QString project_name = file_info.completeBaseName();
    QString error;
    
    if (!project_.newEmptyProject(project_name, 
                                  toPresenterVideoFormat(video_format), 
                                  toPresenterSourceType(source_type), 
                                  &error)) {
        ORC_LOG_ERROR("Failed to create project: {}", error.toStdString());
        QMessageBox::critical(this, "Error", error);
        return;
    }
    
    // Update render coordinator with new project pointer (presenter was recreated)
    render_coordinator_->setProject(project_.presenter()->getCoreProjectHandle());
    
    // Add source stage
    ORC_LOG_INFO("Adding source stage: {}", source_stage_name);
    orc::NodeID source_node_id;
    try {
        // Use same spacing as DAG alignment function
        const double grid_offset_x = 50.0;
        const double grid_offset_y = 50.0;
        source_node_id = project_.presenter()->addNode(source_stage_name, grid_offset_x, grid_offset_y);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error", 
            QString("Failed to add source stage: %1").arg(e.what()));
        return;
    }
    
    // Add FFmpeg video sink stage
    ORC_LOG_INFO("Adding FFmpeg video sink stage");
    orc::NodeID sink_node_id;
    try {
        // Use same spacing as DAG alignment function
        const double grid_spacing_x = 225.0;
        const double grid_offset_x = 50.0;
        const double grid_offset_y = 50.0;
        sink_node_id = project_.presenter()->addNode("ffmpeg_video_sink", grid_offset_x + grid_spacing_x, grid_offset_y);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error", 
            QString("Failed to add sink stage: %1").arg(e.what()));
        return;
    }
    
    // Set parameters on source stage based on source type
    std::map<std::string, orc::ParameterValue> source_params;
    
    if (source_type == orc::SourceType::Composite) {
        // Composite source
        source_params["input_path"] = primary_file;
        source_params["db_path"] = db_path.toStdString();
        
        // Check for optional pcm and efm files
        QString pcm_path = base_path + ".pcm";
        if (QFileInfo::exists(pcm_path)) {
            source_params["pcm_path"] = pcm_path.toStdString();
        }
        
        QString efm_path = base_path + ".efm";
        if (QFileInfo::exists(efm_path)) {
            source_params["efm_path"] = efm_path.toStdString();
        }
    } else {
        // YC source
        if (ext == "tbcy") {
            source_params["y_path"] = primary_file;
            source_params["c_path"] = secondary_file;
        } else {
            source_params["y_path"] = secondary_file;
            source_params["c_path"] = primary_file;
        }
        source_params["db_path"] = db_path.toStdString();
        
        // Check for optional pcm and efm files
        QString pcm_path = base_path + ".pcm";
        if (QFileInfo::exists(pcm_path)) {
            source_params["pcm_path"] = pcm_path.toStdString();
        }
        
        QString efm_path = base_path + ".efm";
        if (QFileInfo::exists(efm_path)) {
            source_params["efm_path"] = efm_path.toStdString();
        }
    }
    
    // Set parameters on the source stage using presenter
    try {
        project_.presenter()->setNodeParameters(source_node_id, source_params);
        ORC_LOG_INFO("Source stage parameters set successfully");
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error", 
            QString("Failed to set parameters on source stage: %1").arg(e.what()));
        return;
    }
    
    // Connect source to sink
    ORC_LOG_INFO("Connecting source stage to sink stage");
    try {
        project_.presenter()->addEdge(source_node_id, sink_node_id);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error", 
            QString("Failed to connect stages: %1").arg(e.what()));
        return;
    }
    
    // Rebuild DAG from the newly created project structure
    project_.rebuildDAG();
    
    // Recreate DAG model/scene since the presenter has changed
    if (dag_model_) {
        delete dag_scene_;
        delete dag_model_;
        dag_model_ = new OrcGraphModel(*project_.presenter(), dag_view_);
        dag_scene_ = new OrcGraphicsScene(*dag_model_, dag_view_);
        dag_view_->setScene(dag_scene_);
        
        // Reconnect signals
        connect(dag_model_, &QtNodes::AbstractGraphModel::connectionCreated,
                this, &MainWindow::onDAGModified);
        connect(dag_model_, &QtNodes::AbstractGraphModel::connectionDeleted,
                this, &MainWindow::onDAGModified);
        connect(dag_model_, &QtNodes::AbstractGraphModel::nodeCreated,
                this, &MainWindow::onDAGModified);
        connect(dag_model_, &QtNodes::AbstractGraphModel::nodeDeleted,
                this, &MainWindow::onDAGModified);
        connect(dag_scene_, &OrcGraphicsScene::nodeSelected,
                this, &MainWindow::onQtNodeSelected);
        connect(dag_scene_, &OrcGraphicsScene::editParametersRequested,
                this, &MainWindow::onEditParameters);
        connect(dag_scene_, &OrcGraphicsScene::triggerStageRequested,
                this, &MainWindow::onTriggerStage);
        connect(dag_scene_, &OrcGraphicsScene::inspectStageRequested,
                this, &MainWindow::onInspectStage);
        connect(dag_scene_, &OrcGraphicsScene::runAnalysisRequested,
                this, &MainWindow::runAnalysisForNode);
    }
    
    // Don't set a project path - leave it empty so user must use "Save As"
    // Project is marked as modified by create_empty_project() and subsequent operations
    
    ORC_LOG_INFO("Quick project created successfully from: {}", filename.toStdString());
    
    // Remember this source directory
    setLastSourceDirectory(QFileInfo(filename).absolutePath());
    
    // Update UI - preview renderer and DAG display
    updateUIState();
    updatePreviewRenderer();
    loadProjectDAG();
    
    // Automatically select the source stage with the lowest node ID
    selectLowestSourceStage();
    
    statusBar()->showMessage("Quick project created successfully", 5000);
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
    bool has_saved_path = !project_.projectPath().isEmpty();
    
    // Enable/disable actions based on project state
    if (save_project_action_) {
        // Save is only enabled if project is modified AND has a saved path
        // (i.e., the first Save As must be done before using Save)
        save_project_action_->setEnabled(has_project && project_.isModified() && has_saved_path);
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
        // For keyboard navigation, update immediately without debouncing
        preview_dialog_->previewSlider()->blockSignals(true);
        preview_dialog_->previewSlider()->setValue(new_index);
        preview_dialog_->previewSlider()->blockSignals(false);
        
        // Skip debounce timer and update immediately
        preview_update_timer_->stop();
        pending_preview_index_ = new_index;
        updateAllPreviewComponents();
        preview_update_pending_ = false;
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
    
    // Convert position between field and frame indices
    uint64_t new_position = current_position;
    
    // Determine if previous and new types are field-based or frame-based
    bool previous_is_field = (previous_type == orc::PreviewOutputType::Field || 
                              previous_type == orc::PreviewOutputType::Luma);
    bool new_is_field = (current_output_type_ == orc::PreviewOutputType::Field || 
                         current_output_type_ == orc::PreviewOutputType::Luma);
    
    // Get first_field_offset - this is the same for all frame-based outputs (determined by field 0 parity)
    // We can get it from any frame-based output
    uint64_t first_field_offset = 0;
    for (const auto& output : available_outputs_) {
        if (output.type == orc::PreviewOutputType::Frame ||
            output.type == orc::PreviewOutputType::Frame_Reversed ||
            output.type == orc::PreviewOutputType::Split) {
            first_field_offset = output.first_field_offset;
            ORC_LOG_DEBUG("Found first_field_offset: {}", first_field_offset);
            break;
        }
    }
    
    if (previous_is_field && !new_is_field) {
        // Converting from field to frame: select frame containing current field
        // Frame F contains fields: F*2 + offset and F*2 + offset + 1
        // So field N is in frame: (N - offset) / 2
        if (current_position >= first_field_offset) {
            new_position = (current_position - first_field_offset) / 2;
        } else {
            new_position = 0;
        }
        ORC_LOG_DEBUG("Field->Frame conversion: field {} -> frame {} (offset: {})", 
                     current_position, new_position, first_field_offset);
    } else if (!previous_is_field && new_is_field) {
        // Converting from frame to field: select first field of current frame
        // Frame F maps to first field at: F*2 + offset
        new_position = (current_position * 2) + first_field_offset;
        ORC_LOG_DEBUG("Frame->Field conversion: frame {} -> field {} (offset: {})", 
                     current_position, new_position, first_field_offset);
    }
    // Otherwise, both are same type (field->field or frame->frame), keep position as-is
    
    // Update viewer controls (slider range, step, labels) without triggering a render yet
    bool old_signal_state = preview_dialog_->previewSlider()->blockSignals(true);
    refreshViewerControls(true /* skip_preview */);
    
    // Set the calculated position (after refreshViewerControls updates the range)
    if (new_position >= 0 && new_position <= static_cast<uint64_t>(preview_dialog_->previewSlider()->maximum())) {
        preview_dialog_->previewSlider()->setValue(new_position);
    }
    
    // Restore signal state and trigger preview update at the correct position
    preview_dialog_->previewSlider()->blockSignals(old_signal_state);
    updateAllPreviewComponents();
}

void MainWindow::onAspectRatioModeChanged(int index)
{
    // Handle aspect ratio entirely in GUI: set preview widget correction
    static std::vector<orc::AspectRatioModeInfo> available_modes = {
        {orc::AspectRatioMode::SAR_1_1, "1:1 (Square)", 1.0},
        {orc::AspectRatioMode::DAR_4_3, "4:3 (Display)", 0.7}
    };
    if (index < 0 || index >= static_cast<int>(available_modes.size())) {
        return;
    }

    current_aspect_ratio_mode_ = available_modes[index].mode;

    // Determine correction factor: for 1:1 use 1.0; for 4:3 compute from active area
    auto computeAspectCorrection = [this]() -> double {
        double correction = 1.0;
        // Fetch hints (active lines) and video parameters for current node
        auto hints = hints_presenter_->getHintsForField(current_view_node_id_, orc::FieldID(0));
        if (hints.video_params.has_value() && hints.active_line.has_value() && hints.active_line->is_valid()) {
            const auto& vp = hints.video_params.value();
            const auto& al = hints.active_line.value();
            int active_width = vp.active_video_end - vp.active_video_start;
            int active_frame_height = al.last_active_frame_line - al.first_active_frame_line + 1;
            if (active_width > 0 && active_frame_height > 0) {
                double target_ratio = 1.0;
                switch (current_output_type_) {
                    case orc::PreviewOutputType::Field:
                    case orc::PreviewOutputType::Luma:
                        // Single field desired ratio ~ 4:1.5
                        target_ratio = 4.0 / 1.5;  // ~2.6667
                        // Field shows half the frame height
                        correction = (target_ratio * (active_frame_height / 2.0)) / static_cast<double>(active_width);
                        break;
                    case orc::PreviewOutputType::Frame:
                    case orc::PreviewOutputType::Frame_Reversed:
                    case orc::PreviewOutputType::Split:
                        target_ratio = 4.0 / 3.0;  // ~1.3333
                        correction = (target_ratio * static_cast<double>(active_frame_height)) / static_cast<double>(active_width);
                        break;
                    default:
                        correction = 1.0;
                        break;
                }
            }
        } else {
            // Fallback: use stage-provided DAR correction if available
            for (const auto& output : available_outputs_) {
                if (output.type == current_output_type_ && output.option_id == current_option_id_) {
                    correction = output.dar_aspect_correction;
                    break;
                }
            }
        }
        return correction;
    };

    double correction = (current_aspect_ratio_mode_ == orc::AspectRatioMode::DAR_4_3)
        ? computeAspectCorrection()
        : 1.0;

    if (preview_dialog_ && preview_dialog_->previewWidget()) {
        preview_dialog_->previewWidget()->setAspectCorrection(correction);
    }

    // No core-side scaling; re-render image to ensure redraw uses new scaling
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
    
    // Update NTSC observer menu item availability (only for NTSC)
    // NTSC observers (FM code, white flag) are NTSC-specific
    auto video_format_presenter = project_.presenter()->getVideoFormat();
    bool is_ntsc = (video_format_presenter == orc::presenters::VideoFormat::NTSC);
    preview_dialog_->ntscObserverAction()->setEnabled(is_ntsc);
    
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
    
    const auto nodes = project_.presenter()->getNodes();
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
    const auto nodes = project_.presenter()->getNodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
        [&node_id](const orc::presenters::NodeInfo& n) { return n.node_id == node_id; });
    
    if (node_it == nodes.end()) {
        QMessageBox::warning(this, "Edit Parameters",
            QString("Stage '%1' not found").arg(QString::fromStdString(node_id.to_string())));
        return;
    }
    
    std::string stage_name = node_it->stage_name;
    
    // Check if stage exists in registry
    if (!orc::presenters::ProjectPresenter::hasStage(stage_name)) {
        QMessageBox::warning(this, "Edit Parameters",
            QString("Unknown stage type '%1'").arg(QString::fromStdString(stage_name)));
        return;
    }
    
    // Get parameter descriptors using presenter (handles video format/source type context internally)
    auto param_descriptors = project_.presenter()->getStageParameters(stage_name);
    
    if (param_descriptors.empty()) {
        QMessageBox::information(this, "Edit Parameters",
            QString("Stage '%1' does not have configurable parameters")
                .arg(QString::fromStdString(stage_name)));
        return;
    }
    
    // Get current parameter values from the node
    auto current_values = project_.presenter()->getNodeParameters(node_id);
    
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
            project_.presenter()->setNodeParameters(node_id, new_values);
            
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
                project_.presenter()->setNodeParameters(node_id, empty_params);
                
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
        // Store the node being triggered so we can request data after completion
        pending_trigger_node_id_ = node_id;
        
        // Create progress dialog
        trigger_progress_dialog_ = new QProgressDialog("Starting trigger...", "Cancel", 0, 100, this);
        trigger_progress_dialog_->setWindowTitle("Processing");
        trigger_progress_dialog_->setWindowModality(Qt::ApplicationModal);
        trigger_progress_dialog_->setMinimumDuration(0);
        trigger_progress_dialog_->setValue(0);
        trigger_progress_dialog_->show();
        trigger_progress_dialog_->raise();
        trigger_progress_dialog_->activateWindow();
        
        // Connect cancel button
        connect(trigger_progress_dialog_, &QProgressDialog::canceled,
                this, [this]() {
            ORC_LOG_DEBUG("User canceled trigger");
            render_coordinator_->cancelTrigger();
            // Clean up the dialog and request state
            pending_trigger_request_id_ = 0;
            pending_trigger_node_id_ = orc::NodeID();  // Reset to invalid ID
            if (trigger_progress_dialog_) {
                trigger_progress_dialog_->blockSignals(true);
                trigger_progress_dialog_->deleteLater();
                // QPointer will be nulled when dialog is deleted
            }
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
    
    // If line scope is visible, refresh it for the new stage
    refreshLineScopeForCurrentStage();
    
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
    const auto nodes = project_.presenter()->getNodes();
    const auto edges = project_.presenter()->getEdges();
    
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
        in_degree[edge.target_node]++;
        forward_edges[edge.source_node].push_back(edge.target_node);
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
        reverse_edges[edge.target_node].push_back(edge.source_node);
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
            project_.presenter()->setNodePosition(node_id, x, y);
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
    
    // For YC sources, combine option_id with signal selection (Y/C/Y+C)
    std::string effective_option_id = current_option_id_;
    if (preview_dialog_->signalCombo()->isVisible()) {
        int signal_index = preview_dialog_->signalCombo()->currentIndex();
        std::string suffix;
        switch (signal_index) {
            case 0: suffix = "_yc"; break;  // Y+C composite
            case 1: suffix = "_y"; break;   // Luma only
            case 2: suffix = "_c"; break;   // Chroma only
            default: suffix = "_yc"; break;
        }
        effective_option_id = current_option_id_ + suffix;
        ORC_LOG_DEBUG("  YC source: base option '{}' + signal suffix '{}' = '{}'",
                      current_option_id_, suffix, effective_option_id);
    }
    
    // Request preview from coordinator (async, thread-safe)
    pending_preview_request_id_ = render_coordinator_->requestPreview(
        current_view_node_id_,
        current_output_type_,
        current_index,
        effective_option_id
    );
    
    // Mark that a render is now in-flight (will be cleared when onPreviewReady is called)
    preview_render_in_flight_ = true;
    
    // Initialize the cache with the current index we're rendering
    latest_requested_preview_index_ = current_index;
}

void MainWindow::updateVectorscope(const orc::PreviewRenderResult& result)
{
    auto it = vectorscope_dialogs_.find(result.node_id);
    if (it == vectorscope_dialogs_.end()) {
        ORC_LOG_DEBUG("updateVectorscope: no dialog for node '{}'", result.node_id.to_string());
        return;
    }
    auto* dialog = it->second;
    if (!dialog) {
        ORC_LOG_DEBUG("updateVectorscope: dialog pointer null for node '{}'", result.node_id.to_string());
        return;
    }
    if (!dialog->isVisible()) {
        ORC_LOG_DEBUG("updateVectorscope: dialog hidden for node '{}' — skipping", result.node_id.to_string());
        return;
    }
    
    // Update vectorscope if data is available
    if (result.vectorscope_data) {
        const size_t sample_count = result.vectorscope_data->samples.size();
        ORC_LOG_DEBUG("updateVectorscope: delivering {} samples to dialog for node '{}' (output_type={}, index={})",
                      sample_count, result.node_id.to_string(), static_cast<int>(result.output_type), result.output_index);
        dialog->updateVectorscope(*result.vectorscope_data);
    } else {
        ORC_LOG_DEBUG("updateVectorscope: no vectorscope data for node '{}' (output_type={}, index={})",
                      result.node_id.to_string(), static_cast<int>(result.output_type), result.output_index);
    }
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

    // Apply correction immediately for current mode using active area
    auto computeAspectCorrection = [this]() -> double {
        double correction = 1.0;
        // Always use the fallback method to avoid concurrent DAG access issues
        for (const auto& output : available_outputs_) {
            if (output.type == current_output_type_ && output.option_id == current_option_id_) {
                correction = output.dar_aspect_correction;
                break;
            }
        }
        return correction;
    };

    double correction = (current_aspect_ratio_mode_ == orc::AspectRatioMode::DAR_4_3)
        ? computeAspectCorrection()
        : 1.0;
    if (preview_dialog_ && preview_dialog_->previewWidget()) {
        preview_dialog_->previewWidget()->setAspectCorrection(correction);
    }
}

void MainWindow::refreshViewerControls(bool skip_preview)
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

    // Apply aspect correction based on selected mode
    // Use the fallback method to avoid concurrent DAG access from GUI and render threads
    double correction = 1.0;
    if (current_aspect_ratio_mode_ == orc::AspectRatioMode::DAR_4_3) {
        for (const auto& output : available_outputs_) {
            if (output.type == current_output_type_ && output.option_id == current_option_id_) {
                correction = output.dar_aspect_correction;
                break;
            }
        }
    }
    if (preview_dialog_ && preview_dialog_->previewWidget()) {
        preview_dialog_->previewWidget()->setAspectCorrection(correction);
    }
    
    // Check if current output has separate channels (YC source) and show/hide signal selector
    bool has_separate_channels = false;
    for (const auto& output : available_outputs_) {
        if (output.has_separate_channels) {
            has_separate_channels = true;
            break;
        }
    }
    preview_dialog_->setSignalControlsVisible(has_separate_channels);
    
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
    if (!skip_preview) {
        updateAllPreviewComponents();
    }
}

void MainWindow::updatePreviewRenderer()
{
    ORC_LOG_DEBUG("Updating preview renderer");
    
    // Get the DAG - could be null for empty projects, that's fine
    auto dag = project_.hasSource() ? project_.getDAG() : nullptr;
    
    // Debug: show what we're working with
    if (dag) {
        auto nodes = project_.presenter()->getNodes();
        ORC_LOG_DEBUG("DAG contains {} nodes:", nodes.size());
        for (const auto& node : nodes) {
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
        // Check if current node still exists using presenter
        bool current_exists = project_.presenter()->hasNode(current_view_node_id_);
        
        // If current node was deleted or is placeholder when real nodes exist, switch
        if (!current_exists && current_view_node_id_ != NodeID(-999)) {
            need_to_switch = true;
        } else if (current_view_node_id_ == NodeID(-999) && project_.presenter()->getFirstNode().is_valid()) {
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
        if (current_view_node_id_.is_valid() == false) {
            // Pick first node as temporary fallback
            NodeID first_node = project_.presenter()->getFirstNode();
            if (first_node.is_valid()) {
                current_view_node_id_ = first_node;
                pending_outputs_request_id_ = render_coordinator_->requestAvailableOutputs(current_view_node_id_);
            }
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

void MainWindow::selectLowestSourceStage()
{
    // Find source stage with the lowest node ID
    const auto nodes = project_.presenter()->getNodes();
    const auto all_stages = orc::presenters::ProjectPresenter::getAllStages();
    orc::NodeID lowest_source_id;
    bool found = false;
    
    for (const auto& node : nodes) {
        // Check if this stage is a source by looking up its info
        auto stage_it = std::find_if(all_stages.begin(), all_stages.end(),
            [&node](const orc::presenters::StageInfo& s) { return s.name == node.stage_name; });
        
        if (stage_it != all_stages.end() && stage_it->is_source) {
            if (!found || node.node_id < lowest_source_id) {
                lowest_source_id = node.node_id;
                found = true;
            }
        }
    }
    
    if (found) {
        ORC_LOG_DEBUG("Auto-selecting source stage: {}", lowest_source_id.to_string());
        onNodeSelectedForView(lowest_source_id);
    } else {
        ORC_LOG_DEBUG("No source stages found to auto-select");
    }
}

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

QString MainWindow::getLastSourceDirectory() const
{
    QSettings settings("orc-project", "orc-gui");
    QString dir = settings.value("lastSourceDirectory", QString()).toString();
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        return QDir::homePath();
    }
    return dir;
}

void MainWindow::setLastSourceDirectory(const QString& path)
{
    QSettings settings("orc-project", "orc-gui");
    settings.setValue("lastSourceDirectory", path);
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
    const auto nodes = project_.presenter()->getNodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
        [&node_id](const orc::presenters::NodeInfo& n) { return n.node_id == node_id; });
    
    if (node_it == nodes.end()) {
        ORC_LOG_ERROR("Stage '{}' not found in project", node_id);
        QMessageBox::warning(this, "Inspection Failed",
            QString("Stage '%1' not found.").arg(QString::fromStdString(node_id.to_string())));
        return;
    }
    
    const std::string& stage_name = node_it->stage_name;
    
    // Use presenter to get inspection report (handles DAG vs fresh stage internally)
    try {
        auto inspection_view = project_.presenter()->getNodeInspection(node_id);
        
        if (!inspection_view.has_value()) {
            QMessageBox::information(this, "Stage Inspection",
                QString("Stage '%1' does not support inspection reporting.")
                    .arg(QString::fromStdString(stage_name)));
            return;
        }
        
        // Show inspection dialog with presenter view model
        orc::InspectionDialog dialog(inspection_view.value(), this);
        dialog.exec();
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Failed to inspect stage '{}': {}", stage_name, e.what());
        QMessageBox::warning(this, "Inspection Failed",
            QString("Failed to inspect stage: %1").arg(e.what()));
    }
}

void MainWindow::runAnalysisForNode(const orc::AnalysisToolInfo& tool_info, const orc::NodeID& node_id, const std::string& stage_name)
{
    ORC_LOG_DEBUG("Running analysis '{}' for node '{}'", tool_info.name, node_id.to_string());

    // Special-case: Mask Line Configuration uses a custom rules-based config dialog
    if (tool_info.id == "mask_line_config") {
        ORC_LOG_DEBUG("Opening mask line configuration dialog for node '{}'", node_id.to_string());
        
        // Get current parameters from the node
        auto nodes = project_.presenter()->getNodes();
        auto node_it = std::find_if(nodes.begin(), nodes.end(),
            [&node_id](const orc::presenters::NodeInfo& n) { return n.node_id == node_id; });
        
        if (node_it == nodes.end()) {
            QMessageBox::warning(this, "Error",
                "Could not find node in project.");
            return;
        }
        
        // Create and show the config dialog
        MaskLineConfigDialog dialog(this);
        
        // Load current parameters from presenter
        auto current_params = project_.presenter()->getNodeParameters(node_id);
        dialog.set_parameters(current_params);
        
        // Show dialog and apply if accepted
        if (dialog.exec() == QDialog::Accepted) {
            auto new_params = dialog.get_parameters();
            
            ORC_LOG_DEBUG("Mask line config accepted, applying parameters");
            
            try {
                // Update node parameters using presenter
                project_.presenter()->setNodeParameters(node_id, new_params);
                
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

    // Special-case: FFmpeg Preset Configuration uses a custom preset dialog
    if (tool_info.id == "ffmpeg_preset_config") {
        ORC_LOG_DEBUG("Opening FFmpeg preset configuration dialog for node '{}'", node_id.to_string());
        
        // Get current parameters from the node
        auto nodes = project_.presenter()->getNodes();
        auto node_it = std::find_if(nodes.begin(), nodes.end(),
            [&node_id](const orc::presenters::NodeInfo& n) { return n.node_id == node_id; });
        
        if (node_it == nodes.end()) {
            QMessageBox::warning(this, "Error",
                "Could not find node in project.");
            return;
        }
        
        // Create and show the preset dialog
        FFmpegPresetDialog dialog(this);
        
        // Load current parameters from presenter
        auto current_params = project_.presenter()->getNodeParameters(node_id);
        dialog.set_parameters(current_params);
        
        // Show dialog and apply if accepted
        if (dialog.exec() == QDialog::Accepted) {
            auto new_params = dialog.get_parameters();
            
            ORC_LOG_DEBUG("FFmpeg preset config accepted, applying parameters");
            
            try {
                // Update node parameters using presenter
                project_.presenter()->setNodeParameters(node_id, new_params);
                
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
                    QString("Applied FFmpeg export preset to '%1'")
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
    if (tool_info.id == "vectorscope") {
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
    if (tool_info.id == "dropout_editor") {
        // Get the project
        if (!dag_model_) {
            ORC_LOG_ERROR("No DAG model available for dropout editor");
            return;
        }

        // Get node and edge information from presenter
        const auto nodes = project_.presenter()->getNodes();
        auto node_it = std::find_if(nodes.begin(), nodes.end(),
            [&node_id](const auto& n) { return n.node_id == node_id; });
        
        if (node_it == nodes.end()) {
            ORC_LOG_ERROR("Node '{}' not found in project", node_id.to_string());
            return;
        }

        // Execute DAG to get the VideoFieldRepresentation for the input node
        // Find the input edge to this node
        const auto edges = project_.presenter()->getEdges();
        orc::NodeID input_node_id;
        bool found_input = false;
        for (const auto& edge : edges) {
            if (edge.target_node == node_id) {
                input_node_id = edge.source_node;
                found_input = true;
                break;
            }
        }

        if (!found_input) {
            QMessageBox::warning(this, "Error",
                "Could not find input node for dropout editor. "
                "Ensure the dropout_map stage has a valid video source connected.");
            return;
        }

        // Get DAG and execute to input node
        auto dag = project_.getDAG();
        // Create temporary RenderPresenter for execution
        auto* core_project = project_.presenter()->getCoreProjectHandle();
        if (!core_project) {
            QMessageBox::warning(this, "Error", "Invalid project state.");
            return;
        }
        
        orc::presenters::RenderPresenter render_presenter(core_project);
        render_presenter.setDAG(project_.getDAG());
        
        std::shared_ptr<const orc::VideoFieldRepresentation> source_repr;
        try {
            auto output_void = render_presenter.executeToNode(input_node_id);
            if (output_void) {
                source_repr = std::static_pointer_cast<const orc::VideoFieldRepresentation>(output_void);
            }
        } catch (const std::exception& e) {
            QMessageBox::warning(this, "Error",
                QString("Failed to execute DAG: %1").arg(e.what()));
            return;
        }

        if (!source_repr) {
            QMessageBox::warning(this, "Error",
                "Could not get video field data from input. "
                "Ensure the dropout_map stage has a valid video source connected.");
            return;
        }

        // Open the editor dialog as a non-modal independent window
        auto* dialog = new DropoutEditorDialog(node_id, dropout_presenter_.get(), source_repr, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        
        // Connect to handle the accepted signal
        connect(dialog, &QDialog::accepted, this, [this, node_id, dialog]() {
            // Get the edited dropout map
            auto edited_map = dialog->getDropoutMap();
            
            ORC_LOG_DEBUG("Dropout editor returned map with {} field entries", edited_map.size());
            
            // Save the dropout map via presenter
            if (dropout_presenter_->setDropoutMap(node_id, edited_map)) {
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
            } else {
                QMessageBox::warning(this, "Error", "Failed to save dropout map");
            }
        });
        
        // Show as non-modal window
        dialog->show();
        return;
    }
    
    // Special-case: Dropout Analysis triggers batch processing and shows dialog
    if (tool_info.id == "dropout_analysis") {
        // Get node info from project
        auto nodes = project_.presenter()->getNodes();
        auto node_it = std::find_if(nodes.begin(), nodes.end(),
            [&node_id](const orc::presenters::NodeInfo& n) { return n.node_id == node_id; });
        
        QString node_label = QString::fromStdString(stage_name);
        if (node_it != nodes.end()) {
            node_label = QString::fromStdString(node_it->label.empty() ? node_it->stage_name : node_it->label);
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
        prog_dialog->setWindowModality(Qt::ApplicationModal);
        prog_dialog->setMinimumDuration(0);
        prog_dialog->setCancelButton(nullptr);
        prog_dialog->setValue(0);
        prog_dialog->show();
        prog_dialog->raise();
        prog_dialog->activateWindow();

        // Show the dialog (but it will be empty until data arrives)
        dialog->show();
        dialog->raise();
        dialog->activateWindow();

        // Request dropout data from coordinator (triggers batch processing)
        // Mode is controlled by the sink stage parameter, not the dialog
        uint64_t request_id = render_coordinator_->requestDropoutData(node_id, orc::DropoutAnalysisMode::FULL_FIELD);
        pending_dropout_requests_[request_id] = node_id;
        
        ORC_LOG_DEBUG("Requested dropout analysis data for node '{}', request_id={}",
                      node_id.to_string(), request_id);
        return;
    }
    
    // Special-case: SNR Analysis triggers batch processing and shows dialog
    if (tool_info.id == "snr_analysis") {
        // Get node info from project
        auto nodes = project_.presenter()->getNodes();
        auto node_it = std::find_if(nodes.begin(), nodes.end(),
            [&node_id](const orc::presenters::NodeInfo& n) { return n.node_id == node_id; });
        
        QString node_label = QString::fromStdString(stage_name);
        if (node_it != nodes.end()) {
            node_label = QString::fromStdString(node_it->label.empty() ? node_it->stage_name : node_it->label);
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
                            prog_dialog->setWindowModality(Qt::ApplicationModal);
                            prog_dialog->setMinimumDuration(0);
                            prog_dialog->setCancelButton(nullptr);
                            prog_dialog->setValue(0);
                            prog_dialog->show();
                            prog_dialog->raise();
                            prog_dialog->activateWindow();
                            
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
        prog_dialog->setWindowModality(Qt::ApplicationModal);
        prog_dialog->setMinimumDuration(0);
        prog_dialog->setCancelButton(nullptr);
        prog_dialog->setValue(0);
        prog_dialog->show();
        prog_dialog->raise();
        prog_dialog->activateWindow();

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
    if (tool_info.id == "burst_level_analysis") {
        // Get node info from project
        auto nodes = project_.presenter()->getNodes();
        auto node_it = std::find_if(nodes.begin(), nodes.end(),
            [&node_id](const orc::presenters::NodeInfo& n) { return n.node_id == node_id; });
        
        QString node_label = QString::fromStdString(stage_name);
        if (node_it != nodes.end()) {
            node_label = QString::fromStdString(node_it->label.empty() ? node_it->stage_name : node_it->label);
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
        prog_dialog->setWindowModality(Qt::ApplicationModal);
        prog_dialog->setMinimumDuration(0);
        prog_dialog->setCancelButton(nullptr);
        prog_dialog->setValue(0);
        prog_dialog->show();
        prog_dialog->raise();
        prog_dialog->activateWindow();
        
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
    
    // Default path: Generic analysis dialog for all other tools
    // This handles tools like Field Corruption Generator using auto-generated UI
    
    // Create an AnalysisPresenter to get tool parameters and run the analysis
    auto* analysis_presenter = new orc::presenters::AnalysisPresenter(project_.presenter()->getCoreProjectHandle());
    
    // Get tool info to verify it exists
    const auto tool_info_full = analysis_presenter->getToolInfo(tool_info.id);
    if (tool_info_full.name.empty()) {
        ORC_LOG_WARN("Analysis tool '{}' (id='{}') not found",
                     tool_info.name, tool_info.id);
        QMessageBox::warning(this, "Analysis Tool Not Found",
            QString("The analysis tool '%1' was not found.")
                .arg(QString::fromStdString(tool_info.name)));
        delete analysis_presenter;
        return;
    }
    
    // Create and show generic analysis dialog
    auto* dialog = new orc::gui::GenericAnalysisDialog(
        tool_info.id,
        tool_info_full,
        analysis_presenter,
        node_id,
        project_.presenter()->getCoreProjectHandle(),
        this
    );
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    
    // Connect apply signal to actually apply results to the stage
    connect(dialog, &orc::gui::GenericAnalysisDialog::applyResultsRequested,
            [this, tool_info_id = tool_info.id, node_id, analysis_presenter](const orc::AnalysisResult& result) {
        ORC_LOG_DEBUG("Applying analysis results from tool '{}' to node '{}'",
                     tool_info_id, node_id.to_string());
        
        try {
            // Specialized presenters have already applied results to the graph via applyResultToGraph()
            // MainWindow just needs to update the UI
            
            // Rebuild DAG and update preview to reflect changes
            project_.rebuildDAG();
            updatePreviewRenderer();
            dag_model_->refresh();
            updatePreview();
            
            statusBar()->showMessage(
                QString("Applied analysis results from '%1' to node '%2'")
                    .arg(QString::fromStdString(tool_info_id))
                    .arg(QString::fromStdString(node_id.to_string())),
                5000
            );
            QMessageBox::information(this, "Results Applied",
                "Analysis results have been successfully applied to the stage.");
        } catch (const std::exception& e) {
            ORC_LOG_ERROR("Failed to apply analysis results: {}", e.what());
            QMessageBox::warning(this, "Apply Failed",
                QString("Failed to apply results: %1").arg(e.what()));
        }
    });
    
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

QProgressDialog* MainWindow::createAnalysisProgressDialog(const QString& title, const QString& message, QPointer<QProgressDialog>& existingDialog)
{
    if (existingDialog) {
        delete existingDialog;
    }
    
    auto* dialog = new QProgressDialog(message, QString(), 0, 100, this);
    dialog->setWindowTitle(title);
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->setMinimumDuration(0);
    dialog->setCancelButton(nullptr);  // No cancel for now
    dialog->setValue(0);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    
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

void MainWindow::onShowNtscObserverDialog()
{
    if (!ntsc_observer_dialog_) {
        return;
    }
    
    // Show the dialog first
    ntsc_observer_dialog_->show();
    ntsc_observer_dialog_->raise();
    ntsc_observer_dialog_->activateWindow();
    
    // Update NTSC observer information after showing
    updateNtscObserverDialog();
}

void MainWindow::onFieldTimingRequested()
{
    if (!current_view_node_id_.is_valid()) {
        ORC_LOG_WARN("No node selected for field timing view");
        return;
    }
    
    // Request field timing data for current preview frame/field
    int current_index = preview_dialog_->previewSlider()->value();
    pending_field_timing_request_id_ = render_coordinator_->requestFieldTimingData(
        current_view_node_id_,
        current_output_type_,
        current_index
    );
    
    ORC_LOG_DEBUG("Requested field timing data (request_id={})", pending_field_timing_request_id_);
}

void MainWindow::onLineScopeDialogClosed()
{
    // Clear the marker state when line scope is closed
    last_line_scope_field_index_ = std::numeric_limits<uint64_t>::max();
    last_line_scope_line_number_ = -1;
    last_line_scope_image_x_ = -1;
    
    // Update field timing dialog if it's visible to remove the marker
    auto* field_timing_dialog = preview_dialog_->fieldTimingDialog();
    if (field_timing_dialog && field_timing_dialog->isVisible()) {
        onFieldTimingRequested();
    }
}

void MainWindow::onSetCrosshairsFromFieldTiming()
{
    auto* field_timing_dialog = preview_dialog_->fieldTimingDialog();
    if (!field_timing_dialog) {
        return;
    }
    
    // Get video parameters to convert sample to field/line/x
    std::optional<orc::presenters::VideoParametersView> video_params;
    if (current_view_node_id_.is_valid()) {
        auto* core_project = project_.presenter()->getCoreProjectHandle();
        if (core_project) {
            orc::presenters::RenderPresenter render_presenter(core_project);
            render_presenter.setDAG(project_.getDAG());
            auto vp = render_presenter.getVideoParameters(current_view_node_id_);
            if (vp.has_value()) {
                video_params = orc::presenters::toVideoParametersView(*vp);
            }
        }
    }
    
    if (!video_params.has_value() || video_params->field_width <= 0 || video_params->field_height <= 0) {
        ORC_LOG_WARN("No video parameters available for set crosshairs");
        return;
    }
    
    // Get center sample from timing widget
    int center_sample = field_timing_dialog->timingWidget()->getCenterSample();
    if (center_sample < 0) {
        ORC_LOG_WARN("No valid center sample position");
        return;
    }
    
    const int fw = video_params->field_width;
    const int fh = video_params->field_height;
    
    // Determine which field the sample is in
    int samples_per_field = fw * fh;
    uint64_t field_offset = 0;
    int sample_in_field = center_sample;
    
    // Check if we're showing two fields (frame mode)
    if (current_output_type_ == orc::PreviewOutputType::Frame ||
        current_output_type_ == orc::PreviewOutputType::Frame_Reversed ||
        current_output_type_ == orc::PreviewOutputType::Split) {
        // Determine which field based on sample position
        int frame_index = preview_dialog_->previewSlider()->value();
        uint64_t field1 = frame_index * 2;
        uint64_t field2 = frame_index * 2 + 1;
        
        if (center_sample >= samples_per_field) {
            // In second field
            field_offset = field2;
            sample_in_field = center_sample - samples_per_field;
        } else {
            // In first field
            field_offset = field1;
            sample_in_field = center_sample;
        }
    } else {
        // Single field mode
        field_offset = preview_dialog_->previewSlider()->value();
    }
    
    // Convert sample position to line and x
    int line_number = sample_in_field / fw;
    int sample_x = sample_in_field % fw;
    
    // Clamp to valid range
    if (line_number >= fh) line_number = fh - 1;
    if (sample_x >= fw) sample_x = fw - 1;
    
    ORC_LOG_DEBUG("Setting crosshairs from field timing: center_sample={}, field={}, line={}, x={}",
                 center_sample, field_offset, line_number, sample_x);
    
    // Map field/line to image coordinates
    int image_height = preview_dialog_->previewWidget()->originalImageSize().height();
    auto mapping = render_coordinator_->mapFieldToImage(
        current_view_node_id_, current_output_type_,
        preview_dialog_->previewSlider()->value(),
        field_offset, line_number, image_height);
    
    if (!mapping.is_valid) {
        ORC_LOG_WARN("Failed to map field/line to image coordinates");
        return;
    }
    
    // Request line samples at this position (which will set crosshairs)
    onLineScopeRequested(sample_x, mapping.image_y);
}

void MainWindow::onFieldTimingDataReady(uint64_t request_id, uint64_t field_index,
                                       std::optional<uint64_t> field_index_2,
                                       std::vector<uint16_t> samples, std::vector<uint16_t> samples_2,
                                       std::vector<uint16_t> y_samples, std::vector<uint16_t> c_samples,
                                       std::vector<uint16_t> y_samples_2, std::vector<uint16_t> c_samples_2)
{
    Q_UNUSED(request_id);
    
    ORC_LOG_DEBUG("Field timing data ready: field {}{}, {} composite samples, {} Y samples, {} C samples",
                  field_index, field_index_2.has_value() ? " + " + std::to_string(field_index_2.value()) : "",
                  samples.size(), y_samples.size(), c_samples.size());
    
    auto* field_timing_dialog = preview_dialog_->fieldTimingDialog();
    if (!field_timing_dialog) {
        ORC_LOG_WARN("No field timing dialog available!");
        return;
    }
    
    // Get video parameters for mV conversion
    std::optional<orc::presenters::VideoParametersView> video_params;
    if (current_view_node_id_.is_valid()) {
        // Create temporary render presenter to get video parameters
        auto* core_project = project_.presenter()->getCoreProjectHandle();
        if (core_project) {
            orc::presenters::RenderPresenter render_presenter(core_project);
            render_presenter.setDAG(project_.getDAG());
            
            auto vp = render_presenter.getVideoParameters(current_view_node_id_);
            if (vp.has_value()) {
                // Convert to view model
                video_params = orc::presenters::toVideoParametersView(*vp);
            }
        }
    }
    
    // Set the field data and show the dialog
    std::optional<int> marker_sample;
    if (video_params.has_value() && video_params->field_width > 0 && video_params->field_height > 0 &&
        last_line_scope_field_index_ != std::numeric_limits<uint64_t>::max() &&
        last_line_scope_image_x_ >= 0 && last_line_scope_line_number_ >= 0) {
        const int fw = video_params->field_width;
        const int fh = video_params->field_height;
        
        ORC_LOG_DEBUG("Field timing marker calculation: last_field={}, last_line={}, last_x={}, field_index={}, field_index_2={}",
                     last_line_scope_field_index_, last_line_scope_line_number_, last_line_scope_image_x_,
                     field_index, field_index_2.has_value() ? field_index_2.value() : 0);
        
        if (last_line_scope_line_number_ < fh) {
            int clamped_x = std::max(0, std::min(last_line_scope_image_x_, fw - 1));
            int local_sample = last_line_scope_line_number_ * fw + clamped_x;

            // Determine which field in the timing data this maps to
            uint64_t f1 = field_index;
            std::optional<uint64_t> f2 = field_index_2;
            if (last_line_scope_field_index_ == f1) {
                marker_sample = local_sample;
                ORC_LOG_DEBUG("Marker in field 1 at sample {}", local_sample);
            } else if (f2.has_value() && last_line_scope_field_index_ == f2.value()) {
                marker_sample = local_sample + fw * fh;  // offset into second field
                ORC_LOG_DEBUG("Marker in field 2 at sample {} (offset {})", marker_sample.value(), local_sample);
            } else {
                ORC_LOG_DEBUG("Marker field {} doesn't match f1={} or f2={}", 
                            last_line_scope_field_index_, f1, f2.has_value() ? f2.value() : 0);
            }
        }
    }

    field_timing_dialog->setFieldData(
        QString::fromStdString(current_view_node_id_.to_string()),
        field_index,
        samples,
        field_index_2,
        samples_2,
        y_samples,
        c_samples,
        y_samples_2,
        c_samples_2,
        video_params,
        marker_sample
    );
    
    // Only show/raise/activate if not already visible
    // This prevents stealing focus when the dialog is already open
    if (!field_timing_dialog->isVisible()) {
        field_timing_dialog->show();
        field_timing_dialog->raise();
        field_timing_dialog->activateWindow();
    }
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
    
    // Get field IDs from core library (handles field ordering correctly)
    orc::FieldID field1_id;
    orc::FieldID field2_id;
    
    if (is_frame_mode) {
        // Use core library to determine which fields make up this frame
        auto frame_fields = render_coordinator_->getFrameFields(current_view_node_id_, current_index);
        if (!frame_fields.is_valid) {
            quality_metrics_dialog_->clearMetrics();
            return;
        }
        field1_id = orc::FieldID(frame_fields.first_field);
        field2_id = orc::FieldID(frame_fields.second_field);
    } else {
        // Field mode - simple mapping
        field1_id = orc::FieldID(current_index);
        field2_id = orc::FieldID(0);
    }
    
    // Get quality metrics using presenter (no direct DAGFieldRenderer access)
    try {
        // Create temporary RenderPresenter for metrics extraction
        auto* core_project = project_.presenter()->getCoreProjectHandle();
        if (!core_project) {
            quality_metrics_dialog_->clearMetrics();
            return;
        }
        
        orc::presenters::RenderPresenter render_presenter(core_project);
        render_presenter.setDAG(project_.getDAG());
        
        if (is_frame_mode) {
            // Render both fields to populate observation context, then update dialog with both fields and frame averages
            (void)render_presenter.getObservationContext(current_view_node_id_, field1_id);
            (void)render_presenter.getObservationContext(current_view_node_id_, field2_id);
            const void* ctx_ptr = render_presenter.getObservationContext(current_view_node_id_, field1_id);
            if (!ctx_ptr) {
                quality_metrics_dialog_->clearMetrics();
                return;
            }
            quality_metrics_dialog_->updateMetricsForFrameFromContext(field1_id, field2_id, ctx_ptr);
        } else {
            // Single field mode: render field to populate observation context, then update dialog
            const void* ctx_ptr = render_presenter.getObservationContext(current_view_node_id_, field1_id);
            if (!ctx_ptr) {
                quality_metrics_dialog_->clearMetrics();
                return;
            }
            quality_metrics_dialog_->updateMetricsFromContext(field1_id, ctx_ptr);
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
    // NOTE: Do NOT call updateLineScope() here. The line scope should maintain its current
    // field/line across frame changes. Automatic refresh would cause the displayed line to
    // jump because the mapping from image_y to field coordinates changes between frames.
    // The line scope is only updated when:
    // 1. User clicks on the preview to select a new line
    // 2. User navigates up/down with line scope buttons
    // 3. Line scope is initially opened
    // 4. Frame changes - line scope updates via its own connection to previewFrameChanged signal
    updateVBIDialog();
    updateHintsDialog();
    updateQualityMetricsDialog();
    updateNtscObserverDialog();
    
    // Notify line scope dialog that preview frame has changed
    // Line scope will refresh samples at its current field/line position via orc-core
    if (preview_dialog_) {
        ORC_LOG_DEBUG("updateAllPreviewComponents: calling notifyFrameChanged");
        preview_dialog_->notifyFrameChanged();
    }
    
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
        // Frame mode - get field IDs from core library (handles field ordering)
        auto frame_fields = render_coordinator_->getFrameFields(current_view_node_id_, current_index);
        if (!frame_fields.is_valid) {
            vbi_dialog_->clearVBIInfo();
            return;
        }
        orc::FieldID field1_id(frame_fields.first_field);
        orc::FieldID field2_id(frame_fields.second_field);
        // Request both fields - VBI interpretation requires data from both fields
        // (e.g., CLV timecode may be split across fields)
        pending_vbi_is_frame_mode_ = true;
        pending_vbi_request_id_ = render_coordinator_->requestVBIData(current_view_node_id_, field1_id);
        pending_vbi_request_id_field2_ = render_coordinator_->requestVBIData(current_view_node_id_, field2_id);
    } else {
        // Field mode - request single field
        pending_vbi_is_frame_mode_ = false;
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
    
    // Get field ID from core library (handles field ordering correctly)
    orc::FieldID field_id;
    orc::FieldID second_field_id;
    if (is_frame_mode) {
        // Use core library to determine first field of frame
        auto frame_fields = render_coordinator_->getFrameFields(current_view_node_id_, current_index);
        if (!frame_fields.is_valid) {
            hints_dialog_->clearHints();
            return;
        }
        field_id = orc::FieldID(frame_fields.first_field);
        second_field_id = orc::FieldID(frame_fields.second_field);
    } else {
        // Field mode - simple mapping
        field_id = orc::FieldID(current_index);
    }
    
    // Get hints from the current DAG/node
    // Note: This is a synchronous access - we'll create a temporary renderer
    if (!hints_presenter_) {
        hints_dialog_->clearHints();
        return;
    }

    auto hints = hints_presenter_->getHintsForField(current_view_node_id_, field_id);
    hints_dialog_->updateFieldParityHint(hints.parity);
    
    // Update phase hint based on mode
    if (is_frame_mode) {
        auto second_hints = hints_presenter_->getHintsForField(current_view_node_id_, second_field_id);
        hints_dialog_->updateFieldPhaseHintForFrame(hints.phase, second_hints.phase);
    } else {
        hints_dialog_->updateFieldPhaseHint(hints.phase);
    }
    
    hints_dialog_->updateActiveLineHint(hints.active_line);
    hints_dialog_->updateVideoParameters(hints.video_params);
}

void MainWindow::updateNtscObserverDialog()
{
    // Only update if NTSC observer dialog is visible
    if (!ntsc_observer_dialog_ || !ntsc_observer_dialog_->isVisible()) {
        return;
    }
    
    // Get current field being displayed
    if (!current_view_node_id_.is_valid()) {
        ntsc_observer_dialog_->clearObservations();
        return;
    }
    
    // Get the current index from the preview slider
    int current_index = preview_dialog_->previewSlider()->value();
    
    // Check if we're in frame mode (any mode that shows two fields)
    bool is_frame_mode = (current_output_type_ == orc::PreviewOutputType::Frame ||
                         current_output_type_ == orc::PreviewOutputType::Frame_Reversed ||
                         current_output_type_ == orc::PreviewOutputType::Split);
    
    // Get field IDs from core library (handles field ordering correctly)
    orc::FieldID field1_id;
    orc::FieldID field2_id;
    
    if (is_frame_mode) {
        // Use core library to determine which fields make up this frame
        auto frame_fields = render_coordinator_->getFrameFields(current_view_node_id_, current_index);
        if (!frame_fields.is_valid) {
            ntsc_observer_dialog_->clearObservations();
            return;
        }
        field1_id = orc::FieldID(frame_fields.first_field);
        field2_id = orc::FieldID(frame_fields.second_field);
    } else {
        // Field mode - simple mapping
        field1_id = orc::FieldID(current_index);
        field2_id = orc::FieldID(0);
    }
    
    // Get NTSC observations using presenter (no direct DAGFieldRenderer access)
    try {
        // Create temporary RenderPresenter for observation extraction
        auto* core_project = project_.presenter()->getCoreProjectHandle();
        if (!core_project) {
            ntsc_observer_dialog_->clearObservations();
            return;
        }
        
        orc::presenters::RenderPresenter render_presenter(core_project);
        render_presenter.setDAG(project_.getDAG());
        
        if (is_frame_mode) {
            // Get observation context for both fields
            const auto* context1_void = render_presenter.getObservationContext(current_view_node_id_, field1_id);
            const auto* context2_void = render_presenter.getObservationContext(current_view_node_id_, field2_id);
            
            if (!context1_void || !context2_void) {
                ntsc_observer_dialog_->clearObservations();
                return;
            }
            
            // Extract observations from opaque context pointers
            auto field1_obs = orc::presenters::NtscObservationPresenter::extractFieldObservations(
                field1_id, context1_void);
            auto field2_obs = orc::presenters::NtscObservationPresenter::extractFieldObservations(
                field2_id, context2_void);
            ntsc_observer_dialog_->updateObservationsForFrame(field1_id, field1_obs, field2_id, field2_obs);
        } else {
            // Get observation context for single field
            const auto* context_void = render_presenter.getObservationContext(current_view_node_id_, field1_id);
            
            if (!context_void) {
                ntsc_observer_dialog_->clearObservations();
                return;
            }
            
            // Extract observations from opaque context pointer
            auto field_obs = orc::presenters::NtscObservationPresenter::extractFieldObservations(
                field1_id, context_void);
            ntsc_observer_dialog_->updateObservations(field1_id, field_obs);
        }
        
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Failed to get NTSC observations: {}", e.what());
        ntsc_observer_dialog_->clearObservations();
    }
}
void MainWindow::onLineScopeRequested(int image_x, int image_y)
{
    ORC_LOG_DEBUG("Line scope requested at image position ({}, {})", image_x, image_y);
    
    if (!current_view_node_id_.is_valid()) {
        ORC_LOG_WARN("No node selected for line scope");
        return;
    }
    
    // Store original image x coordinate for later use (to avoid rounding errors)
    last_line_scope_image_x_ = image_x;
    
    // Get the preview image height for split mode
    int image_height = preview_dialog_->previewWidget()->originalImageSize().height();
    
    // Use orc-core to map image coordinates to field coordinates
    // This ensures the GUI doesn't duplicate field ordering logic
    auto mapping = render_coordinator_->mapImageToField(
        current_view_node_id_, current_output_type_, 
        preview_dialog_->previewSlider()->value(), image_y, image_height);
    
    if (!mapping.is_valid) {
        ORC_LOG_WARN("Failed to map image coordinates to field");
        return;
    }
    
    uint64_t field_index = mapping.field_index;
    int field_line = mapping.field_line;
    
    // Map image_x from preview image coordinates to field sample coordinates
    // The preview widget gives us coordinates in the rendered RGB image space,
    // but the field data may have a different width (no aspect ratio correction in samples)
    // We need to get the original field width to do proper mapping
    // For now, we'll pass image_x and let the backend handle clamping
    // TODO: Get actual field descriptor to properly map coordinates
    int sample_x = image_x;
    
    ORC_LOG_DEBUG("Requesting line samples for field {}, line {}, sample_x {}", field_index, field_line, sample_x);
    
    // Get the preview image width for coordinate mapping
    int preview_image_width = preview_dialog_->previewWidget()->originalImageSize().width();
    
    // Request line samples from the coordinator using Field output type
    pending_line_sample_request_id_ = render_coordinator_->requestLineSamples(
        current_view_node_id_,
        orc::PreviewOutputType::Field,
        field_index,
        field_line,
        sample_x,
        preview_image_width
    );
}

void MainWindow::onLineSamplesReady(uint64_t request_id, uint64_t field_index, int line_number, int sample_x, 
                                    std::vector<uint16_t> samples, std::optional<orc::SourceParameters> video_params,
                                    std::vector<uint16_t> y_samples, std::vector<uint16_t> c_samples)
{
    Q_UNUSED(request_id);
    
    ORC_LOG_DEBUG("Line samples ready: {} samples for field {}, line {}, sample_x={} (YC: Y={}, C={}) mode={}", 
                  samples.size(), field_index, line_number, sample_x, y_samples.size(), c_samples.size(),
                  static_cast<int>(current_output_type_));
    
    // Convert public API SourceParameters to presenter VideoParametersView for dialogs
    std::optional<orc::presenters::VideoParametersView> view_params;
    if (video_params.has_value()) {
        view_params = orc::presenters::toVideoParametersView(video_params.value());
    }
    
    if (!preview_dialog_) {
        ORC_LOG_WARN("No preview dialog available!");
        return;
    }
    
    // Get preview image width for coordinate mapping
    int preview_image_width = preview_dialog_->previewWidget()->originalImageSize().width();
    int preview_image_height = preview_dialog_->previewWidget()->originalImageSize().height();
    
    // Store the actual field/line being displayed - this is the ground truth
    last_line_scope_field_index_ = field_index;
    last_line_scope_line_number_ = line_number;
    
    // Store context for sample marker updates
    last_line_scope_preview_width_ = preview_image_width;
    last_line_scope_samples_count_ = samples.size();
    if (!samples.empty()) {
        // Use composite samples if available
    } else if (!y_samples.empty()) {
        // Use Y samples for size if no composite
        last_line_scope_samples_count_ = y_samples.size();
    }
    
    // Use orc-core to map the current field/line to image coordinates for display
    // This ensures we always have the correct visual position for the current frame
    uint64_t current_index = preview_dialog_->previewSlider()->value();
    auto mapping = render_coordinator_->mapFieldToImage(
        current_view_node_id_, current_output_type_, current_index,
        field_index, line_number, preview_image_height);
    
    int image_y = 0;
    if (mapping.is_valid) {
        image_y = mapping.image_y;
        // Update cross-hairs using the correctly mapped position for this frame
        preview_dialog_->previewWidget()->updateCrosshairsPosition(last_line_scope_image_x_, image_y);
    } else {
        ORC_LOG_WARN("Failed to map field coordinates to image - not updating cross-hairs to avoid jumping");
        // Don't update cross-hairs if mapping fails - keep them at the last valid position
        // This prevents the cross-hairs from jumping to an incorrect position during navigation
        image_y = line_number;  // Fallback for dialog display, but not for cross-hairs
    }
    
    // Use the stored original image_x to avoid rounding errors from reverse-mapping
    int original_sample_x = last_line_scope_image_x_;
    
    ORC_LOG_DEBUG("Using original_sample_x={} from last request", original_sample_x);
    
    // Calculate image_y from field/line using orc-core
    int image_height = preview_dialog_->previewWidget()->originalImageSize().height();
    auto image_coords = render_coordinator_->mapFieldToImage(
        current_view_node_id_, current_output_type_,
        preview_dialog_->previewSlider()->value(), field_index, line_number, image_height);
    int calculated_image_y = image_coords.is_valid ? image_coords.image_y : 0;
    
    // Store field/line for later navigation
    last_line_scope_field_index_ = field_index;
    last_line_scope_line_number_ = line_number;
    
    // Show the line scope dialog with the samples, including the current node_id
    QString node_id_str = QString::fromStdString(current_view_node_id_.to_string());
    preview_dialog_->showLineScope(node_id_str, field_index, line_number, sample_x, samples, view_params, 
                                   preview_image_width, original_sample_x, calculated_image_y, y_samples, c_samples);
    
    // Update field timing dialog if it's visible (to update the marker position)
    if (preview_dialog_->fieldTimingDialog() && preview_dialog_->fieldTimingDialog()->isVisible()) {
        ORC_LOG_DEBUG("Refreshing field timing dialog to update marker position");
        onFieldTimingRequested();
    }
}

void MainWindow::onFrameLineNavigationReady(uint64_t request_id, orc::FrameLineNavigationResult result)
{
    Q_UNUSED(request_id);
    
    ORC_LOG_DEBUG("Frame line navigation ready: valid={}, new_field={}, new_line={}", 
                  result.is_valid, result.new_field_index, result.new_line_number);
    
    if (!result.is_valid) {
        ORC_LOG_DEBUG("Frame line navigation out of bounds, staying at current position");
        return;
    }
    
    // Request line samples at the new position using the unified helper
    ORC_LOG_DEBUG("Requesting samples at field {}, line {}", result.new_field_index, result.new_line_number);
    
    requestLineSamplesForNavigation(
        result.new_field_index,
        result.new_line_number,
        last_line_scope_image_x_,
        preview_dialog_->previewWidget()->originalImageSize().width()
    );
}

void MainWindow::requestLineSamplesForNavigation(uint64_t field_index, int line_number, int sample_x, int preview_image_width)
{
    // Unified helper to request line samples for navigation
    // This ensures all modes (Field, Frame, Split) handle updates consistently
    // Update stored field/line BEFORE requesting so they're available when samples arrive
    last_line_scope_field_index_ = field_index;
    last_line_scope_line_number_ = line_number;
    
    ORC_LOG_DEBUG("requestLineSamplesForNavigation: mode={}, field={}, line={}, sample_x={}, width={}", 
                  static_cast<int>(current_output_type_), field_index, line_number, sample_x, preview_image_width);
    
    pending_line_sample_request_id_ = render_coordinator_->requestLineSamples(
        current_view_node_id_,
        orc::PreviewOutputType::Field,
        field_index,
        line_number,
        sample_x,
        preview_image_width
    );
    
    ORC_LOG_DEBUG("requestLineSamplesForNavigation: request_id={}", pending_line_sample_request_id_);
}

void MainWindow::onSampleMarkerMoved(int sample_x)
{
    if (!preview_dialog_) {
        return;
    }
    
    // Map sample_x from field-space back to preview-space
    int preview_x = (sample_x * last_line_scope_preview_width_) / last_line_scope_samples_count_;
    
    // Calculate image_y from stored field/line using orc-core
    int image_height = preview_dialog_->previewWidget()->originalImageSize().height();
    auto image_coords = render_coordinator_->mapFieldToImage(
        current_view_node_id_, current_output_type_,
        preview_dialog_->previewSlider()->value(), 
        last_line_scope_field_index_, last_line_scope_line_number_, image_height);
    
    // Only update cross-hairs if mapping is valid
    // If mapping fails, keep the cross-hairs at their current position to avoid jumping
    if (image_coords.is_valid) {
        // Update cross-hairs at the new X position with recalculated Y
        preview_dialog_->previewWidget()->updateCrosshairsPosition(preview_x, image_coords.image_y);
        
        // Update stored position so it's maintained when changing stages
        last_line_scope_image_x_ = preview_x;
        
        // Update field timing dialog if it's visible (to update the marker position)
        if (preview_dialog_->fieldTimingDialog() && preview_dialog_->fieldTimingDialog()->isVisible()) {
            ORC_LOG_DEBUG("Refreshing field timing dialog to update marker after sample marker move");
            onFieldTimingRequested();
        }
    } else {
        ORC_LOG_WARN("Failed to map field coordinates in onSampleMarkerMoved - not updating cross-hairs");
    }
}

void MainWindow::refreshLineScopeForCurrentStage()
{
    if (!preview_dialog_ || !preview_dialog_->isLineScopeVisible()) {
        return;  // Line scope not visible, nothing to do
    }
    
    if (!current_view_node_id_.is_valid()) {
        // No valid stage selected - clear line scope and cross-hairs
        ORC_LOG_DEBUG("No valid stage for line scope, clearing");
        preview_dialog_->previewWidget()->setCrosshairsEnabled(false);
        
        // Show empty line scope
        QString node_id_str = "(none)";
        preview_dialog_->showLineScope(node_id_str, 0, 0, 0, 
                                      std::vector<uint16_t>(),  // Empty samples
                                      std::nullopt, 0, 0, 0);
        return;
    }
    
    // Re-request line samples for the same field/line but new stage
    // Calculate the image_y from stored field/line for the refresh
    if (last_line_scope_image_x_ >= 0 && last_line_scope_field_index_ >= 0) {
        int image_height = preview_dialog_->previewWidget()->originalImageSize().height();
        auto image_coords = render_coordinator_->mapFieldToImage(
            current_view_node_id_, current_output_type_,
            preview_dialog_->previewSlider()->value(), 
            last_line_scope_field_index_, last_line_scope_line_number_, image_height);
        int calculated_image_y = image_coords.is_valid ? image_coords.image_y : 0;
        
        ORC_LOG_DEBUG("Refreshing line scope for new stage at field={}, line={}, image_y={}", 
                     last_line_scope_field_index_, last_line_scope_line_number_, calculated_image_y);
        onLineScopeRequested(last_line_scope_image_x_, calculated_image_y);
    }
}

void MainWindow::onLineScopeRefreshAtFieldLine()
{
    ORC_LOG_DEBUG("onLineScopeRefreshAtFieldLine called");
    
    if (!preview_dialog_ || !preview_dialog_->isLineScopeVisible()) {
        ORC_LOG_DEBUG("Line scope not visible, skipping refresh");
        return;
    }
    
    if (!current_view_node_id_.is_valid()) {
        ORC_LOG_DEBUG("No valid node, skipping refresh");
        return;
    }
    
    // If we don't have a valid stored field, try to initialize from current slider position
    if (last_line_scope_field_index_ < 0) {
        ORC_LOG_DEBUG("No stored field index, initializing from current mode");
        
        if (current_output_type_ == orc::PreviewOutputType::Field) {
            last_line_scope_field_index_ = preview_dialog_->previewSlider()->value();
        } else if (current_output_type_ == orc::PreviewOutputType::Split) {
            // For split mode, use first field of the pair
            uint64_t pair_index = preview_dialog_->previewSlider()->value();
            last_line_scope_field_index_ = pair_index * 2;
        } else if (current_output_type_ == orc::PreviewOutputType::Frame ||
                   current_output_type_ == orc::PreviewOutputType::Frame_Reversed) {
            // For frame mode, get fields from frame and use first one
            auto frame_fields = render_coordinator_->getFrameFields(current_view_node_id_, 
                                                                    preview_dialog_->previewSlider()->value());
            if (frame_fields.is_valid) {
                last_line_scope_field_index_ = frame_fields.first_field;
            } else {
                ORC_LOG_WARN("Failed to get frame fields, cannot initialize");
                return;
            }
        }
        ORC_LOG_DEBUG("Initialized field_index to {}", last_line_scope_field_index_);
    }
    
    ORC_LOG_DEBUG("Refreshing line scope: was at field={}, line={}", 
                 last_line_scope_field_index_, last_line_scope_line_number_);
    
    uint64_t new_field_index = last_line_scope_field_index_;
    
    // If in Frame mode, get which fields are in the current frame and pick the one with same parity
    if (current_output_type_ == orc::PreviewOutputType::Frame || 
        current_output_type_ == orc::PreviewOutputType::Frame_Reversed) {
        uint64_t frame_index = preview_dialog_->previewSlider()->value();
        auto frame_fields = render_coordinator_->getFrameFields(current_view_node_id_, frame_index);
        
        if (frame_fields.is_valid) {
            // Pick field with same parity (odd/even) as the one we were viewing
            bool was_odd = (last_line_scope_field_index_ % 2) == 1;
            bool first_is_odd = (frame_fields.first_field % 2) == 1;
            
            if (was_odd == first_is_odd) {
                new_field_index = frame_fields.first_field;
            } else {
                new_field_index = frame_fields.second_field;
            }
            
            ORC_LOG_DEBUG("Frame {} contains fields [{}, {}], using field {} (parity match)",
                         frame_index, frame_fields.first_field, frame_fields.second_field, new_field_index);
        } else {
            ORC_LOG_WARN("Failed to get frame fields, keeping old field index");
        }
    }
    // In Field or Split mode, just use the current field index from the slider
    else if (current_output_type_ == orc::PreviewOutputType::Field) {
        new_field_index = preview_dialog_->previewSlider()->value();
        ORC_LOG_DEBUG("Field mode: using field {} from slider", new_field_index);
    }
    else if (current_output_type_ == orc::PreviewOutputType::Split) {
        // In Split mode, preserve the parity (odd/even) of the field we were viewing
        // Split pairs are always: (0,1), (2,3), (4,5), etc.
        uint64_t pair_index = preview_dialog_->previewSlider()->value();
        uint64_t first_field_in_pair = pair_index * 2;
        uint64_t second_field_in_pair = pair_index * 2 + 1;
        
        // Preserve the parity of the field we were viewing
        bool was_odd = (last_line_scope_field_index_ % 2) == 1;
        
        if (was_odd) {
            new_field_index = second_field_in_pair;
        } else {
            new_field_index = first_field_in_pair;
        }
        ORC_LOG_DEBUG("Split mode: pair_index={}, preserving parity, using field={}", pair_index, new_field_index);
    }
    
    // Validate that we have sensible values before requesting samples
    if (new_field_index < 0 || last_line_scope_line_number_ < 0) {
        ORC_LOG_WARN("Invalid field={} or line={}, cannot refresh", new_field_index, last_line_scope_line_number_);
        return;
    }
    
    // Request samples at the SAME line number in the new field
    int sample_x = last_line_scope_image_x_;
    int preview_width = preview_dialog_->previewWidget()->originalImageSize().width();
    
    ORC_LOG_DEBUG("Requesting samples at field={}, line={}, sample_x={}", 
                 new_field_index, last_line_scope_line_number_, sample_x);
    
    pending_line_sample_request_id_ = render_coordinator_->requestLineSamples(
        current_view_node_id_, 
        orc::PreviewOutputType::Field,
        new_field_index, last_line_scope_line_number_, 
        sample_x, preview_width);
}

void MainWindow::onLineNavigation(int direction, uint64_t current_field, int current_line, int sample_x, int preview_image_width)
{
    ORC_LOG_DEBUG("Line navigation requested: direction={}, field={}, line={}, sample_x={}", direction, current_field, current_line, sample_x);
    
    if (!current_view_node_id_.is_valid()) {
        return;
    }
    
    // NOTE: Do NOT modify last_line_scope_image_x_ here!
    // It was set in onLineScopeRequested() as the visual image coordinate where the user clicked.
    // We need to preserve it so the cross-hairs stay in the same visual position when samples refresh.
    // The sample_x parameter here is in field coordinates (0-909), not image coordinates.
    
    // Get the image size to determine bounds
    int image_height = preview_dialog_->previewWidget()->originalImageSize().height();
    int field_height = image_height;
    
    if (current_output_type_ == orc::PreviewOutputType::Field) {
        // Simple field mode - just move within the same field
        int new_line_number = current_line + direction;
        field_height = image_height;
        
        // Bounds check
        if (new_line_number < 0 || new_line_number >= field_height) {
            ORC_LOG_DEBUG("Line navigation rejected: line {} is out of bounds (field_height={})", new_line_number, field_height);
            return;
        }
        
        // In field mode, stay in the same field
        ORC_LOG_DEBUG("Navigating to field {}, line {}", current_field, new_line_number);
        
        // Use unified helper to request samples
        requestLineSamplesForNavigation(current_field, new_line_number, sample_x, preview_image_width);
        
    } else if (current_output_type_ == orc::PreviewOutputType::Frame ||
               current_output_type_ == orc::PreviewOutputType::Frame_Reversed) {
        // Frame mode - delegate to core library via async request
        // In frame mode, image height is 2x field height (interlaced)
        field_height = image_height / 2;
        
        // Request the core library to calculate navigation
        pending_line_sample_request_id_ = render_coordinator_->requestFrameLineNavigation(
            current_view_node_id_,
            current_output_type_,
            current_field,
            current_line,
            direction,
            field_height
        );
        
    } else if (current_output_type_ == orc::PreviewOutputType::Split) {
        // Split mode - two fields stacked vertically, stay within the same field
        field_height = image_height / 2;
        int new_line_number = current_line + direction;
        
        // Bounds check
        if (new_line_number < 0 || new_line_number >= field_height) {
            ORC_LOG_DEBUG("Line navigation rejected: line {} is out of bounds in split mode (field_height={})", new_line_number, field_height);
            return;
        }
        
        ORC_LOG_DEBUG("Navigating to field {}, line {}", current_field, new_line_number);
        
        // Use unified helper to request samples - same as field mode
        requestLineSamplesForNavigation(current_field, new_line_number, sample_x, preview_image_width);
    }
}

