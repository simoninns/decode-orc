/*
 * File:        previewdialog.cpp
 * Module:      orc-gui
 * Purpose:     Separate preview window for field/frame viewing
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "previewdialog.h"
#include "fieldpreviewwidget.h"
#include "linescopedialog.h"
#include "fieldtimingdialog.h"
#include "preview/vectorscope_dialog.h"
#include "logging.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QStatusBar>
#include <QPushButton>
#include <QCloseEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QMenu>
#include <QSettings>
#include <QGuiApplication>
#include <QFontMetrics>
#include <QScreen>
#include <QSignalBlocker>
#include <algorithm>

namespace {

constexpr const char* kLineScopeViewId = "preview.linescope";
constexpr const char* kFieldTimingViewId = "preview.field_timing";
constexpr const char* kComponentVectorscopeViewId = "preview.vectorscope";

orc::ParameterValue resolveTweakParameterValue(
    const orc::ParameterDescriptor& desc,
    const std::map<std::string, orc::ParameterValue>& values)
{
    auto val_it = values.find(desc.name);
    if (val_it != values.end()) {
        return val_it->second;
    }

    if (desc.constraints.default_value.has_value()) {
        return *desc.constraints.default_value;
    }

    switch (desc.type) {
        case orc::ParameterType::INT32:
            return int32_t{0};
        case orc::ParameterType::UINT32:
            return uint32_t{0};
        case orc::ParameterType::DOUBLE:
            return double{0.0};
        case orc::ParameterType::BOOL:
            return false;
        case orc::ParameterType::STRING:
        case orc::ParameterType::FILE_PATH:
            return std::string{};
    }

    return std::string{};
}

} // namespace

PreviewDialog::PreviewDialog(QWidget *parent)
    : QDialog(parent)
    , line_scope_dialog_(nullptr)
    , field_timing_dialog_(nullptr)
    , vectorscope_dialog_(nullptr)
    , nav_debounce_timer_(new QTimer(this))
    , tweak_debounce_timer_(new QTimer(this))
{
    nav_debounce_timer_->setSingleShot(true);
    nav_debounce_timer_->setInterval(100);  // ms: coalesces rapid scrub moves
    connect(nav_debounce_timer_, &QTimer::timeout, [this]() {
        emit renderRequested(currentIndex());
    });

    tweak_debounce_timer_->setSingleShot(true);
    tweak_debounce_timer_->setInterval(150);  // ms: coalesces rapid widget changes
    connect(tweak_debounce_timer_, &QTimer::timeout, [this]() {
        if (!tweak_node_id_.is_valid() || tweak_widgets_.empty()) {
            return;
        }
        emit tweakParameterChanged(tweak_node_id_, collectTweakValues(), last_tweak_class_);
    });

    setupUI();
    setWindowTitle("Field/Frame Preview");
    
    // Use Qt::Window flag to allow independent positioning without forcing z-order
    setWindowFlags(Qt::Window);
    
    // Don't destroy on close, just hide
    setAttribute(Qt::WA_DeleteOnClose, false);
    
    // Set default size - geometry will be restored by MainWindow
    resize(800, 700);
}

void PreviewDialog::setSignalControlsVisible(bool visible)
{
    signal_label_->setVisible(visible);
    signal_combo_->setVisible(visible);
}

PreviewDialog::~PreviewDialog() = default;

const std::string& PreviewDialog::kComponentVectorscopeViewIdRef()
{
    static const std::string view_id{kComponentVectorscopeViewId};
    return view_id;
}

void PreviewDialog::navigateToIndex(int zero_based)
{
    const int clamped = std::clamp(zero_based,
                                   preview_slider_->minimum(),
                                   preview_slider_->maximum());
    nav_debounce_timer_->stop();  // cancel any pending debounced render
    setIndex(clamped);
    emit positionChanged(clamped);
    emit renderRequested(clamped);
}

void PreviewDialog::navigateToIndexDebounced(int zero_based)
{
    const int clamped = std::clamp(zero_based,
                                   preview_slider_->minimum(),
                                   preview_slider_->maximum());
    setIndex(clamped);              // update UI immediately for visual feedback
    emit positionChanged(clamped);
    nav_debounce_timer_->start();   // (re)starts; fires renderRequested when settled
}

void PreviewDialog::setIndex(int zero_based)
{
    // Silently sync both slider and spinbox without emitting any signals.
    preview_slider_->blockSignals(true);
    frame_jump_spinbox_->blockSignals(true);
    preview_slider_->setValue(zero_based);
    frame_jump_spinbox_->setValue(zero_based + 1);  // 1-indexed display
    preview_slider_->blockSignals(false);
    frame_jump_spinbox_->blockSignals(false);
}

void PreviewDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    
    // Menu bar
    menu_bar_ = new QMenuBar(this);
    auto* fileMenu = menu_bar_->addMenu("&File");
    export_png_action_ = fileMenu->addAction("&Export PNG...");
    export_png_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    connect(export_png_action_, &QAction::triggered, this, &PreviewDialog::exportPNGRequested);
    
    auto* observersMenu = menu_bar_->addMenu("&Observers");
    show_vbi_action_ = observersMenu->addAction("&VBI Decoder");
    show_vbi_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    connect(show_vbi_action_, &QAction::triggered, this, &PreviewDialog::showVBIDialogRequested);
    
    show_quality_metrics_action_ = observersMenu->addAction("&Quality Metrics");
    show_quality_metrics_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    connect(show_quality_metrics_action_, &QAction::triggered, this, &PreviewDialog::showQualityMetricsDialogRequested);
    
    show_ntsc_observer_action_ = observersMenu->addAction("&NTSC Observer");
    show_ntsc_observer_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    connect(show_ntsc_observer_action_, &QAction::triggered, this, &PreviewDialog::showNtscObserverDialogRequested);
    
    auto* hintsMenu = menu_bar_->addMenu("&Hints");
    show_hints_action_ = hintsMenu->addAction("&Video Parameter Hints");
    show_hints_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
    connect(show_hints_action_, &QAction::triggered, this, &PreviewDialog::showHintsDialogRequested);
    
    auto* viewMenu = menu_bar_->addMenu("&View");
    show_field_timing_action_ = viewMenu->addAction("&Field Timing");
    show_field_timing_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
    connect(show_field_timing_action_, &QAction::triggered, this, [this]() {
        if (!hasAvailablePreviewView(kFieldTimingViewId)) {
            status_bar_->showMessage("Field timing is not available for this stage", 2000);
            return;
        }
        emit fieldTimingRequested();
    });

        show_component_vectorscope_action_ = viewMenu->addAction("&Vectorscope");
        show_component_vectorscope_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
        show_component_vectorscope_action_->setVisible(false);
        show_component_vectorscope_action_->setEnabled(false);
        connect(show_component_vectorscope_action_, &QAction::triggered,
            this, &PreviewDialog::onComponentVectorscopeActionTriggered);

    show_live_tweaks_action_ = viewMenu->addAction("&Live Tweaks");
    show_live_tweaks_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));
    show_live_tweaks_action_->setCheckable(true);
    show_live_tweaks_action_->setChecked(false);
    connect(show_live_tweaks_action_, &QAction::toggled, this, &PreviewDialog::onShowLiveTweaksToggled);
    
    mainLayout->setMenuBar(menu_bar_);
    
    // Preview widget
    preview_widget_ = new FieldPreviewWidget(this);
    preview_widget_->setMinimumSize(640, 480);
    mainLayout->addWidget(preview_widget_, 1);
    
    // Preview info label
    preview_info_label_ = new QLabel("No preview available");
    mainLayout->addWidget(preview_info_label_);
    
    // Slider controls with navigation buttons
    auto* sliderLayout = new QHBoxLayout();
    
    // Navigation buttons
    first_button_ = new QPushButton("<<");
    prev_button_ = new QPushButton("<");
    next_button_ = new QPushButton(">");
    last_button_ = new QPushButton(">>");
    
    // Set auto-repeat on prev/next buttons for navigation
    // Increased delay to reduce sensitivity for single-frame stepping
    prev_button_->setAutoRepeat(true);
    prev_button_->setAutoRepeatDelay(200);
    prev_button_->setAutoRepeatInterval(30);
    
    next_button_->setAutoRepeat(true);
    next_button_->setAutoRepeatDelay(200);
    next_button_->setAutoRepeatInterval(30);
    
    // Prevent navigation buttons from being treated as dialog default buttons
    // (without this, the << button appears focused when the spinbox has focus)
    first_button_->setAutoDefault(false);
    prev_button_->setAutoDefault(false);
    next_button_->setAutoDefault(false);
    last_button_->setAutoDefault(false);

    // Set fixed width for navigation buttons
    first_button_->setFixedWidth(40);
    prev_button_->setFixedWidth(40);
    next_button_->setFixedWidth(40);
    last_button_->setFixedWidth(40);
    
    slider_min_label_ = new QLabel("0");
    slider_max_label_ = new QLabel("0");
    preview_slider_ = new QSlider(Qt::Horizontal);
    preview_slider_->setEnabled(false);
    // Set tracking to false for better performance during scrubbing
    // This makes the slider only emit valueChanged when released, not during drag
    // For real-time preview during drag, we can use sliderMoved signal separately
    preview_slider_->setTracking(true);  // Keep true for now, but we'll throttle updates in MainWindow
    
    sliderLayout->addWidget(first_button_);
    sliderLayout->addWidget(prev_button_);
    sliderLayout->addWidget(next_button_);
    sliderLayout->addWidget(last_button_);
    
    // Jump-to spin box (1-indexed, to the right of navigation buttons)
    frame_jump_spinbox_ = new QSpinBox();
    frame_jump_spinbox_->setMinimum(1);
    frame_jump_spinbox_->setMaximum(1);
    frame_jump_spinbox_->setEnabled(false);
    {
        // Size the spinbox to comfortably hold up to 5-digit numbers
        QFontMetrics fm(frame_jump_spinbox_->font());
        frame_jump_spinbox_->setMinimumWidth(fm.horizontalAdvance("99999") + 36);  // +36 for arrows/margins
    }
    frame_jump_spinbox_->setToolTip("Jump directly to a frame/field number");
    sliderLayout->addWidget(frame_jump_spinbox_);
    
    sliderLayout->addWidget(slider_min_label_);
    sliderLayout->addWidget(preview_slider_, 1);
    sliderLayout->addWidget(slider_max_label_);
    mainLayout->addLayout(sliderLayout);
    
    // Control row: Preview mode and aspect ratio
    auto* controlLayout = new QHBoxLayout();
    
    controlLayout->addWidget(new QLabel("Preview Mode:"));
    preview_mode_combo_ = new QComboBox();
    controlLayout->addWidget(preview_mode_combo_);
    
    signal_label_ = new QLabel("Signal:");
    signal_label_->setVisible(false);  // Hidden by default, shown for YC sources
    controlLayout->addWidget(signal_label_);
    signal_combo_ = new QComboBox();
    signal_combo_->addItem("Y+C");
    signal_combo_->addItem("Y");
    signal_combo_->addItem("C");
    signal_combo_->setVisible(false);  // Hidden by default, shown for YC sources
    controlLayout->addWidget(signal_combo_);
    
    controlLayout->addWidget(new QLabel("Aspect Ratio:"));
    aspect_ratio_combo_ = new QComboBox();
    controlLayout->addWidget(aspect_ratio_combo_);
    
    // Add Zoom 1:1 button
    zoom1to1_button_ = new QPushButton("Zoom 1:1");
    zoom1to1_button_->setAutoDefault(false);
    zoom1to1_button_->setToolTip("Resize preview to original image size");
    controlLayout->addWidget(zoom1to1_button_);
    
    // Add Dropouts toggle button
    dropouts_button_ = new QPushButton("Dropouts: Off");
    dropouts_button_->setAutoDefault(false);
    dropouts_button_->setCheckable(true);
    dropouts_button_->setChecked(false);
    dropouts_button_->setToolTip("Show/hide dropout regions");
    controlLayout->addWidget(dropouts_button_);
    
    controlLayout->addStretch();
    mainLayout->addLayout(controlLayout);
    
    // Status bar
    status_bar_ = new QStatusBar(this);
    status_bar_->showMessage("No stage selected");
    mainLayout->addWidget(status_bar_);

    // -----------------------------------------------------------------------
    // Live Preview Tweaks Dialog (Phase 6)
    // Hosted in its own window and opened from View -> Live Tweaks.
    // -----------------------------------------------------------------------
    live_tweaks_dialog_ = new QDialog(this, Qt::Window);
    live_tweaks_dialog_->setAttribute(Qt::WA_DeleteOnClose, false);
    updateLiveTweaksWindowTitle();
    live_tweaks_dialog_->resize(460, 520);

    auto* tweak_dialog_layout = new QVBoxLayout(live_tweaks_dialog_);
    tweak_panel_content_ = new QWidget(live_tweaks_dialog_);
    tweak_form_layout_ = new QFormLayout(tweak_panel_content_);
    tweak_form_layout_->setContentsMargins(8, 8, 8, 8);

    tweak_panel_scroll_ = new QScrollArea(live_tweaks_dialog_);
    tweak_panel_scroll_->setWidget(tweak_panel_content_);
    tweak_panel_scroll_->setWidgetResizable(true);
    tweak_panel_scroll_->setFrameShape(QFrame::NoFrame);
    tweak_dialog_layout->addWidget(tweak_panel_scroll_);

    auto* tweak_buttons_layout = new QHBoxLayout();
    tweak_buttons_layout->addStretch();
    tweak_reset_button_ = new QPushButton("Reset to stage parameters", live_tweaks_dialog_);
    tweak_write_button_ = new QPushButton("Write to stage parameters", live_tweaks_dialog_);
    tweak_reset_button_->setEnabled(false);
    tweak_write_button_->setEnabled(false);
    tweak_buttons_layout->addWidget(tweak_reset_button_);
    tweak_buttons_layout->addWidget(tweak_write_button_);
    tweak_dialog_layout->addLayout(tweak_buttons_layout);

    connect(tweak_reset_button_, &QPushButton::clicked, this, &PreviewDialog::onResetLiveTweaksClicked);
    connect(tweak_write_button_, &QPushButton::clicked, this, &PreviewDialog::onWriteLiveTweaksClicked);

    connect(live_tweaks_dialog_, &QDialog::finished, this, [this](int) {
        if (show_live_tweaks_action_) {
            QSignalBlocker blocker(show_live_tweaks_action_);
            show_live_tweaks_action_->setChecked(false);
        }
        emit allLiveTweaksDismissed();
    });
    
    // -----------------------------------------------------------------------
    // Internal sync: keep spinbox display in step with slider (handles range-
    // clamping done by Qt when setRange is called, and also setIndex calls).
    // These connections do NOT emit navigation signals.
    // -----------------------------------------------------------------------
    connect(preview_slider_, &QSlider::valueChanged, [this](int value) {
        frame_jump_spinbox_->blockSignals(true);
        frame_jump_spinbox_->setValue(value + 1);
        frame_jump_spinbox_->blockSignals(false);
    });
    connect(preview_slider_, &QSlider::rangeChanged, [this](int min, int max) {
        frame_jump_spinbox_->setRange(min + 1, max + 1);
    });

    // -----------------------------------------------------------------------
    // Navigation: all sources call navigateToIndex / navigateToIndexDebounced.
    // Those two methods are the single authority for position changes and
    // decide which signals to emit.
    // -----------------------------------------------------------------------

    // First / Prev / Next / Last buttons — always immediate
    connect(first_button_, &QPushButton::clicked,  [this]() {
        navigateToIndex(preview_slider_->minimum());
    });
    connect(prev_button_,  &QPushButton::clicked,  [this]() {
        navigateToIndex(preview_slider_->value() - 1);
    });
    connect(next_button_,  &QPushButton::clicked,  [this]() {
        navigateToIndex(preview_slider_->value() + 1);
    });
    connect(last_button_,  &QPushButton::clicked,  [this]() {
        navigateToIndex(preview_slider_->maximum());
    });

    // Slider drag — debounced for smooth scrubbing
    connect(preview_slider_, &QSlider::sliderMoved, [this](int value) {
        navigateToIndexDebounced(value);
    });
    // Slider release — commit immediately (cancels any pending debounce)
    connect(preview_slider_, &QSlider::sliderReleased, [this]() {
        navigateToIndex(preview_slider_->value());
    });

    // Spinbox changes (arrow buttons AND keyboard typing) — debounced so that
    // intermediate digits while typing are coalesced into a final render.
    connect(frame_jump_spinbox_, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int one_based) {
        navigateToIndexDebounced(one_based - 1);
    });
    // Non-navigation signals
    connect(preview_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreviewDialog::previewModeChanged);
    connect(signal_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreviewDialog::signalChanged);
    connect(aspect_ratio_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreviewDialog::aspectRatioModeChanged);

    // Connect dropouts button
    connect(dropouts_button_, &QPushButton::toggled, [this](bool checked) {
        dropouts_button_->setText(checked ? "Dropouts: On" : "Dropouts: Off");
        emit showDropoutsChanged(checked);
    });
    
    // Connect Zoom 1:1 button
    connect(zoom1to1_button_, &QPushButton::clicked, [this]() {
        QSize img_size = preview_widget_->originalImageSize();
        if (img_size.isEmpty()) {
            return;  // No image to zoom to
        }
        
        // The image from core already has aspect ratio scaling applied,
        // so we can use the image size directly for 1:1 zoom
        QSize display_size = img_size;
        
        // Calculate total window size based on widget size
        // We need to account for all other UI elements
        int extra_height = height() - preview_widget_->height();
        int extra_width = width() - preview_widget_->width();
        
        // Set the preview widget to the original size
        preview_widget_->setMinimumSize(display_size);
        preview_widget_->setMaximumSize(display_size);
        
        // Adjust the dialog size
        adjustSize();
        
        // Reset size constraints after resize
        preview_widget_->setMinimumSize(320, 240);
        preview_widget_->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    });
    
    // Create line scope dialog
    line_scope_dialog_ = new LineScopeDialog(this);
    
    // Create field timing dialog
    field_timing_dialog_ = new FieldTimingDialog(this);
    
    // Connect to dialog hide/close events to disable cross-hairs
    connect(line_scope_dialog_, &QDialog::finished, this, [this]() {
        preview_widget_->setCrosshairsEnabled(false);
    });
    connect(line_scope_dialog_, &QDialog::rejected, this, [this]() {
        preview_widget_->setCrosshairsEnabled(false);
    });
    
    // Connect line clicked signal
    connect(preview_widget_, &FieldPreviewWidget::lineClicked, [this](int image_x, int image_y) {
        if (!hasAvailablePreviewView(kLineScopeViewId)) {
            status_bar_->showMessage("Line scope is not available for this stage", 2000);
            return;
        }
        emit lineScopeRequested(image_x, image_y);
    });
}

void PreviewDialog::setCurrentNode(const QString& node_label, const QString& node_id)
{
    Q_UNUSED(node_label);
    status_bar_->showMessage(QString("Viewing output from stage: %1").arg(node_id));
}

void PreviewDialog::setCurrentNodeId(orc::NodeID node_id)
{
    if (tweak_node_id_ != node_id) {
        clearTweakPanel();
        tweak_node_id_ = node_id;
        updateLiveTweaksWindowTitle();
    }
    current_node_id_ = node_id;

    if (vectorscope_dialog_ && vectorscope_dialog_->isVisible() && node_id.is_valid()) {
        vectorscope_node_id_ = node_id;
        vectorscope_dialog_->setStage(node_id);
    }
}

void PreviewDialog::setAvailablePreviewViews(const std::vector<orc::PreviewViewDescriptor>& views)
{
    available_preview_view_ids_.clear();
    for (const auto& view : views) {
        available_preview_view_ids_.insert(view.id);
    }

    const bool line_scope_available = hasAvailablePreviewView(kLineScopeViewId);
    const bool field_timing_available = hasAvailablePreviewView(kFieldTimingViewId);
    const bool component_vectorscope_available = hasAvailablePreviewView(kComponentVectorscopeViewId);

    if (show_field_timing_action_) {
        show_field_timing_action_->setVisible(field_timing_available);
        show_field_timing_action_->setEnabled(field_timing_available);
    }

    if (!line_scope_available && line_scope_dialog_ && line_scope_dialog_->isVisible()) {
        line_scope_dialog_->close();
    }

    if (!field_timing_available && field_timing_dialog_ && field_timing_dialog_->isVisible()) {
        field_timing_dialog_->close();
    }

    if (show_component_vectorscope_action_) {
        show_component_vectorscope_action_->setVisible(component_vectorscope_available);
        show_component_vectorscope_action_->setEnabled(component_vectorscope_available);
    }

    if (!component_vectorscope_available) {
        closeVectorscopeDialogs();
    }
}

bool PreviewDialog::hasAvailablePreviewView(const std::string& view_id) const
{
    return available_preview_view_ids_.find(view_id) != available_preview_view_ids_.end();
}

void PreviewDialog::setSharedPreviewCoordinate(const orc::PreviewCoordinate& coordinate)
{
    if (!coordinate.is_valid()) {
        return;
    }

    shared_preview_coordinate_ = coordinate;
    if (vectorscope_dialog_) {
        shared_preview_coordinate_->vectorscope_active_area_only = vectorscope_dialog_->isActiveAreaOnly();
    }
    emit previewCoordinateChanged(*shared_preview_coordinate_);
}

void PreviewDialog::showVectorscopeForNode(orc::NodeID node_id)
{
    if (!node_id.is_valid()) {
        return;
    }

    if (!vectorscope_dialog_) {
        vectorscope_dialog_ = new VectorscopeDialog(this);
        vectorscope_dialog_->setAttribute(Qt::WA_DeleteOnClose, false);

        connect(vectorscope_dialog_, &VectorscopeDialog::dataRefreshRequested, this, [this]() {
            if (!current_node_id_.is_valid()) {
                return;
            }

            orc::PreviewCoordinate coordinate;
            if (shared_preview_coordinate_.has_value() && shared_preview_coordinate_->is_valid()) {
                coordinate = *shared_preview_coordinate_;
            } else {
                coordinate.field_index = static_cast<uint64_t>(currentIndex());
            }

            coordinate.vectorscope_active_area_only = vectorscope_dialog_->isActiveAreaOnly();
            emit vectorscopeRequested(coordinate);
        });

        connect(vectorscope_dialog_, &QObject::destroyed, this, [this]() {
            vectorscope_dialog_ = nullptr;
            vectorscope_node_id_ = orc::NodeID{};
        });
    }

    vectorscope_dialog_->setScopeLabel(QStringLiteral("Vectorscope"));

    vectorscope_node_id_ = node_id;
    vectorscope_dialog_->setStage(node_id);
    vectorscope_dialog_->show();
    vectorscope_dialog_->raise();
    vectorscope_dialog_->activateWindow();
}

void PreviewDialog::updateVectorscope(orc::NodeID node_id, const std::optional<orc::VectorscopeData>& data)
{
    Q_UNUSED(node_id);

    if (!vectorscope_dialog_) {
        return;
    }

    if (!vectorscope_dialog_->isVisible()) {
        return;
    }

    if (data.has_value()) {
        vectorscope_dialog_->updateVectorscope(*data);
    } else {
        vectorscope_dialog_->clearDisplay();
    }
}

bool PreviewDialog::isVectorscopeVisibleForNode(orc::NodeID node_id) const
{
    Q_UNUSED(node_id);
    return vectorscope_dialog_ && vectorscope_dialog_->isVisible();
}

void PreviewDialog::onSampleMarkerMoved(int sample_x)
{
    // Emit signal for MainWindow to update cross-hairs
    // MainWindow has the context to map sample_x properly
    emit sampleMarkerMovedInLineScope(sample_x);
}

// =============================================================================
// Live Preview Tweak Panel (Phase 6)
// =============================================================================

void PreviewDialog::setTweakableParameters(
    orc::NodeID node_id,
    const std::vector<orc::LiveTweakableParameterView>& tweakable,
    const std::vector<orc::ParameterDescriptor>& descriptors,
    const std::map<std::string, orc::ParameterValue>& display_values,
    bool has_unsaved_changes)
{
    clearTweakPanel();
    tweak_node_id_ = node_id;
    updateLiveTweaksWindowTitle();

    if (tweakable.empty() || descriptors.empty()) {
        tweak_form_layout_->addRow(new QLabel("No live tweak parameters available for this stage.", tweak_panel_content_));
        tweak_reset_button_->setEnabled(false);
        tweak_write_button_->setEnabled(false);
        return;
    }

    buildTweakPanel(tweakable, descriptors, display_values);

    const bool has_live_tweaks = !tweak_widgets_.empty();
    tweak_reset_button_->setEnabled(has_live_tweaks);
    tweak_write_button_->setEnabled(has_live_tweaks);
    if (!has_live_tweaks) {
        tweak_form_layout_->addRow(new QLabel("No live tweak parameters available for this stage.", tweak_panel_content_));
    }

    tweak_unsaved_changes_ = has_unsaved_changes && has_live_tweaks;
    updateLiveTweaksWindowTitle();
}

void PreviewDialog::setLiveTweaksDirty(bool has_unsaved_changes)
{
    tweak_unsaved_changes_ = has_unsaved_changes && !tweak_widgets_.empty();
    updateLiveTweaksWindowTitle();
}

void PreviewDialog::clearTweakPanel()
{
    tweak_debounce_timer_->stop();
    tweak_widgets_.clear();
    tweak_node_id_ = orc::NodeID{};
    tweak_unsaved_changes_ = false;
    updateLiveTweaksWindowTitle();
    tweak_reset_button_->setEnabled(false);
    tweak_write_button_->setEnabled(false);

    // Remove all rows from the form
    while (tweak_form_layout_->rowCount() > 0) {
        tweak_form_layout_->removeRow(0);
    }
}

void PreviewDialog::updateLiveTweaksWindowTitle()
{
    if (!live_tweaks_dialog_) {
        return;
    }

    const QString dirty_marker = tweak_unsaved_changes_ ? "*" : "";

    if (tweak_node_id_.is_valid()) {
        live_tweaks_dialog_->setWindowTitle(
            QString("Live Preview Tweaks%1 - Stage ID: %2")
                .arg(dirty_marker)
                .arg(QString::fromStdString(tweak_node_id_.to_string())));
        return;
    }

    live_tweaks_dialog_->setWindowTitle(QString("Live Preview Tweaks%1 - No stage selected").arg(dirty_marker));
}

void PreviewDialog::buildTweakPanel(
    const std::vector<orc::LiveTweakableParameterView>& tweakable,
    const std::vector<orc::ParameterDescriptor>& descriptors,
    const std::map<std::string, orc::ParameterValue>& current_values)
{
    // Build lookup maps for quick access
    std::map<std::string, const orc::ParameterDescriptor*> desc_map;
    for (const auto& d : descriptors) {
        desc_map[d.name] = &d;
    }
    std::map<std::string, orc::LiveTweakClass> tweak_class_map;
    for (const auto& t : tweakable) {
        tweak_class_map[t.parameter_name] = t.tweak_class;
    }

    for (const auto& tweak : tweakable) {
        auto desc_it = desc_map.find(tweak.parameter_name);
        if (desc_it == desc_map.end()) {
            continue;  // Descriptor not provided — skip
        }
        const auto& desc = *desc_it->second;

        const orc::ParameterValue value = resolveTweakParameterValue(desc, current_values);

        QWidget* widget = nullptr;
        TweakWidgetEntry entry;
        entry.type = desc.type;
        entry.tweak_class = tweak.tweak_class;

        switch (desc.type) {
            case orc::ParameterType::INT32: {
                auto* spin = new QSpinBox(tweak_panel_content_);
                if (desc.constraints.min_value.has_value())
                    spin->setMinimum(std::get<int32_t>(*desc.constraints.min_value));
                else
                    spin->setMinimum(std::numeric_limits<int32_t>::min());
                if (desc.constraints.max_value.has_value())
                    spin->setMaximum(std::get<int32_t>(*desc.constraints.max_value));
                else
                    spin->setMaximum(std::numeric_limits<int32_t>::max());
                spin->setValue(std::get<int32_t>(value));
                widget = spin;
                const std::string pname = desc.name;
                const orc::LiveTweakClass tc = tweak.tweak_class;
                connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), [this, pname, tc](int) {
                    last_tweak_class_ = tc;
                    tweak_debounce_timer_->start();
                });
                break;
            }
            case orc::ParameterType::UINT32: {
                auto* spin = new QSpinBox(tweak_panel_content_);
                spin->setMinimum(0);
                if (desc.constraints.min_value.has_value())
                    spin->setMinimum(static_cast<int>(std::get<uint32_t>(*desc.constraints.min_value)));
                if (desc.constraints.max_value.has_value())
                    spin->setMaximum(static_cast<int>(std::get<uint32_t>(*desc.constraints.max_value)));
                else
                    spin->setMaximum(std::numeric_limits<int>::max());
                spin->setValue(static_cast<int>(std::get<uint32_t>(value)));
                widget = spin;
                const std::string pname = desc.name;
                const orc::LiveTweakClass tc = tweak.tweak_class;
                connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), [this, pname, tc](int) {
                    last_tweak_class_ = tc;
                    tweak_debounce_timer_->start();
                });
                break;
            }
            case orc::ParameterType::DOUBLE: {
                auto* spin = new QDoubleSpinBox(tweak_panel_content_);
                spin->setDecimals(4);
                if (desc.constraints.min_value.has_value())
                    spin->setMinimum(std::get<double>(*desc.constraints.min_value));
                else
                    spin->setMinimum(-std::numeric_limits<double>::max());
                if (desc.constraints.max_value.has_value())
                    spin->setMaximum(std::get<double>(*desc.constraints.max_value));
                else
                    spin->setMaximum(std::numeric_limits<double>::max());
                spin->setValue(std::get<double>(value));
                widget = spin;
                const std::string pname = desc.name;
                const orc::LiveTweakClass tc = tweak.tweak_class;
                connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this, pname, tc](double) {
                    last_tweak_class_ = tc;
                    tweak_debounce_timer_->start();
                });
                break;
            }
            case orc::ParameterType::BOOL: {
                auto* check = new QCheckBox(tweak_panel_content_);
                check->setChecked(std::get<bool>(value));
                widget = check;
                const std::string pname = desc.name;
                const orc::LiveTweakClass tc = tweak.tweak_class;
                connect(check, &QCheckBox::toggled, [this, pname, tc](bool) {
                    last_tweak_class_ = tc;
                    tweak_debounce_timer_->start();
                });
                break;
            }
            case orc::ParameterType::STRING: {
                if (!desc.constraints.allowed_strings.empty()) {
                    auto* combo = new QComboBox(tweak_panel_content_);
                    for (const auto& s : desc.constraints.allowed_strings) {
                        combo->addItem(QString::fromStdString(s));
                    }
                    combo->setCurrentText(QString::fromStdString(std::get<std::string>(value)));
                    widget = combo;
                    const std::string pname = desc.name;
                    const orc::LiveTweakClass tc = tweak.tweak_class;
                    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, pname, tc](int) {
                        last_tweak_class_ = tc;
                        tweak_debounce_timer_->start();
                    });
                }
                // Free-form strings and FILE_PATH are not shown in the tweak panel
                break;
            }
            case orc::ParameterType::FILE_PATH:
                // Output file paths are never tweak-panel items; skip.
                break;
        }

        if (widget) {
            entry.widget = widget;
            tweak_widgets_[desc.name] = std::move(entry);
            const QString label = QString::fromStdString(
                desc.display_name.empty() ? desc.name : desc.display_name);
            tweak_form_layout_->addRow(label + ":", widget);
        }
    }
}

std::map<std::string, orc::ParameterValue> PreviewDialog::collectTweakValues() const
{
    std::map<std::string, orc::ParameterValue> result;
    for (const auto& [name, entry] : tweak_widgets_) {
        if (!entry.widget) {
            continue;
        }
        switch (entry.type) {
            case orc::ParameterType::INT32: {
                if (auto* spin = qobject_cast<QSpinBox*>(entry.widget)) {
                    result[name] = static_cast<int32_t>(spin->value());
                }
                break;
            }
            case orc::ParameterType::UINT32: {
                if (auto* spin = qobject_cast<QSpinBox*>(entry.widget)) {
                    result[name] = static_cast<uint32_t>(spin->value());
                }
                break;
            }
            case orc::ParameterType::DOUBLE: {
                if (auto* spin = qobject_cast<QDoubleSpinBox*>(entry.widget)) {
                    result[name] = spin->value();
                }
                break;
            }
            case orc::ParameterType::BOOL: {
                if (auto* check = qobject_cast<QCheckBox*>(entry.widget)) {
                    result[name] = check->isChecked();
                }
                break;
            }
            case orc::ParameterType::STRING: {
                if (auto* combo = qobject_cast<QComboBox*>(entry.widget)) {
                    result[name] = combo->currentText().toStdString();
                }
                break;
            }
            case orc::ParameterType::FILE_PATH:
                break;
        }
    }
    return result;
}

orc::LiveTweakClass PreviewDialog::dominantTweakClass(
    const std::string& changed_param_name) const
{
    auto it = tweak_widgets_.find(changed_param_name);
    if (it != tweak_widgets_.end()) {
        return it->second.tweak_class;
    }
    return orc::LiveTweakClass::DecodePhase;
}

void PreviewDialog::onShowLiveTweaksToggled(bool checked)
{
    if (!live_tweaks_dialog_) {
        return;
    }

    if (!checked) {
        live_tweaks_dialog_->hide();
        status_bar_->showMessage("Live tweaks hidden", 1500);
        return;
    }

    live_tweaks_dialog_->show();
    live_tweaks_dialog_->raise();
    live_tweaks_dialog_->activateWindow();

    if (tweak_widgets_.empty() && tweak_form_layout_ && tweak_form_layout_->rowCount() == 0) {
        tweak_form_layout_->addRow(new QLabel("No live tweak parameters available for this stage.", tweak_panel_content_));
    }

    status_bar_->showMessage("Live tweaks shown", 1500);
}

void PreviewDialog::onResetLiveTweaksClicked()
{
    if (!tweak_node_id_.is_valid()) {
        status_bar_->showMessage("No selected stage for reset", 2000);
        return;
    }
    emit resetLiveTweaksRequested(tweak_node_id_);
}

void PreviewDialog::onWriteLiveTweaksClicked()
{
    if (!tweak_node_id_.is_valid()) {
        status_bar_->showMessage("No selected stage for write", 2000);
        return;
    }
    emit writeLiveTweaksRequested(tweak_node_id_, collectTweakValues());
}

void PreviewDialog::closeEvent(QCloseEvent* event)
{
    if (live_tweaks_dialog_) {
        live_tweaks_dialog_->hide();
    }
    closeChildDialogs();
    QDialog::closeEvent(event);
}

void PreviewDialog::closeChildDialogs()
{
    // Close line scope dialog if open
    if (line_scope_dialog_ && line_scope_dialog_->isVisible()) {
        line_scope_dialog_->close();
    }
    
    // Close field timing dialog if open
    if (field_timing_dialog_ && field_timing_dialog_->isVisible()) {
        field_timing_dialog_->close();
    }

    closeVectorscopeDialogs();
    
    // Disable cross-hairs when closing
    if (preview_widget_) {
        preview_widget_->setCrosshairsEnabled(false);
    }
}

void PreviewDialog::closeVectorscopeDialogs()
{
    if (vectorscope_dialog_) {
        vectorscope_dialog_->close();
    }
    vectorscope_node_id_ = orc::NodeID{};
}

bool PreviewDialog::isLineScopeVisible() const
{
    return line_scope_dialog_ && line_scope_dialog_->isVisible();
}
void PreviewDialog::showLineScope(const QString& node_id, int stage_index, uint64_t field_index, int line_number, int sample_x, 
                                  const std::vector<uint16_t>& samples,
                                  const std::optional<orc::presenters::VideoParametersView>& video_params,
                                   int preview_image_width, int original_sample_x, int original_image_y,
                                  orc::PreviewOutputType preview_mode,
                                  const std::vector<uint16_t>& y_samples,
                                  const std::vector<uint16_t>& c_samples)
{
    if (line_scope_dialog_) {
        // Store line scope context for cross-hair updates
        current_line_scope_preview_width_ = preview_image_width;
        current_line_scope_samples_count_ = samples.size();
        if (current_line_scope_samples_count_ == 0 && !y_samples.empty()) {
            // Use Y samples size if no composite
            current_line_scope_samples_count_ = y_samples.size();
        }
        // Note: We don't know the image_y here directly, but MainWindow will update cross-hairs
        
        // Connect navigation signal if not already connected
        connect(line_scope_dialog_, &LineScopeDialog::lineNavigationRequested,
                this, &PreviewDialog::lineNavigationRequested, Qt::UniqueConnection);
        
        // Connect refresh signal if not already connected
        connect(line_scope_dialog_, &LineScopeDialog::refreshRequested,
                this, &PreviewDialog::lineScopeRequested, Qt::UniqueConnection);
        
        // Connect sample marker moved signal to update cross-hairs
        connect(line_scope_dialog_, &LineScopeDialog::sampleMarkerMoved,
                this, &PreviewDialog::onSampleMarkerMoved, Qt::UniqueConnection);
        
        // Only enable cross-hairs if there's actual data to display
        // For stages like FFmpeg video sync that don't have line data, hide cross-hairs
        if (samples.empty() && y_samples.empty() && c_samples.empty()) {
            preview_widget_->setCrosshairsEnabled(false);
        } else {
            preview_widget_->setCrosshairsEnabled(true);
        }
        
        line_scope_dialog_->setLineSamples(node_id, stage_index, field_index, line_number, sample_x, samples, video_params, 
                                          preview_image_width, original_sample_x, original_image_y, preview_mode,
                                          y_samples, c_samples);
        
        const bool was_visible = line_scope_dialog_->isVisible();
        // Only show if not already visible to avoid position resets
        if (!was_visible) {
            // Position line scope beside preview if possible
            const int margin = 12;
            const QRect preview_geom = frameGeometry();
            QSize scope_size = line_scope_dialog_->size();
            const QSize hint_size = line_scope_dialog_->sizeHint();
            if (hint_size.isValid()) {
                scope_size = hint_size;
            }

            QScreen* screen = this->screen();
            if (!screen) {
                screen = QGuiApplication::primaryScreen();
            }
            QRect screen_geom = screen ? screen->availableGeometry() : QRect();

            int target_x = preview_geom.right() + margin;
            int target_y = preview_geom.top();

            auto clamp = [](int value, int min_v, int max_v) {
                return std::max(min_v, std::min(value, max_v));
            };

            if (screen) {
                const int screen_left = screen_geom.left();
                const int screen_top = screen_geom.top();
                const int screen_right = screen_geom.left() + screen_geom.width() - scope_size.width();
                const int screen_bottom = screen_geom.top() + screen_geom.height() - scope_size.height();

                const int right_x = preview_geom.right() + margin;
                const int left_x = preview_geom.left() - margin - scope_size.width();

                if (right_x <= screen_right) {
                    target_x = right_x;
                } else if (left_x >= screen_left) {
                    target_x = left_x;
                } else {
                    target_x = clamp(preview_geom.right() - scope_size.width(), screen_left, screen_right);
                }

                target_y = clamp(target_y, screen_top, screen_bottom);
            }

            line_scope_dialog_->move(target_x, target_y);
            line_scope_dialog_->show();
        }

        // Only raise when first shown to avoid stealing focus on updates
        if (!was_visible) {
            line_scope_dialog_->raise();
        }
    }
}
void PreviewDialog::notifyFrameChanged()
{
    emit previewFrameChanged();
}

void PreviewDialog::onComponentVectorscopeActionTriggered()
{
    if (!hasAvailablePreviewView(kComponentVectorscopeViewId) || !current_node_id_.is_valid()) {
        return;
    }

    showVectorscopeForNode(current_node_id_);

    orc::PreviewCoordinate coordinate;
    if (shared_preview_coordinate_.has_value() && shared_preview_coordinate_->is_valid()) {
        coordinate = *shared_preview_coordinate_;
    } else {
        coordinate.field_index = static_cast<uint64_t>(currentIndex());
        coordinate.line_index = 0;
        coordinate.sample_offset = 0;
    }

    emit vectorscopeRequested(coordinate);
}
