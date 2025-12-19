/*
 * File:        dagnodeeditdialog.h
 * Module:      orc-gui
 * Purpose:     DAG node edit dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QComboBox>
#include <map>
#include <string>

/**
 * @brief Dialog for editing DAG node parameters
 */
class DAGNodeEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit DAGNodeEditDialog(const std::string& node_id,
                               const std::string& stage_name,
                               const std::map<std::string, std::string>& parameters,
                               const std::vector<std::string>& available_stages,
                               QWidget* parent = nullptr);
    
    std::map<std::string, std::string> getParameters() const;
    std::string getSelectedStage() const;

private:
    struct ParameterEditor {
        QString key;
        QLineEdit* value_edit;
    };
    
    std::vector<ParameterEditor> parameter_editors_;
    QComboBox* stage_combo_;
};

/**
 * @brief Dialog for adding a new DAG node
 */
class DAGNodeAddDialog : public QDialog {
    Q_OBJECT

public:
    explicit DAGNodeAddDialog(const std::vector<std::string>& available_stages,
                              QWidget* parent = nullptr);
    
    std::string getSelectedStage() const;
    std::string getNodeId() const;

private:
    QLineEdit* node_id_edit_;
    QComboBox* stage_combo_;
};
