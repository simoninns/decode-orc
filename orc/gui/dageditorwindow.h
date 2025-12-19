/*
 * File:        dageditorwindow.h
 * Module:      orc-gui
 * Purpose:     DAG editor window
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef DAGEDITORWINDOW_H
#define DAGEDITORWINDOW_H

#include <QMainWindow>
#include <QString>
#include <memory>

class DAGViewerWidget;
class GUIProject;

/**
 * @brief Separate window for DAG editing
 * 
 * Edits the DAG within a GUIProject. All modifications update the project
 * and mark it as modified. The parent window is responsible for saving
 * the project (which includes the DAG).
 */
class DAGEditorWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit DAGEditorWindow(QWidget *parent = nullptr);
    ~DAGEditorWindow() override;
    
    DAGViewerWidget* dagViewer() { return dag_viewer_; }
    
    void setProject(GUIProject* project);
    void setSourceInfo(int source_number, const QString& source_name);
    void loadProjectDAG();

signals:
    void projectModified();  // Emitted when DAG is modified

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNodeSelected(const std::string& node_id);
    void onEditParameters(const std::string& node_id);
    void onTriggerStage(const std::string& node_id);
    void updateWindowTitle();

private:
    void setupMenus();
    
    DAGViewerWidget* dag_viewer_;
    GUIProject* project_;  // Not owned
};

#endif // DAGEDITORWINDOW_H
