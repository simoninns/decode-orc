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
#include <memory>
#include "guiproject.h"
#include "preview_renderer.h"  // For PreviewOutputType
#include "orcgraphmodel.h"
#include "orcgraphicsscene.h"

class OrcGraphicsView;
class PreviewDialog;

namespace orc {
    class VideoFieldRepresentation;
    class DAG;
    class AnalysisTool;
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
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    
    // Project operations
    void newProject();
    void openProject(const QString& filename);
    void saveProject();
    void saveProjectAs();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onEditProject();
    void onPreviewIndexChanged(int index);
    void onNavigatePreview(int delta);
    void onPreviewModeChanged(int index);
    void onAspectRatioModeChanged(int index);
    void onNodeSelectedForView(const std::string& node_id);
    void onDAGModified();
    void onExportPNG();
    void onNodeContextMenu(QtNodes::NodeId nodeId, const QPointF& pos);
    void onArrangeDAGToGrid();
    void onQtNodeSelected(QtNodes::NodeId nodeId);
    void onInspectStage(const std::string& node_id);

private:
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
    void loadProjectDAG();  // Load DAG into embedded viewer
    void onEditParameters(const std::string& node_id);
    void onTriggerStage(const std::string& node_id);
    void runAnalysisForNode(orc::AnalysisTool* tool, const std::string& node_id, const std::string& stage_name);
    
    // Settings helpers
    QString getLastProjectDirectory() const;
    void setLastProjectDirectory(const QString& path);
    void saveSettings();
    void restoreSettings();
    
    // Project management
    GUIProject project_;
    std::unique_ptr<orc::PreviewRenderer> preview_renderer_;
    std::string current_view_node_id_;  // Which node is being viewed
    QtNodes::NodeId last_selected_qt_node_id_;  // Last selected node in DAG for DEL key
    
    // UI components
    PreviewDialog* preview_dialog_;
    OrcGraphModel* dag_model_;
    OrcGraphicsView* dag_view_;
    OrcGraphicsScene* dag_scene_;
    QToolBar* toolbar_;
    QAction* save_project_action_;
    QAction* save_project_as_action_;
    QAction* edit_project_action_;
    QAction* show_preview_action_;
    QAction* auto_show_preview_action_;
    QAction* export_png_action_;
    
    // Preview state (UI only - all data comes from core)
    orc::PreviewOutputType current_output_type_;
    std::string current_option_id_;  ///< Current option ID for PreviewableStage rendering
    std::vector<orc::PreviewOutputInfo> available_outputs_;  ///< Cached outputs for current node
    
    // Preview update throttling
    QTimer* preview_update_timer_;
    int pending_preview_index_;
    bool preview_update_pending_;
    qint64 last_preview_update_time_;  // Timestamp of last update for throttling
    bool last_update_was_sequential_;  // Track if last update was from next/prev buttons
};

#endif // MAINWINDOW_H
