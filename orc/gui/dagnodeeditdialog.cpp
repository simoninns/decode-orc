/******************************************************************************
 * dagnodeeditdialog.cpp
 *
 * Dialog for editing DAG node parameters implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#include "dagnodeeditdialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>

// ============================================================================
// DAGNodeEditDialog Implementation
// ============================================================================

DAGNodeEditDialog::DAGNodeEditDialog(const std::string& node_id,
                                     const std::string& stage_name,
                                     const std::map<std::string, std::string>& parameters,
                                     const std::vector<std::string>& available_stages,
                                     QWidget* parent)
    : QDialog(parent)
    , stage_combo_(nullptr)
{
    setWindowTitle("Edit Node");
    setMinimumWidth(400);
    
    auto* layout = new QVBoxLayout(this);
    
    // Node info and stage selector
    auto* info_layout = new QFormLayout();
    auto* node_id_label = new QLabel(QString::fromStdString(node_id));
    
    info_layout->addRow("Node ID:", node_id_label);
    
    // Stage type selector (editable)
    stage_combo_ = new QComboBox();
    for (const auto& stage : available_stages) {
        stage_combo_->addItem(QString::fromStdString(stage));
    }
    // Set current stage
    int current_index = stage_combo_->findText(QString::fromStdString(stage_name));
    if (current_index >= 0) {
        stage_combo_->setCurrentIndex(current_index);
    }
    info_layout->addRow("Stage Type:", stage_combo_);
    
    layout->addLayout(info_layout);
    
    // Parameters (editable)
    auto* param_layout = new QFormLayout();
    
    for (const auto& [key, value] : parameters) {
        auto* value_edit = new QLineEdit(QString::fromStdString(value));
        param_layout->addRow(QString::fromStdString(key) + ":", value_edit);
        
        parameter_editors_.push_back({
            QString::fromStdString(key),
            value_edit
        });
    }
    
    if (parameters.empty()) {
        param_layout->addRow(new QLabel("(No parameters)"));
    }
    
    layout->addLayout(param_layout);
    
    // Buttons
    auto* button_box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel
    );
    connect(button_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    
    layout->addWidget(button_box);
}

std::map<std::string, std::string> DAGNodeEditDialog::getParameters() const
{
    std::map<std::string, std::string> result;
    
    for (const auto& editor : parameter_editors_) {
        result[editor.key.toStdString()] = editor.value_edit->text().toStdString();
    }
    
    return result;
}

std::string DAGNodeEditDialog::getSelectedStage() const
{
    if (stage_combo_) {
        return stage_combo_->currentText().toStdString();
    }
    return "";
}

// ============================================================================
// DAGNodeAddDialog Implementation
// ============================================================================

DAGNodeAddDialog::DAGNodeAddDialog(const std::vector<std::string>& available_stages,
                                   QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Add Node");
    setMinimumWidth(350);
    
    auto* layout = new QVBoxLayout(this);
    
    // Form for node details
    auto* form_layout = new QFormLayout();
    
    // Node ID
    node_id_edit_ = new QLineEdit();
    node_id_edit_->setPlaceholderText("Enter unique node ID");
    form_layout->addRow("Node ID:", node_id_edit_);
    
    // Stage selection
    stage_combo_ = new QComboBox();
    for (const auto& stage : available_stages) {
        stage_combo_->addItem(QString::fromStdString(stage));
    }
    form_layout->addRow("Stage:", stage_combo_);
    
    layout->addLayout(form_layout);
    
    // Buttons
    auto* button_box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel
    );
    connect(button_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    
    layout->addWidget(button_box);
}

std::string DAGNodeAddDialog::getSelectedStage() const
{
    return stage_combo_->currentText().toStdString();
}

std::string DAGNodeAddDialog::getNodeId() const
{
    return node_id_edit_->text().toStdString();
}
