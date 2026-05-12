/*
 * File:        pluginmanagerdialog.cpp
 * Module:      orc-gui
 * Purpose:     Plugin registry management dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "pluginmanagerdialog.h"

#include "presenters/include/project_presenter.h"
#include "presenters/include/project_presenter_types.h"

#include <algorithm>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QString>
#include <QCoreApplication>
#include <QFile>

#include <unordered_set>

namespace orc {

// Column indices for the registry table
static constexpr int COL_ID      = 0;
static constexpr int COL_PATH    = 1;
static constexpr int COL_VERSION = 2;
static constexpr int COL_SOURCE  = 3;
static constexpr int COL_ENABLED = 4;
static constexpr int NUM_COLS    = 5;
static constexpr int ROW_REGISTRY_ENTRY_ROLE = Qt::UserRole + 1;
static constexpr int ROW_IS_CORE_ROLE = Qt::UserRole + 2;
static constexpr int ROW_PATH_ROLE = Qt::UserRole + 3;
static constexpr int ROW_RELEASE_ASSET_URL_ROLE = Qt::UserRole + 4;

static const QStringList COLUMN_HEADERS = {
    "ID", "Path", "Version", "Source", "Enabled"
};

PluginManagerDialog::PluginManagerDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Plugin Manager");
    resize(900, 500);
    buildUI();
    refresh();
    captureInitialRegistrySnapshot();
}

void PluginManagerDialog::accept()
{
    if (!plugin_changes_made_) {
        QDialog::accept();
        return;
    }

    QMessageBox restart_box(this);
    restart_box.setWindowTitle("Restart Required");
    restart_box.setIcon(QMessageBox::Question);
    restart_box.setText(
        "Plugin changes require an application restart to take effect.\n\n"
        "Restart now?");
    restart_box.setStandardButtons(QMessageBox::NoButton);
    QPushButton* restart_btn = restart_box.addButton("Restart", QMessageBox::AcceptRole);
    QPushButton* cancel_btn = restart_box.addButton("Cancel", QMessageBox::RejectRole);
    restart_box.setDefaultButton(restart_btn);
    restart_box.exec();

    if (restart_box.clickedButton() == restart_btn) {
        QDialog::accept();
        QCoreApplication::quit();
        return;
    }

    if (restart_box.clickedButton() == cancel_btn) {
        // Keep dialog open so user can continue editing or choose Cancel.
        return;
    }
}

void PluginManagerDialog::reject()
{
    if (!plugin_changes_made_) {
        QDialog::reject();
        return;
    }

    const auto answer = QMessageBox::question(
        this,
        "Discard Plugin Changes",
        "Discard plugin changes made in this dialog session?",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        return;
    }

    QString restore_error;
    if (!restoreInitialRegistrySnapshot(&restore_error)) {
        QMessageBox::warning(
            this,
            "Discard Plugin Changes Failed",
            QString("Could not discard plugin changes: %1\n\n"
                    "Close cancelled to avoid leaving plugin state inconsistent.")
                .arg(restore_error));
        return;
    }

    plugin_changes_made_ = false;
    removed_paths_this_session_.clear();
    refresh();
    QDialog::reject();
}

void PluginManagerDialog::captureInitialRegistrySnapshot()
{
    const auto registry = orc::presenters::ProjectPresenter::readPluginRegistry();
    initial_registry_path_ = registry.registry_path;
    initial_registry_contents_.clear();
    initial_registry_exists_ = false;

    if (initial_registry_path_.empty()) {
        return;
    }

    QFile file(QString::fromStdString(initial_registry_path_));
    if (!file.exists()) {
        initial_registry_exists_ = false;
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        initial_registry_exists_ = false;
        return;
    }

    initial_registry_contents_ = file.readAll().toStdString();
    initial_registry_exists_ = true;
}

bool PluginManagerDialog::restoreInitialRegistrySnapshot(QString* error_message)
{
    if (initial_registry_path_.empty()) {
        if (error_message) {
            *error_message = "Registry path is unknown";
        }
        return false;
    }

    const QString path = QString::fromStdString(initial_registry_path_);
    QFile file(path);

    if (!initial_registry_exists_) {
        if (!file.exists()) {
            return true;
        }
        if (!file.remove()) {
            if (error_message) {
                *error_message = QString("Failed to remove '%1'").arg(path);
            }
            return false;
        }
        return true;
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error_message) {
            *error_message = QString("Failed to open '%1' for writing").arg(path);
        }
        return false;
    }

    const qint64 written = file.write(initial_registry_contents_.c_str(),
                                      static_cast<qint64>(initial_registry_contents_.size()));
    if (written != static_cast<qint64>(initial_registry_contents_.size())) {
        if (error_message) {
            *error_message = QString("Failed to write full snapshot to '%1'").arg(path);
        }
        return false;
    }

    return true;
}

void PluginManagerDialog::buildUI()
{
    auto* root_layout = new QVBoxLayout(this);

    // Registry path row
    auto* path_row = new QHBoxLayout();
    path_row->addWidget(new QLabel("Registry:"));
    registry_path_label_ = new QLabel();
    registry_path_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    path_row->addWidget(registry_path_label_, 1);
    root_layout->addLayout(path_row);

    // Plugin table
    table_ = new QTableWidget(0, NUM_COLS, this);
    table_->setHorizontalHeaderLabels(COLUMN_HEADERS);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto* header = table_->horizontalHeader();
    header->setSectionResizeMode(COL_ID, QHeaderView::Interactive);
    header->setSectionResizeMode(COL_PATH, QHeaderView::Interactive);
    header->setSectionResizeMode(COL_VERSION, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(COL_SOURCE, QHeaderView::Stretch);
    header->setSectionResizeMode(COL_ENABLED, QHeaderView::ResizeToContents);
    QFont header_font = header->font();
    header_font.setBold(false);
    header->setFont(header_font);
    for (int col = 0; col < NUM_COLS; ++col) {
        if (auto* header_item = table_->horizontalHeaderItem(col)) {
            header_item->setFont(header_font);
        }
    }
    table_->setColumnWidth(COL_ID, 260);
    table_->setColumnWidth(COL_PATH, 220);
    table_->verticalHeader()->setVisible(false);
    root_layout->addWidget(table_, 1);

    // Action buttons
    auto* button_row = new QHBoxLayout();
    add_button_     = new QPushButton("Add Plugin...");
    remove_button_  = new QPushButton("Remove");
    button_row->addWidget(add_button_);
    button_row->addWidget(remove_button_);
    button_row->addStretch();
    root_layout->addLayout(button_row);

    // Commit / cancel buttons
    auto* close_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root_layout->addWidget(close_box);

    // Note label
    auto* note_label = new QLabel("Note: Registry changes take effect on the next application launch.");
    note_label->setEnabled(false);
    root_layout->addWidget(note_label);

    // Connect signals
    connect(add_button_,     &QPushButton::clicked, this, &PluginManagerDialog::onAddPlugin);
    connect(remove_button_,  &QPushButton::clicked, this, &PluginManagerDialog::onRemovePlugin);
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &PluginManagerDialog::onSelectionChanged);
        connect(table_, &QTableWidget::itemChanged, this, &PluginManagerDialog::onTableItemChanged);
    connect(close_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(close_box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    onSelectionChanged();
}

void PluginManagerDialog::refresh()
{
    const auto registry = orc::presenters::ProjectPresenter::readPluginRegistry();
    const auto loaded_plugins = orc::presenters::ProjectPresenter::getLoadedPlugins();

    registry_path_label_->setText(
        registry.registry_path.empty()
            ? "<none>"
            : QString::fromStdString(registry.registry_path));

            refreshing_table_ = true;
    table_->setRowCount(0);
    std::unordered_set<std::string> seen_ids;
    std::unordered_set<std::string> seen_paths;

    for (const auto& e : registry.entries) {
        // Overlay runtime-discovered data (id, version) when the registry YAML
        // doesn't have it populated yet (e.g. freshly added remote plugins).
        const auto loaded_it = std::find_if(
            loaded_plugins.begin(), loaded_plugins.end(),
            [&e](const auto& lp) { return !lp.path.empty() && lp.path == e.path; });

        const std::string display_id = (!e.plugin_id.empty())
            ? e.plugin_id
            : (loaded_it != loaded_plugins.end() ? loaded_it->plugin_id : std::string());
        const std::string display_version = (!e.plugin_version.empty())
            ? e.plugin_version
            : (loaded_it != loaded_plugins.end() ? loaded_it->plugin_version : std::string());

        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* id_item = new QTableWidgetItem(QString::fromStdString(display_id));
        id_item->setData(ROW_REGISTRY_ENTRY_ROLE, true);
        id_item->setData(ROW_IS_CORE_ROLE, e.is_core_plugin);
        id_item->setData(ROW_PATH_ROLE, QString::fromStdString(e.path));
        id_item->setData(ROW_RELEASE_ASSET_URL_ROLE, QString::fromStdString(e.release_asset_url));
        table_->setItem(row, COL_ID, id_item);
        table_->setItem(row, COL_PATH,    new QTableWidgetItem(QString::fromStdString(e.path)));
        table_->setItem(row, COL_VERSION, new QTableWidgetItem(QString::fromStdString(display_version)));

        auto* enabled_item = new QTableWidgetItem();
        enabled_item->setData(ROW_REGISTRY_ENTRY_ROLE, true);
        enabled_item->setData(ROW_IS_CORE_ROLE, e.is_core_plugin);
        enabled_item->setFlags(e.is_core_plugin
            ? (Qt::ItemIsSelectable | Qt::ItemIsUserCheckable)
            : (Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled));
        enabled_item->setCheckState((e.enabled || e.is_core_plugin) ? Qt::Checked : Qt::Unchecked);
        const std::string source = e.is_core_plugin
            ? std::string("Core")
            : (!e.release_asset_url.empty()
                ? e.release_asset_url
                : (e.source_repo_url.empty() ? e.path : e.source_repo_url));
        table_->setItem(row, COL_SOURCE, new QTableWidgetItem(QString::fromStdString(source)));
        table_->setItem(row, COL_ENABLED, enabled_item);

        if (!display_id.empty()) {
            seen_ids.insert(display_id);
        }
        if (!e.path.empty()) {
            seen_paths.insert(e.path);
        }
    }

    // Also show runtime-loaded plugins that are not represented in the registry,
    // but skip any that were removed during this dialog session (they remain
    // in-memory until the next restart but should not re-appear in the list).
    for (const auto& plugin : loaded_plugins) {
        const bool id_seen = !plugin.plugin_id.empty() && seen_ids.count(plugin.plugin_id) > 0;
        const bool path_seen = !plugin.path.empty() && seen_paths.count(plugin.path) > 0;
        const bool removed = removed_paths_this_session_.count(plugin.path) > 0;
        if (id_seen || path_seen || removed) {
            continue;
        }

        const int row = table_->rowCount();
        table_->insertRow(row);

        auto* id_item = new QTableWidgetItem(QString::fromStdString(plugin.plugin_id));
        id_item->setData(ROW_REGISTRY_ENTRY_ROLE, false);
        id_item->setData(ROW_IS_CORE_ROLE, plugin.is_core_plugin);
        id_item->setData(ROW_PATH_ROLE, QString::fromStdString(plugin.path));
        id_item->setData(ROW_RELEASE_ASSET_URL_ROLE, QString());
        table_->setItem(row, COL_ID, id_item);
        table_->setItem(row, COL_PATH,    new QTableWidgetItem(QString::fromStdString(plugin.path)));
        table_->setItem(row, COL_VERSION, new QTableWidgetItem(QString::fromStdString(plugin.plugin_version)));

        auto* enabled_item = new QTableWidgetItem();
        enabled_item->setData(ROW_REGISTRY_ENTRY_ROLE, false);
        enabled_item->setData(ROW_IS_CORE_ROLE, plugin.is_core_plugin);
        enabled_item->setFlags(plugin.is_core_plugin
            ? (Qt::ItemIsSelectable | Qt::ItemIsUserCheckable)
            : (Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled));
        enabled_item->setCheckState(Qt::Checked);
        const std::string source = plugin.is_core_plugin ? std::string("Core") : plugin.path;
        table_->setItem(row, COL_SOURCE, new QTableWidgetItem(QString::fromStdString(source)));
        table_->setItem(row, COL_ENABLED, enabled_item);
    }

    refreshing_table_ = false;

    onSelectionChanged();
}

void PluginManagerDialog::onSelectionChanged()
{
    const bool has_selection = !table_->selectedItems().isEmpty();
    bool is_registry_entry = false;
    bool is_core_plugin = false;
    bool has_removal_identity = false;

    if (has_selection) {
        const int row = table_->currentRow();
        if (row >= 0) {
            if (auto* id_item = table_->item(row, COL_ID)) {
                is_registry_entry = id_item->data(ROW_REGISTRY_ENTRY_ROLE).toBool();
                is_core_plugin = id_item->data(ROW_IS_CORE_ROLE).toBool();
                const QString plugin_id = id_item->text();
                const QString path = id_item->data(ROW_PATH_ROLE).toString();
                const QString release_asset_url = id_item->data(ROW_RELEASE_ASSET_URL_ROLE).toString();
                has_removal_identity =
                    !plugin_id.isEmpty() || !path.isEmpty() || !release_asset_url.isEmpty();
            }
        }
    }

    const bool can_mutate_registry_entry =
        has_selection && is_registry_entry && !is_core_plugin && has_removal_identity;
    remove_button_->setEnabled(can_mutate_registry_entry);
}

void PluginManagerDialog::onTableItemChanged(QTableWidgetItem* item)
{
    if (!item || refreshing_table_ || item->column() != COL_ENABLED) {
        return;
    }

    const int row = item->row();
    auto* id_item = table_->item(row, COL_ID);
    if (!id_item) {
        refresh();
        return;
    }

    const bool is_registry_entry = id_item->data(ROW_REGISTRY_ENTRY_ROLE).toBool();
    const bool is_core_plugin = id_item->data(ROW_IS_CORE_ROLE).toBool();
    const QString plugin_id = id_item->text();
    const QString plugin_path = table_->item(row, COL_PATH) ? table_->item(row, COL_PATH)->text() : QString();
    const QString plugin_version = table_->item(row, COL_VERSION) ? table_->item(row, COL_VERSION)->text() : QString();
    const bool enabled = (item->checkState() == Qt::Checked);

    if (is_core_plugin) {
        refresh();
        return;
    }

    if (!is_registry_entry) {
        if (plugin_path.isEmpty()) {
            refresh();
            return;
        }

        orc::presenters::PluginRegistryEntryInfo entry_info;
        entry_info.path = plugin_path.toStdString();
        entry_info.plugin_id = plugin_id.toStdString();
        entry_info.plugin_version = plugin_version.toStdString();
        entry_info.artifact_source = "local_path";
        entry_info.trust_state = "untrusted";
        entry_info.enabled = true;

        const auto add_result = orc::presenters::ProjectPresenter::addPluginRegistryEntry(entry_info);

        if (!add_result.success &&
            add_result.error_message.find("already exists in the registry") == std::string::npos &&
            add_result.error_message.find("is already registered") == std::string::npos) {
            QMessageBox::warning(this,
                                 "Update Plugin Failed",
                                 QString::fromStdString(add_result.error_message));
            refresh();
            return;
        }
    }

    if (plugin_id.isEmpty()) {
        QMessageBox::warning(this,
                             "Update Plugin Failed",
                             "This plugin has no ID, so its enabled state cannot be changed.");
        refresh();
        return;
    }

    const auto result = orc::presenters::ProjectPresenter::setPluginRegistryEntryEnabled(
        plugin_id.toStdString(), enabled);

    if (!result.success) {
        QMessageBox::warning(this,
                             enabled ? "Enable Plugin Failed" : "Disable Plugin Failed",
                             QString::fromStdString(result.error_message));
    } else {
        plugin_changes_made_ = true;
    }

    refresh();
}

void PluginManagerDialog::onAddPlugin()
{
    const QStringList source_modes = {
        "Local plugin file",
        "Remote GitHub releases URL"
    };

    bool mode_ok = false;
    const QString source_mode = QInputDialog::getItem(
        this,
        "Add Plugin",
        "Plugin source:",
        source_modes,
        1,
        false,
        &mode_ok);

    if (!mode_ok || source_mode.isEmpty()) {
        return;
    }

    if (source_mode == "Local plugin file") {
    #if defined(_WIN32)
        const QString plugin_filter = "Plugin libraries (*.dll)";
    #elif defined(__APPLE__)
        const QString plugin_filter = "Plugin libraries (*.dylib)";
    #else
        const QString plugin_filter = "Plugin libraries (*.so)";
    #endif

        const QString path = QFileDialog::getOpenFileName(
            this,
            "Select Plugin Binary",
            QString(),
            plugin_filter);

        if (path.isEmpty()) {
            return;
        }

        orc::presenters::PluginRegistryEntryInfo entry_info;
        entry_info.artifact_source = "local_path";
        entry_info.path = path.toStdString();
        entry_info.enabled = true;
        entry_info.trust_state = "untrusted";

        const auto result = orc::presenters::ProjectPresenter::addPluginRegistryEntry(entry_info);

        if (!result.success) {
            QMessageBox::warning(this, "Add Plugin Failed",
                                 QString::fromStdString(result.error_message));
            return;
        }

        plugin_changes_made_ = true;

        refresh();
        return;
    }

    QInputDialog url_dialog(this);
    url_dialog.setWindowTitle("Add Remote Plugin");
    url_dialog.setLabelText("GitHub releases URL:");
    url_dialog.setInputMode(QInputDialog::TextInput);
    url_dialog.setTextEchoMode(QLineEdit::Normal);
    url_dialog.setTextValue("https://github.com/simoninns/orc-plugin_skeleton/releases");
    url_dialog.resize(900, url_dialog.sizeHint().height());
    url_dialog.setMinimumWidth(900);

    if (url_dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString releases_url = url_dialog.textValue();
    if (releases_url.trimmed().isEmpty()) {
        return;
    }

    const auto result = orc::presenters::ProjectPresenter::addPluginFromReleasesUrl(
        releases_url.trimmed().toStdString());

    if (!result.success) {
        QMessageBox::warning(this, "Add Plugin Failed",
                             QString::fromStdString(result.error_message));
        return;
    }

    plugin_changes_made_ = true;

    refresh();
}

void PluginManagerDialog::onRemovePlugin()
{
    const int row = table_->currentRow();
    if (row < 0) {
        return;
    }

    const auto* id_item = table_->item(row, COL_ID);
    if (!id_item) {
        return;
    }

    if (id_item->data(ROW_IS_CORE_ROLE).toBool()) {
        QMessageBox::information(this, "Remove Plugin",
                                 "Core plugins cannot be removed.");
        return;
    }

    const QString plugin_id = id_item->text();
    const QString plugin_path = id_item->data(ROW_PATH_ROLE).toString();
    const QString release_asset_url = id_item->data(ROW_RELEASE_ASSET_URL_ROLE).toString();

    const auto answer = QMessageBox::question(
        this, "Remove Plugin",
        QString("Remove selected plugin from the registry?") +
        (plugin_id.isEmpty() ? QString() : QString("\n\nID: %1").arg(plugin_id)),
        QMessageBox::Yes | QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        return;
    }

    const auto result = orc::presenters::ProjectPresenter::removePluginRegistryEntry(
        plugin_id.toStdString(),
        plugin_path.toStdString(),
        release_asset_url.toStdString());

    if (!result.success) {
        QMessageBox::warning(this, "Remove Plugin Failed",
                             QString::fromStdString(result.error_message));
        return;
    }

    // Track the removed path so it is suppressed from the loaded-plugins
    // fallback display during this session (the binary stays in-memory until
    // the next restart, but the user should see it gone immediately).
    if (!plugin_path.isEmpty()) {
        removed_paths_this_session_.insert(plugin_path.toStdString());
    }
    plugin_changes_made_ = true;

    refresh();
}

} // namespace orc
