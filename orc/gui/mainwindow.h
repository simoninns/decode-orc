/*
 * File:        mainwindow.h
 * Module:      orc-gui
 * Purpose:     Main application window
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QTabWidget>
#include <QPointer>
#include <QProgressDialog>
#include <QTimer>
#include <memory>
#include <future>
#include "../core/include/node_id.h"
#include "guiproject.h"
#include "preview_renderer.h"  // For PreviewOutputType
#include "orcgraphmodel.h"
#include "orcgraphicsscene.h"
#include "render_coordinator.h"
#include "../core/include/node_id.h"

class OrcGraphicsView;
class PreviewDialog;
class VBIDialog;
class HintsDialog;
class PulldownDialog;
class DropoutAnalysisDialog;
class SNRAnalysisDialog;
class BurstLevelAnalysisDialog;
class QualityMetricsDialog;
class VectorscopeDialog;
class RenderCoordinator;

namespace orc {
    class DropoutAnalysisDecoder;
    enum class DropoutAnalysisMode;
    class SNRAnalysisDecoder;
    enum class SNRAnalysisMode;
}

namespace orc {
    class VideoFieldRepresentation;
    class DAG;
    class AnalysisTool;
    class VBIDecoder;
    class DropoutAnalysisDecoder;
}

class FieldPreviewWidget;
class QLabel;
class QSlider;
class QToolBar;
class QComboBox;
class QSplitter;
class QTimer;

/**
 * Main window for orc-gui
 * 
 * Layout:
 * - Toolbar (file operations, source selection)
 * - Central preview area
 * - Bottom status/navigation bar
 * 
 * Architecture: This window is a thin display client.
 * All rendering logic is in orc::PreviewRenderer (orc-core).
 */

using orc::NodeID;  // Make NodeID available for Qt signals/slots

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    
    // Project operations
    void newProject(orc::VideoSystem video_format = orc::VideoSystem::Unknown);
    void openProject(const QString& filename);
    void saveProject();
    void saveProjectAs();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNewProject();  // Shows a submenu or default
    void onNewNTSCProject();
    void onNewPALProject();
    void onOpenProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onEditProject();
    void onPreviewIndexChanged(int index);
    void onNavigatePreview(int delta);
    void onPreviewModeChanged(int index);
    void onAspectRatioModeChanged(int index);
    void onNodeSelectedForView(const orc::NodeID& node_id);
    void onDAGModified();
    void onPreviewDialogExportPNG();
    void onNodeContextMenu(QtNodes::NodeId nodeId, const QPointF& pos);
    void onArrangeDAGToGrid();
    void onQtNodeSelected(QtNodes::NodeId nodeId);
    void onInspectStage(const orc::NodeID& node_id);
    void onShowVBIDialog();
    void updateVBIDialog();
    void onShowHintsDialog();
    void updateHintsDialog();
    void onShowQualityMetricsDialog();
    void updateQualityMetricsDialog();
    void onShowPulldownDialog();
    void updatePulldownDialog();
    
    // Coordinator response slots
    void onPreviewReady(uint64_t request_id, orc::PreviewRenderResult result);
    void onVBIDataReady(uint64_t request_id, orc::VBIFieldInfo info);
    void onAvailableOutputsReady(uint64_t request_id, std::vector<orc::PreviewOutputInfo> outputs);
    void onDropoutDataReady(uint64_t request_id, std::vector<orc::FrameDropoutStats> frame_stats, int32_t total_frames);
    void onDropoutProgress(size_t current, size_t total, QString message);
    void onSNRDataReady(uint64_t request_id, std::vector<orc::FrameSNRStats> frame_stats, int32_t total_frames);
    void onSNRProgress(size_t current, size_t total, QString message);
    void onBurstLevelDataReady(uint64_t request_id, std::vector<orc::FrameBurstLevelStats> frame_stats, int32_t total_frames);
    void onBurstLevelProgress(size_t current, size_t total, QString message);
    void onTriggerProgress(size_t current, size_t total, QString message);
    void onTriggerComplete(uint64_t request_id, bool success, QString status);
    void onCoordinatorError(uint64_t request_id, QString message);
    void onAbout();

signals:

private:
    bool checkUnsavedChanges();  // Returns true if safe to proceed, false if cancelled
    void setupUI();
    void setupMenus();
    void setupToolbar();
    void updateWindowTitle();
    void updatePreviewInfo();
    void updateUIState();
    void updatePreview();
    void updatePreviewRenderer();
    void updatePreviewModeCombo();
    void updateAspectRatioCombo();  // Populate aspect ratio combo from core
    void refreshViewerControls();  // Update slider, combo, preview, and info for current node
    void updateAllPreviewComponents();  // Update preview image, info label, VBI dialog, and vectorscope(s)
    void updateVectorscope(const orc::NodeID& node_id, const orc::PreviewImage& image);
    void loadProjectDAG();  // Load DAG into embedded viewer
    void positionViewToTopLeft();  // Position view to show top-left node
    void onEditParameters(const orc::NodeID& node_id);
    void onTriggerStage(const orc::NodeID& node_id);
    void runAnalysisForNode(orc::AnalysisTool* tool, const orc::NodeID& node_id, const std::string& stage_name);
    QProgressDialog* createAnalysisProgressDialog(const QString& title, const QString& message, QPointer<QProgressDialog>& existingDialog);
    
    // Settings helpers
    QString getLastProjectDirectory() const;
    void setLastProjectDirectory(const QString& path);
    QString getLastExportDirectory() const;
    void setLastExportDirectory(const QString& path);
    void saveSettings();
    void restoreSettings();
    
    // Project management
    GUIProject project_;
    std::unique_ptr<RenderCoordinator> render_coordinator_;  // Owns all core rendering state
    orc::NodeID current_view_node_id_;  // Which node is being viewed
    QtNodes::NodeId last_selected_qt_node_id_;  // Last selected node in DAG for DEL key
    
    // Pending request tracking
    uint64_t pending_preview_request_id_{0};
    uint64_t pending_vbi_request_id_{0};
    uint64_t pending_outputs_request_id_{0};
    uint64_t pending_trigger_request_id_{0};
    std::unordered_map<uint64_t, orc::NodeID> pending_dropout_requests_;  // request_id -> node_id
    std::unordered_map<uint64_t, orc::NodeID> pending_snr_requests_;      // request_id -> node_id
    std::unordered_map<uint64_t, orc::NodeID> pending_burst_level_requests_;  // request_id -> node_id
    
    // Dropout analysis state tracking
    orc::NodeID last_dropout_node_id_;
    orc::DropoutAnalysisMode last_dropout_mode_;
    orc::PreviewOutputType last_dropout_output_type_;
    
    // SNR analysis state tracking
    orc::NodeID last_snr_node_id_;
    orc::SNRAnalysisMode last_snr_mode_;
    orc::PreviewOutputType last_snr_output_type_;
    
    // UI components
    PreviewDialog* preview_dialog_;
    QualityMetricsDialog* quality_metrics_dialog_;
    VBIDialog* vbi_dialog_;
    HintsDialog* hints_dialog_;
    PulldownDialog* pulldown_dialog_;
    std::unordered_map<orc::NodeID, DropoutAnalysisDialog*> dropout_analysis_dialogs_;
    std::unordered_map<orc::NodeID, SNRAnalysisDialog*> snr_analysis_dialogs_;
    std::unordered_map<orc::NodeID, BurstLevelAnalysisDialog*> burst_level_analysis_dialogs_;
    std::unordered_map<orc::NodeID, VectorscopeDialog*> vectorscope_dialogs_;
    OrcGraphModel* dag_model_;
    OrcGraphicsView* dag_view_;
    OrcGraphicsScene* dag_scene_;
    QAction* save_project_action_;
    QAction* save_project_as_action_;
    QAction* edit_project_action_;
    QAction* show_preview_action_;
    QAction* auto_show_preview_action_;
    
    // Preview state (UI only - all data comes from core)
    orc::PreviewOutputType current_output_type_;
    std::string current_option_id_;  ///< Current option ID for PreviewableStage rendering
    orc::AspectRatioMode current_aspect_ratio_mode_;  ///< Current aspect ratio mode
    std::vector<orc::PreviewOutputInfo> available_outputs_;  ///< Cached outputs for current node
    
    // Preview update debouncing (for slider scrubbing)
    QTimer* preview_update_timer_;
    int pending_preview_index_;
    bool preview_update_pending_;
    bool last_update_was_sequential_;  // Track if last update was from next/prev buttons
    
    // Trigger progress tracking (now via coordinator signals)
    QProgressDialog* trigger_progress_dialog_;
    
    // Analysis progress dialogs per node (QPointer auto-nulls when deleted)
    std::unordered_map<orc::NodeID, QPointer<QProgressDialog>> dropout_progress_dialogs_;
    std::unordered_map<orc::NodeID, QPointer<QProgressDialog>> snr_progress_dialogs_;
    std::unordered_map<orc::NodeID, QPointer<QProgressDialog>> burst_level_progress_dialogs_;
};

#endif // MAINWINDOW_H
