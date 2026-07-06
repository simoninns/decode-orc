/*
 * File:        pluginmanagerdialog.h
 * Module:      orc-gui
 * Purpose:     Plugin registry management dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_GUI_PLUGINMANAGERDIALOG_H
#define ORC_GUI_PLUGINMANAGERDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <string>
#include <unordered_set>

namespace orc {

/**
 * @brief Dialog for managing the persistent plugin registry.
 *
 * Shows registered plugins and their load/filesystem status, and allows the
 * user to add, remove, enable, and disable plugins via the ProjectPresenter
 * mutation API.  Registry changes take effect on the next application launch.
 */
class PluginManagerDialog : public QDialog {
  Q_OBJECT

 public:
  explicit PluginManagerDialog(QWidget* parent = nullptr);
  ~PluginManagerDialog() override = default;

 protected:
  void accept() override;
  void reject() override;

 private slots:
  void onAddPlugin();
  void onRemovePlugin();
  void onSelectionChanged();
  void onTableItemChanged(QTableWidgetItem* item);

 private:
  void buildUI();
  void refresh();
  void captureInitialRegistrySnapshot();
  bool restoreInitialRegistrySnapshot(QString* error_message);

  QLabel* registry_path_label_;
  QTableWidget* table_;
  QPushButton* add_button_;
  QPushButton* remove_button_;
  bool refreshing_table_ = false;
  bool plugin_changes_made_ = false;
  std::unordered_set<std::string> removed_paths_this_session_;
  std::string initial_registry_path_;
  std::string initial_registry_contents_;
  bool initial_registry_exists_ = false;
};

}  // namespace orc

#endif  // ORC_GUI_PLUGINMANAGERDIALOG_H
