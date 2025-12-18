// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#ifndef DAGEDITORWINDOW_H
#define DAGEDITORWINDOW_H

#include <QMainWindow>
#include <QString>
#include <memory>

class DAGViewerWidget;

/**
 * @brief Separate window for DAG editing with its own menubar
 */
class DAGEditorWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit DAGEditorWindow(QWidget *parent = nullptr);
    ~DAGEditorWindow() = default;
    
    DAGViewerWidget* dagViewer() { return dag_viewer_; }
    
    void setSourceInfo(int source_number, const QString& source_name);

private slots:
    void onLoadDAG();
    void onSaveDAG();
    void onNodeSelected(const std::string& node_id);
    void onChangeNodeType(const std::string& node_id);
    void onEditParameters(const std::string& node_id);

private:
    void setupMenus();
    
    DAGViewerWidget* dag_viewer_;
};

#endif // DAGEDITORWINDOW_H
