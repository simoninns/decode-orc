// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QTabWidget>
#include <memory>
#include "fieldpreviewwidget.h"  // For PreviewMode enum

namespace orc {
    class VideoFieldRepresentation;
    class DAG;
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
    
    void loadSource(const QString& tbc_path);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onOpenTBC();
    void onOpenDAGEditor();
    void onFieldChanged(int field_index);
    void onNavigateField(int delta);
    void onPreviewModeChanged(int index);

private:
    void setupUI();
    void setupMenus();
    void setupToolbar();
    void updateWindowTitle();
    void updateFieldInfo();
    
    // Source management (single source for now)
    QString current_tbc_path_;
    QString current_source_name_;  // Display name for source
    int current_source_number_;     // Always 0 for now
    bool source_loaded_;
    std::shared_ptr<const orc::VideoFieldRepresentation> representation_;
    
    // UI components
    FieldPreviewWidget* preview_widget_;
    DAGEditorWindow* dag_editor_window_;
    QSlider* field_slider_;
    QLabel* field_info_label_;
    QToolBar* toolbar_;
    QComboBox* preview_mode_combo_;
    QAction* dag_editor_action_;  // Track to enable/disable
    
    // Navigation state
    int current_field_index_;
    int total_fields_;
    PreviewMode current_preview_mode_;
    
    // DAG state
    std::shared_ptr<orc::DAG> current_dag_;
};

#endif // MAINWINDOW_H
