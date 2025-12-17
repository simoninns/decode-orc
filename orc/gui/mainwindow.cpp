// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include "mainwindow.h"
#include "fieldpreviewwidget.h"
#include "dagviewerwidget.h"
#include "dagnodeeditdialog.h"
#include "stageparameterdialog.h"
#include "tbc_video_field_representation.h"
#include "passthrough_stage.h"
#include "dropout_correct_stage.h"
#include "../core/include/dag_executor.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QMessageBox>
#include <QKeyEvent>
#include <QComboBox>
#include <QTabWidget>
#include <QSplitter>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , preview_widget_(nullptr)
    , dag_viewer_(nullptr)
    , field_slider_(nullptr)
    , field_info_label_(nullptr)
    , toolbar_(nullptr)
    , preview_mode_combo_(nullptr)
    , current_field_index_(0)
    , total_fields_(0)
    , current_dag_(nullptr)
{
    setupUI();
    setupMenus();
    setupToolbar();
    
    setWindowTitle("orc-gui");
    resize(1280, 720);
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUI()
{
    // Central widget with vertical layout
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    
    // Vertical splitter: DAG viewer on top, field preview on bottom
    auto* splitter = new QSplitter(Qt::Vertical, this);
    
    // DAG viewer
    dag_viewer_ = new DAGViewerWidget(this);
    connect(dag_viewer_, &DAGViewerWidget::nodeSelected,
            this, &MainWindow::onNodeSelected);
    connect(dag_viewer_, &DAGViewerWidget::nodeDoubleClicked,
            this, &MainWindow::onNodeDoubleClicked);
    connect(dag_viewer_, &DAGViewerWidget::changeNodeTypeRequested,
            this, &MainWindow::onChangeNodeType);
    connect(dag_viewer_, &DAGViewerWidget::editParametersRequested,
            this, &MainWindow::onEditParameters);
    connect(dag_viewer_, &DAGViewerWidget::addNodeRequested,
            this, &MainWindow::onAddNodeRequested);
    connect(dag_viewer_, &DAGViewerWidget::deleteNodeRequested,
            this, &MainWindow::onDeleteNodeRequested);
    
    splitter->addWidget(dag_viewer_);
    
    // Field preview widget
    preview_widget_ = new FieldPreviewWidget(this);
    splitter->addWidget(preview_widget_);
    
    // Set initial sizes (40% DAG, 60% preview)
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    
    layout->addWidget(splitter, 1);
    
    // Navigation controls at bottom
    auto* nav_layout = new QHBoxLayout();
    
    // Preview mode selector
    preview_mode_combo_ = new QComboBox(this);
    preview_mode_combo_->addItem("Field View");
    preview_mode_combo_->addItem("Frame (Even+Odd)");
    preview_mode_combo_->addItem("Frame (Odd+Even)");
    preview_mode_combo_->setCurrentIndex(0);
    connect(preview_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onPreviewModeChanged);
    nav_layout->addWidget(preview_mode_combo_);
    
    // Spacer
    nav_layout->addSpacing(20);
    
    // Previous field button
    auto* prev_button = new QPushButton("<", this);
    prev_button->setMaximumWidth(50);
    prev_button->setAutoRepeat(true);
    prev_button->setAutoRepeatDelay(100);  // 100ms initial delay
    prev_button->setAutoRepeatInterval(10);  // 10ms repeat interval (very fast)
    connect(prev_button, &QPushButton::clicked, this, [this]() { onNavigateField(-1); });
    nav_layout->addWidget(prev_button);
    
    // Next field button
    auto* next_button = new QPushButton(">", this);
    next_button->setMaximumWidth(50);
    next_button->setAutoRepeat(true);
    next_button->setAutoRepeatDelay(100);  // 100ms initial delay
    next_button->setAutoRepeatInterval(10);  // 10ms repeat interval (very fast)
    connect(next_button, &QPushButton::clicked, this, [this]() { onNavigateField(1); });
    nav_layout->addWidget(next_button);
    
    // Field slider
    field_slider_ = new QSlider(Qt::Horizontal, this);
    field_slider_->setEnabled(false);
    connect(field_slider_, &QSlider::valueChanged, this, &MainWindow::onFieldChanged);
    nav_layout->addWidget(field_slider_, 1);
    
    // Field info label
    field_info_label_ = new QLabel("No TBC loaded", this);
    field_info_label_->setMinimumWidth(200);
    nav_layout->addWidget(field_info_label_);
    
    layout->addLayout(nav_layout);
    
    setCentralWidget(central);
    
    // Status bar
    statusBar()->showMessage("Ready");
}

void MainWindow::setupMenus()
{
    auto* file_menu = menuBar()->addMenu("&File");
    
    auto* open_action = file_menu->addAction("&Open TBC...");
    open_action->setShortcut(QKeySequence::Open);
    connect(open_action, &QAction::triggered, this, &MainWindow::onOpenTBC);
    
    file_menu->addSeparator();
    
    auto* quit_action = file_menu->addAction("&Quit");
    quit_action->setShortcut(QKeySequence::Quit);
    connect(quit_action, &QAction::triggered, this, &QWidget::close);
    
    // DAG menu
    auto* dag_menu = menuBar()->addMenu("&DAG");
    
    auto* load_dag_action = dag_menu->addAction("&Load DAG...");
    load_dag_action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(load_dag_action, &QAction::triggered, this, &MainWindow::onLoadDAG);
    
    auto* save_dag_action = dag_menu->addAction("&Save DAG...");
    save_dag_action->setShortcut(QKeySequence::Save);
    connect(save_dag_action, &QAction::triggered, this, &MainWindow::onSaveDAG);
}

void MainWindow::setupToolbar()
{
    toolbar_ = addToolBar("Main");
    // Toolbar can be used for other controls later
}

void MainWindow::onOpenTBC()
{
    QString filename = QFileDialog::getOpenFileName(
        this,
        "Open TBC File",
        QString(),
        "TBC Files (*.tbc);;All Files (*)"
    );
    
    if (filename.isEmpty()) {
        return;
    }
    
    try {
        // Load TBC using the core library
        auto repr = orc::create_tbc_representation(
            filename.toStdString(),
            filename.toStdString() + ".db"
        );
        
        if (!repr) {
            QMessageBox::critical(this, "Error", "Failed to load TBC file");
            return;
        }
        
        representation_ = repr;
        current_tbc_path_ = filename;
        
        // Get field range
        auto range = representation_->field_range();
        total_fields_ = range.size();
        current_field_index_ = 0;
        
        // Update UI
        field_slider_->setEnabled(true);
        field_slider_->setRange(0, total_fields_ - 1);
        field_slider_->setValue(0);
        
        // Set representation in preview widget
        preview_widget_->setRepresentation(representation_);
        preview_widget_->setFieldIndex(range.start.value());
        
        updateWindowTitle();
        updateFieldInfo();
        
        statusBar()->showMessage(QString("Loaded: %1 fields").arg(total_fields_));
        
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error", 
            QString("Failed to load TBC: %1").arg(e.what()));
    }
}

void MainWindow::onFieldChanged(int field_index)
{
    if (!representation_ || field_index < 0 || field_index >= total_fields_) {
        return;
    }
    
    current_field_index_ = field_index;
    
    auto range = representation_->field_range();
    orc::FieldID field_id = range.start + field_index;
    
    preview_widget_->setFieldIndex(field_id.value());
    updateFieldInfo();
}

void MainWindow::onNavigateField(int delta)
{
    if (!representation_) {
        return;
    }
    
    int new_index = current_field_index_ + delta;
    if (new_index >= 0 && new_index < total_fields_) {
        field_slider_->setValue(new_index);
    }
}

void MainWindow::onPreviewModeChanged(int index)
{
    PreviewMode mode;
    switch (index) {
        case 0:
            mode = PreviewMode::SingleField;
            break;
        case 1:
            mode = PreviewMode::Frame_EvenOdd;
            break;
        case 2:
            mode = PreviewMode::Frame_OddEven;
            break;
        default:
            mode = PreviewMode::SingleField;
    }
    
    preview_widget_->setPreviewMode(mode);
}

void MainWindow::updateWindowTitle()
{
    if (current_tbc_path_.isEmpty()) {
        setWindowTitle("orc-gui");
    } else {
        QFileInfo info(current_tbc_path_);
        setWindowTitle(QString("orc-gui - %1").arg(info.fileName()));
    }
}

void MainWindow::updateFieldInfo()
{
    if (!representation_ || total_fields_ == 0) {
        field_info_label_->setText("No TBC loaded");
        return;
    }
    
    auto range = representation_->field_range();
    orc::FieldID field_id = range.start + current_field_index_;
    
    PreviewMode mode = preview_widget_->previewMode();
    
    if (mode == PreviewMode::SingleField) {
        // Single field: show one field ID
        field_info_label_->setText(
            QString("Field %1 / %2 (ID: %3)")
                .arg(current_field_index_ + 1)
                .arg(total_fields_)
                .arg(field_id.value())
        );
    } else {
        // Frame view: show both field IDs
        orc::FieldID next_field_id = field_id + 1;
        if (current_field_index_ + 1 < total_fields_) {
            field_info_label_->setText(
                QString("Field %1-%2 / %3 (IDs: %4+%5)")
                    .arg(current_field_index_ + 1)
                    .arg(current_field_index_ + 2)
                    .arg(total_fields_)
                    .arg(field_id.value())
                    .arg(next_field_id.value())
            );
        } else {
            // Last field, can't make frame
            field_info_label_->setText(
                QString("Field %1 / %2 (ID: %3) [no next field]")
                    .arg(current_field_index_ + 1)
                    .arg(total_fields_)
                    .arg(field_id.value())
            );
        }
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (!representation_) {
        QMainWindow::keyPressEvent(event);
        return;
    }
    
    switch (event->key()) {
        case Qt::Key_Left:
            onNavigateField(-1);
            break;
        case Qt::Key_Right:
            onNavigateField(1);
            break;
        case Qt::Key_Home:
            field_slider_->setValue(0);
            break;
        case Qt::Key_End:
            field_slider_->setValue(total_fields_ - 1);
            break;
        case Qt::Key_PageUp:
            onNavigateField(-10);
            break;
        case Qt::Key_PageDown:
            onNavigateField(10);
            break;
        default:
            QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::onLoadDAG()
{
    QString filename = QFileDialog::getOpenFileName(
        this,
        "Load DAG",
        "",
        "YAML Files (*.yaml *.yml);;All Files (*)"
    );
    
    if (filename.isEmpty()) {
        return;
    }
    
    try {
        auto dag = orc::dag_serialization::load_dag_from_yaml(filename.toStdString());
        dag_viewer_->importDAG(dag);
        statusBar()->showMessage(QString("Loaded DAG: %1").arg(filename));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error Loading DAG",
            QString("Failed to load DAG: %1").arg(e.what()));
    }
}

void MainWindow::onSaveDAG()
{
    QString filename = QFileDialog::getSaveFileName(
        this,
        "Save DAG",
        "",
        "YAML Files (*.yaml *.yml);;All Files (*)"
    );
    
    if (filename.isEmpty()) {
        return;
    }
    
    // Ensure .yaml extension
    if (!filename.endsWith(".yaml", Qt::CaseInsensitive) && 
        !filename.endsWith(".yml", Qt::CaseInsensitive)) {
        filename += ".yaml";
    }
    
    try {
        auto dag = dag_viewer_->exportDAG();
        orc::dag_serialization::save_dag_to_yaml(dag, filename.toStdString());
        statusBar()->showMessage(QString("Saved DAG: %1").arg(filename));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error Saving DAG",
            QString("Failed to save DAG: %1").arg(e.what()));
    }
}

void MainWindow::onNodeSelected(const std::string& node_id)
{
    statusBar()->showMessage(QString("Selected node: %1").arg(QString::fromStdString(node_id)));
    
    // Update preview to show output from selected node
    // For now, we show the original TBC since DAG execution isn't implemented yet
    // TODO: When DAG execution is implemented:
    //   - If node_id == "START", show original TBC
    //   - Otherwise, execute DAG up to selected node and show its output
    
    // Currently just showing status - actual preview update would go here
    if (node_id == "START") {
        statusBar()->showMessage("Selected START node - showing original TBC");
    } else {
        statusBar()->showMessage(
            QString("Selected node: %1 - showing output (DAG execution not yet implemented)")
                .arg(QString::fromStdString(node_id))
        );
    }
}

void MainWindow::onNodeDoubleClicked(const std::string& node_id)
{
    // Double-click now just selects the node
    // Use context menu for editing
    statusBar()->showMessage(
        QString("Node selected: %1 (right-click for options)")
            .arg(QString::fromStdString(node_id))
    );
}

void MainWindow::onChangeNodeType(const std::string& node_id)
{
    // Get available stage types
    std::vector<std::string> available_stages = {
        "Passthrough",
        "Dropout Correct"
    };
    
    // Get current stage type from viewer
    std::string current_stage = dag_viewer_->getNodeStageType(node_id);
    
    // For now, use empty parameters
    std::map<std::string, std::string> parameters;
    
    // Open stage selection dialog
    DAGNodeEditDialog dialog(
        node_id,
        current_stage,
        parameters,
        available_stages,
        this
    );
    
    if (dialog.exec() == QDialog::Accepted) {
        std::string new_stage = dialog.getSelectedStage();
        
        // Update node stage type in viewer
        if (new_stage != current_stage) {
            dag_viewer_->setNodeStageType(node_id, new_stage);
            statusBar()->showMessage(
                QString("Changed node %1 to stage type: %2")
                    .arg(QString::fromStdString(node_id))
                    .arg(QString::fromStdString(new_stage))
            );
        }
    }
}

void MainWindow::onEditParameters(const std::string& node_id)
{
    // Get current stage type
    std::string stage_name = dag_viewer_->getNodeStageType(node_id);
    
    // Create temporary stage instance to get parameter info
    if (stage_name == "Dropout Correct") {
        orc::DropoutCorrectStage temp_stage;
        auto descriptors = temp_stage.get_parameter_descriptors();
        
        // Get stored parameters for this node, or use defaults
        auto stored_params = dag_viewer_->getNodeParameters(node_id);
        auto current_params = stored_params.empty() ? temp_stage.get_parameters() : stored_params;
        
        // Show parameter dialog
        StageParameterDialog param_dialog(
            stage_name,
            descriptors,
            current_params,
            this
        );
        
        if (param_dialog.exec() == QDialog::Accepted) {
            auto new_params = param_dialog.get_values();
            
            // Store parameters with the node
            dag_viewer_->setNodeParameters(node_id, new_params);
            
            statusBar()->showMessage(
                QString("Updated parameters for node %1")
                    .arg(QString::fromStdString(node_id))
            );
        }
    } else if (stage_name == "Passthrough") {
        // Passthrough has no parameters - shouldn't get here due to menu being disabled
        QMessageBox::information(this, "No Parameters",
            "Passthrough stage has no configurable parameters.");
    }
}

void MainWindow::onAddNodeRequested(const std::string& after_node_id)
{
    // Get available stages
    std::vector<std::string> available_stages = {
        "dropout_correct",
        "chroma_decode",
        "filter",
        "transform"
    };
    
    DAGNodeAddDialog dialog(available_stages, this);
    
    if (dialog.exec() == QDialog::Accepted) {
        std::string new_node_id = dialog.getNodeId();
        std::string stage_name = dialog.getSelectedStage();
        
        // TODO: Add node to DAG and refresh view
        QMessageBox::information(this, "Node Added",
            QString("Added node '%1' with stage '%2'")
                .arg(QString::fromStdString(new_node_id))
                .arg(QString::fromStdString(stage_name)));
    }
}

void MainWindow::onDeleteNodeRequested(const std::string& node_id)
{
    auto reply = QMessageBox::question(this, "Delete Node",
        QString("Delete node '%1'?").arg(QString::fromStdString(node_id)),
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        // TODO: Remove node from DAG and refresh view
        QMessageBox::information(this, "Node Deleted",
            QString("Deleted node: %1").arg(QString::fromStdString(node_id)));
    }
}
