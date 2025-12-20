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
#include "fieldpreviewwidget.h"  // For PreviewMode enum
#include "guiproject.h"

namespace orc {
    class VideoFieldRepresentation;
    class DAG;
    class DAGFieldRenderer;
}

class FieldPreviewWidget;
class DAGEditorWindow;
class QLabel;
class QSlider;
class QToolBar;
class QComboBox;

/**
 * Main window for orc-gui
 * 
 * Layout:
 * - Toolbar (file operations, source selection)
 * - Central preview area
 * - Bottom status/navigation bar
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
    void onOpenDAGEditor();
    void onFieldChanged(int field_index);
    void onNavigateField(int delta);
    void onPreviewModeChanged(int index);
    void onNodeSelectedForView(const std::string& node_id);
    void onDAGModified();

private:
    void setupUI();
    void setupMenus();
    void setupToolbar();
    void updateWindowTitle();
    void updateFieldInfo();
    void updateUIState();
    void updateFieldView();
    void updateDAGRenderer();
    
    // Settings helpers
    QString getLastProjectDirectory() const;
    void setLastProjectDirectory(const QString& path);
    
    // Project management
    GUIProject project_;
    std::unique_ptr<orc::DAGFieldRenderer> field_renderer_;
    std::string current_view_node_id_;  // Which node is being viewed
    
    // UI components
    FieldPreviewWidget* preview_widget_;
    QPointer<DAGEditorWindow> dag_editor_window_;  // Auto-nulls when window is deleted
    QSlider* field_slider_;
    QLabel* field_info_label_;
    QToolBar* toolbar_;
    QComboBox* preview_mode_combo_;
    QAction* dag_editor_action_;  // Track to enable/disable
    QAction* save_project_action_;
    QAction* save_project_as_action_;
    QAction* edit_project_action_;
    
    // Preview state (UI only - all data comes from core)
    PreviewMode current_preview_mode_;
};

#endif // MAINWINDOW_H
