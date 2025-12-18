// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include "mainwindow.h"
#include "fieldpreviewwidget.h"
#include "dageditorwindow.h"
#include "tbc_video_field_representation.h"
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
    , dag_editor_window_(nullptr)
    , field_slider_(nullptr)
    , field_info_label_(nullptr)
    , toolbar_(nullptr)
    , preview_mode_combo_(nullptr)
    , dag_editor_action_(nullptr)
    , current_source_number_(0)
    , source_loaded_(false)
    , current_field_index_(0)
    , total_fields_(0)
    , current_preview_mode_(PreviewMode::SingleField)
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
    // Central widget with field preview
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    
    // Field preview widget
    preview_widget_ = new FieldPreviewWidget(this);
    layout->addWidget(preview_widget_, 1);
    
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
    
    // Tools menu
    auto* tools_menu = menuBar()->addMenu("&Tools");
    
    dag_editor_action_ = tools_menu->addAction("&DAG Editor...");
    dag_editor_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    dag_editor_action_->setEnabled(false);  // Disabled until source is loaded
    connect(dag_editor_action_, &QAction::triggered, this, &MainWindow::onOpenDAGEditor);
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
    
    loadSource(filename);
}

void MainWindow::loadSource(const QString& tbc_path)
{
    try {
        // Load TBC using the core library
        auto repr = orc::create_tbc_representation(
            tbc_path.toStdString(),
            tbc_path.toStdString() + ".db"
        );
        
        if (!repr) {
            QMessageBox::critical(this, "Error", "Failed to load TBC file");
            return;
        }
        
        representation_ = repr;
        current_tbc_path_ = tbc_path;
        
        // Extract source name from filename
        QFileInfo file_info(tbc_path);
        current_source_name_ = file_info.baseName();
        current_source_number_ = 0;  // Always 0 for now
        source_loaded_ = true;
        
        // Get field range
        auto range = representation_->field_range();
        total_fields_ = range.size();
        current_field_index_ = 0;
        
        // Update UI
        field_slider_->setEnabled(true);
        field_slider_->setRange(0, total_fields_ - 1);
        field_slider_->setValue(0);
        
        // Enable DAG editor now that source is loaded
        if (dag_editor_action_) {
            dag_editor_action_->setEnabled(true);
        }
        
        // Update DAG editor if already open
        if (dag_editor_window_) {
            dag_editor_window_->setSourceInfo(current_source_number_, current_source_name_);
        }
        
        // Set representation in preview widget
        preview_widget_->setRepresentation(representation_);
        preview_widget_->setFieldIndex(range.start.value());
        
        updateWindowTitle();
        updateFieldInfo();
        
        statusBar()->showMessage(QString("Loaded: %1 fields from source \"%2\"")
            .arg(total_fields_).arg(current_source_name_));
        
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
    
    // In frame view modes, move by 2 fields at a time
    int step = (current_preview_mode_ == PreviewMode::Frame_EvenOdd ||
                current_preview_mode_ == PreviewMode::Frame_OddEven) ? 2 : 1;
    
    int new_index = current_field_index_ + (delta * step);
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
    
    current_preview_mode_ = mode;
    
    // Set slider step size based on preview mode
    // In frame modes, slider moves by 2 fields at a time
    if (mode == PreviewMode::Frame_EvenOdd || mode == PreviewMode::Frame_OddEven) {
        field_slider_->setSingleStep(2);
        field_slider_->setPageStep(10);  // 5 frames
    } else {
        field_slider_->setSingleStep(1);
        field_slider_->setPageStep(10);
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

void MainWindow::onOpenDAGEditor()
{
    // Create DAG editor window if it doesn't exist
    if (!dag_editor_window_) {
        dag_editor_window_ = new DAGEditorWindow(this);
        
        // Set source info if loaded
        if (source_loaded_) {
            dag_editor_window_->setSourceInfo(current_source_number_, current_source_name_);
        }
    }
    
    // Show the window
    dag_editor_window_->show();
    dag_editor_window_->raise();
    dag_editor_window_->activateWindow();
}
