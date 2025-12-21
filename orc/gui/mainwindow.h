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

namespace orc {
    class VideoFieldRepresentation;
    class DAG;
}

class FieldPreviewWidget;
class DAGViewerWidget;
class QLabel;
class QSlider;
class QToolBar;
class QComboBox;
class QSplitter;

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
    
    // Settings helpers
    QString getLastProjectDirectory() const;
    void setLastProjectDirectory(const QString& path);
    
    // Project management
    GUIProject project_;
    std::unique_ptr<orc::PreviewRenderer> preview_renderer_;
    std::string current_view_node_id_;  // Which node is being viewed
    
    // UI components
    FieldPreviewWidget* preview_widget_;
    DAGViewerWidget* dag_viewer_;
    QSplitter* main_splitter_;
    QSlider* preview_slider_;
    QLabel* preview_info_label_;
    QLabel* slider_min_label_;
    QLabel* slider_max_label_;
    QToolBar* toolbar_;
    QComboBox* preview_mode_combo_;
    QComboBox* aspect_ratio_combo_;
    QAction* save_project_action_;
    QAction* save_project_as_action_;
    QAction* edit_project_action_;
    QAction* export_png_action_;
    
    // Preview state (UI only - all data comes from core)
    orc::PreviewOutputType current_output_type_;
    std::vector<orc::PreviewOutputInfo> available_outputs_;  ///< Cached outputs for current node
};

#endif // MAINWINDOW_H
