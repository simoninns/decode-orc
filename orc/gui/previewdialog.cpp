/*
 * File:        previewdialog.cpp
 * Module:      orc-gui
 * Purpose:     Separate preview window for field/frame viewing
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "previewdialog.h"

#include <QCloseEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMoveEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QShowEvent>
#include <QStatusBar>
#include <QVBoxLayout>
#include <algorithm>

#include "fieldpreviewwidget.h"
#include "framescopedialog.h"
#include "frametimingdialog.h"
#include "logging.h"
#include "preview/vectorscope_dialog.h"
#include "waveformmonitordialog.h"

namespace {

constexpr const char* kLineScopeViewId = "preview.linescope";
constexpr const char* kFrameTimingViewId = "preview.frame_timing";
constexpr const char* kWaveformMonitorViewId = "preview.frame_timing";
constexpr const char* kComponentVectorscopeViewId = "preview.vectorscope";

}  // namespace

PreviewDialog::PreviewDialog(QWidget* parent)
    : QDialog(parent),
      frame_scope_dialog_(nullptr),
      frame_timing_dialog_(nullptr),
      waveform_monitor_dialog_(nullptr),
      vectorscope_dialog_(nullptr),
      nav_debounce_timer_(new QTimer(this)),
      playback_timer_(new QTimer(this)) {
  nav_debounce_timer_->setSingleShot(true);
  nav_debounce_timer_->setInterval(100);  // ms: coalesces rapid scrub moves
  connect(nav_debounce_timer_, &QTimer::timeout,
          [this]() { emit renderRequested(currentIndex()); });

  // ITU-R BT.470-6 §5.1 (PAL 25 fps) / SMPTE 170M-2004 §2 (NTSC ~30 fps).
  // Default to PAL rate; MainWindow calls setPlaybackFrameRateMs() once it
  // knows the video standard.
  playback_timer_->setInterval(40);
  connect(playback_timer_, &QTimer::timeout, [this]() {
    const int next = currentIndex() + 1;
    if (next > preview_slider_->maximum()) {
      stopPlayback();
      return;
    }
    navigateToIndex(next);
  });

  setupUI();
  setWindowTitle("Field/Frame Preview");

  // Use Qt::Window flag to allow independent positioning without forcing
  // z-order
  setWindowFlags(Qt::Window);

  // Don't destroy on close, just hide
  setAttribute(Qt::WA_DeleteOnClose, false);

  // Set default size - geometry will be restored by MainWindow
  resize(800, 700);
}

void PreviewDialog::setSignalControlsVisible(bool visible) {
  signal_label_->setVisible(visible);
  signal_combo_->setVisible(visible);
}

PreviewDialog::~PreviewDialog() = default;

const std::string& PreviewDialog::kComponentVectorscopeViewIdRef() {
  static const std::string view_id{kComponentVectorscopeViewId};
  return view_id;
}

void PreviewDialog::navigateToIndex(int zero_based) {
  const int clamped = std::clamp(zero_based, preview_slider_->minimum(),
                                 preview_slider_->maximum());
  nav_debounce_timer_->stop();  // cancel any pending debounced render
  setIndex(clamped);
  emit positionChanged(clamped);
  emit renderRequested(clamped);
}

void PreviewDialog::navigateToIndexDebounced(int zero_based) {
  const int clamped = std::clamp(zero_based, preview_slider_->minimum(),
                                 preview_slider_->maximum());
  setIndex(clamped);  // update UI immediately for visual feedback
  emit positionChanged(clamped);
  nav_debounce_timer_
      ->start();  // (re)starts; fires renderRequested when settled
}

void PreviewDialog::setIndex(int zero_based) {
  // Silently sync both slider and spinbox without emitting any signals.
  preview_slider_->blockSignals(true);
  frame_jump_spinbox_->blockSignals(true);
  preview_slider_->setValue(zero_based);
  frame_jump_spinbox_->setValue(zero_based + 1);  // 1-indexed display
  preview_slider_->blockSignals(false);
  frame_jump_spinbox_->blockSignals(false);
}

void PreviewDialog::setupUI() {
  auto* mainLayout = new QVBoxLayout(this);

  // Menu bar
  menu_bar_ = new QMenuBar(this);
  auto* fileMenu = menu_bar_->addMenu("&File");
  export_png_action_ = fileMenu->addAction("&Export PNG...");
  export_png_action_->setShortcut(
      QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
  connect(export_png_action_, &QAction::triggered, this,
          &PreviewDialog::exportPNGRequested);

  auto* observersMenu = menu_bar_->addMenu("&Observers");
  show_vbi_action_ = observersMenu->addAction("&VBI Decoder");
  show_vbi_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
  connect(show_vbi_action_, &QAction::triggered, this,
          &PreviewDialog::showVBIDialogRequested);

  show_quality_metrics_action_ = observersMenu->addAction("&Quality Metrics");
  show_quality_metrics_action_->setShortcut(
      QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
  connect(show_quality_metrics_action_, &QAction::triggered, this,
          &PreviewDialog::showQualityMetricsDialogRequested);

  show_ntsc_observer_action_ = observersMenu->addAction("&NTSC Observer");
  show_ntsc_observer_action_->setShortcut(
      QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
  connect(show_ntsc_observer_action_, &QAction::triggered, this,
          &PreviewDialog::showNtscObserverDialogRequested);

  auto* hintsMenu = menu_bar_->addMenu("&Hints");
  show_hints_action_ = hintsMenu->addAction("&Video Parameter Hints");
  show_hints_action_->setShortcut(
      QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
  connect(show_hints_action_, &QAction::triggered, this,
          &PreviewDialog::showHintsDialogRequested);

  auto* viewMenu = menu_bar_->addMenu("&View");
  show_frame_timing_action_ = viewMenu->addAction("&Frame Timing");
  show_frame_timing_action_->setShortcut(
      QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
  connect(show_frame_timing_action_, &QAction::triggered, this, [this]() {
    if (!hasAvailablePreviewView(kFrameTimingViewId)) {
      status_bar_->showMessage("Frame timing is not available for this stage",
                               2000);
      return;
    }
    emit frameTimingRequested();
  });

  show_waveform_monitor_action_ = viewMenu->addAction("&Waveform Monitor");
  show_waveform_monitor_action_->setShortcut(
      QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
  show_waveform_monitor_action_->setVisible(false);
  show_waveform_monitor_action_->setEnabled(false);
  connect(show_waveform_monitor_action_, &QAction::triggered, this, [this]() {
    if (!hasAvailablePreviewView(kWaveformMonitorViewId)) {
      status_bar_->showMessage(
          "Waveform monitor is not available for this stage", 2000);
      return;
    }
    emit waveformMonitorRequested();
  });

  show_component_vectorscope_action_ = viewMenu->addAction("&Vectorscope");
  show_component_vectorscope_action_->setShortcut(
      QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
  show_component_vectorscope_action_->setVisible(false);
  show_component_vectorscope_action_->setEnabled(false);
  connect(show_component_vectorscope_action_, &QAction::triggered, this,
          &PreviewDialog::onComponentVectorscopeActionTriggered);

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
  play_pause_button_ = new QPushButton("▶");  // ▶ play symbol
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
  play_pause_button_->setAutoDefault(false);
  next_button_->setAutoDefault(false);
  last_button_->setAutoDefault(false);

  // Set fixed width for navigation buttons
  first_button_->setFixedWidth(40);
  prev_button_->setFixedWidth(40);
  play_pause_button_->setFixedWidth(40);
  next_button_->setFixedWidth(40);
  last_button_->setFixedWidth(40);

  play_pause_button_->setToolTip("Play / Pause");

  slider_min_label_ = new QLabel("0");
  slider_max_label_ = new QLabel("0");
  preview_slider_ = new QSlider(Qt::Horizontal);
  preview_slider_->setEnabled(false);
  // Set tracking to false for better performance during scrubbing
  // This makes the slider only emit valueChanged when released, not during drag
  // For real-time preview during drag, we can use sliderMoved signal separately
  preview_slider_->setTracking(
      true);  // Keep true for now, but we'll throttle updates in MainWindow

  sliderLayout->addWidget(first_button_);
  sliderLayout->addWidget(prev_button_);
  sliderLayout->addWidget(play_pause_button_);
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
    frame_jump_spinbox_->setMinimumWidth(fm.horizontalAdvance("99999") +
                                         36);  // +36 for arrows/margins
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

  signal_label_ = new QLabel("Channel:");
  signal_label_->setVisible(false);  // Hidden by default, shown for YC sources
  controlLayout->addWidget(signal_label_);
  signal_combo_ = new QComboBox();
  signal_combo_->addItem("Y+C");
  signal_combo_->addItem("Luma (Y)");
  signal_combo_->addItem("Chroma (C)");
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
  connect(first_button_, &QPushButton::clicked,
          [this]() { navigateToIndex(preview_slider_->minimum()); });
  connect(prev_button_, &QPushButton::clicked,
          [this]() { navigateToIndex(preview_slider_->value() - 1); });
  connect(next_button_, &QPushButton::clicked,
          [this]() { navigateToIndex(preview_slider_->value() + 1); });
  connect(last_button_, &QPushButton::clicked,
          [this]() { navigateToIndex(preview_slider_->maximum()); });

  // Play / Pause button — toggle playback state
  connect(play_pause_button_, &QPushButton::clicked, [this]() {
    if (is_playing_) {
      stopPlayback();
    } else {
      is_playing_ = true;
      play_pause_button_->setText("⏸");  // ⏸ pause symbol
      playback_timer_->start();
    }
  });

  // Slider drag — debounced for smooth scrubbing
  connect(preview_slider_, &QSlider::sliderMoved,
          [this](int value) { navigateToIndexDebounced(value); });
  // Slider release — commit immediately (cancels any pending debounce)
  connect(preview_slider_, &QSlider::sliderReleased,
          [this]() { navigateToIndex(preview_slider_->value()); });

  // Spinbox changes (arrow buttons AND keyboard typing) — debounced so that
  // intermediate digits while typing are coalesced into a final render.
  connect(frame_jump_spinbox_, QOverload<int>::of(&QSpinBox::valueChanged),
          [this](int one_based) { navigateToIndexDebounced(one_based - 1); });
  // Non-navigation signals
  connect(preview_mode_combo_,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &PreviewDialog::previewModeChanged);
  connect(signal_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &PreviewDialog::signalChanged);
  connect(aspect_ratio_combo_,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &PreviewDialog::aspectRatioModeChanged);

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

  // Create frame scope dialog (replaces LineScopeDialog)
  frame_scope_dialog_ = new FrameScopeDialog(this);

  // Create frame timing dialog
  frame_timing_dialog_ = new FrameTimingDialog(this);

  // Create waveform monitor dialog
  waveform_monitor_dialog_ = new WaveformMonitorDialog(this);

  // Connect to dialog hide/close events to disable cross-hairs
  connect(frame_scope_dialog_, &QDialog::finished, this,
          [this]() { preview_widget_->setCrosshairsEnabled(false); });
  connect(frame_scope_dialog_, &QDialog::rejected, this,
          [this]() { preview_widget_->setCrosshairsEnabled(false); });

  // Adapt FrameScopeDialog::lineNavigationRequested (size_t frame_line) to
  // PreviewDialog::lineNavigationRequested (int current_line).
  // Connected once here to avoid stacking lambda connections in showLineScope.
  connect(
      frame_scope_dialog_, &FrameScopeDialog::lineNavigationRequested, this,
      [this](int dir, uint64_t frame_id, size_t frame_line, int sx, int pw) {
        emit lineNavigationRequested(dir, frame_id,
                                     static_cast<int>(frame_line), sx, pw);
      });

  // Connect line clicked signal
  connect(preview_widget_, &FieldPreviewWidget::lineClicked,
          [this](int image_x, int image_y) {
            if (!hasAvailablePreviewView(kLineScopeViewId)) {
              status_bar_->showMessage(
                  "Line scope is not available for this stage", 2000);
              return;
            }
            emit lineScopeRequested(image_x, image_y);
          });
}

void PreviewDialog::setPlaybackFrameRateMs(int ms) {
  playback_timer_->setInterval(ms);
}

void PreviewDialog::stopPlayback() {
  if (!is_playing_) {
    return;
  }
  playback_timer_->stop();
  is_playing_ = false;
  play_pause_button_->setText("▶");  // ▶ play symbol
}

void PreviewDialog::setCurrentNode(const QString& node_label,
                                   const QString& node_id) {
  Q_UNUSED(node_label);
  status_bar_->showMessage(
      QString("Viewing output from stage: %1").arg(node_id));
}

void PreviewDialog::setCurrentNodeId(orc::NodeID node_id) {
  current_node_id_ = node_id;

  if (vectorscope_dialog_ && vectorscope_dialog_->isVisible() &&
      node_id.is_valid()) {
    vectorscope_node_id_ = node_id;
    vectorscope_dialog_->setStage(node_id);
  }
}

void PreviewDialog::setAvailablePreviewViews(
    const std::vector<orc::PreviewViewDescriptor>& views) {
  available_preview_view_ids_.clear();
  for (const auto& view : views) {
    available_preview_view_ids_.insert(view.id);
  }

  const bool line_scope_available = hasAvailablePreviewView(kLineScopeViewId);
  const bool frame_timing_available =
      hasAvailablePreviewView(kFrameTimingViewId);
  const bool waveform_monitor_available =
      hasAvailablePreviewView(kWaveformMonitorViewId);
  const bool component_vectorscope_available =
      hasAvailablePreviewView(kComponentVectorscopeViewId);

  if (show_frame_timing_action_) {
    show_frame_timing_action_->setVisible(frame_timing_available);
    show_frame_timing_action_->setEnabled(frame_timing_available);
  }

  if (show_waveform_monitor_action_) {
    show_waveform_monitor_action_->setVisible(waveform_monitor_available);
    show_waveform_monitor_action_->setEnabled(waveform_monitor_available);
  }

  if (!line_scope_available && frame_scope_dialog_ &&
      frame_scope_dialog_->isVisible()) {
    frame_scope_dialog_->close();
  }

  if (!frame_timing_available && frame_timing_dialog_ &&
      frame_timing_dialog_->isVisible()) {
    frame_timing_dialog_->close();
  }

  if (!waveform_monitor_available && waveform_monitor_dialog_ &&
      waveform_monitor_dialog_->isVisible()) {
    waveform_monitor_dialog_->close();
  }

  if (show_component_vectorscope_action_) {
    show_component_vectorscope_action_->setVisible(
        component_vectorscope_available);
    show_component_vectorscope_action_->setEnabled(
        component_vectorscope_available);
  }

  if (!component_vectorscope_available) {
    closeVectorscopeDialogs();
  }
}

bool PreviewDialog::hasAvailablePreviewView(const std::string& view_id) const {
  return available_preview_view_ids_.find(view_id) !=
         available_preview_view_ids_.end();
}

void PreviewDialog::setSharedPreviewCoordinate(
    const orc::PreviewCoordinate& coordinate) {
  if (!coordinate.is_valid()) {
    return;
  }

  shared_preview_coordinate_ = coordinate;
  if (vectorscope_dialog_) {
    shared_preview_coordinate_->vectorscope_active_area_only =
        vectorscope_dialog_->isActiveAreaOnly();
  }
  emit previewCoordinateChanged(*shared_preview_coordinate_);
}

void PreviewDialog::showVectorscopeForNode(orc::NodeID node_id) {
  if (!node_id.is_valid()) {
    return;
  }

  if (!vectorscope_dialog_) {
    vectorscope_dialog_ = new VectorscopeDialog(this);
    vectorscope_dialog_->setAttribute(Qt::WA_DeleteOnClose, false);

    connect(vectorscope_dialog_, &VectorscopeDialog::dataRefreshRequested, this,
            [this]() {
              if (!current_node_id_.is_valid()) {
                return;
              }

              orc::PreviewCoordinate coordinate;
              if (shared_preview_coordinate_.has_value() &&
                  shared_preview_coordinate_->is_valid()) {
                coordinate = *shared_preview_coordinate_;
              } else {
                coordinate.field_index = static_cast<uint64_t>(currentIndex());
              }

              coordinate.vectorscope_active_area_only =
                  vectorscope_dialog_->isActiveAreaOnly();
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

void PreviewDialog::updateVectorscope(
    orc::NodeID node_id, const std::optional<orc::VectorscopeData>& data) {
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

bool PreviewDialog::isVectorscopeVisibleForNode(orc::NodeID node_id) const {
  Q_UNUSED(node_id);
  return vectorscope_dialog_ && vectorscope_dialog_->isVisible();
}

void PreviewDialog::onSampleMarkerMoved(int sample_x) {
  // Emit signal for MainWindow to update cross-hairs
  // MainWindow has the context to map sample_x properly
  emit sampleMarkerMovedInLineScope(sample_x);
}

void PreviewDialog::closeEvent(QCloseEvent* event) {
  closeChildDialogs();
  QDialog::closeEvent(event);
}

void PreviewDialog::closeChildDialogs() {
  if (frame_scope_dialog_ && frame_scope_dialog_->isVisible()) {
    frame_scope_dialog_->close();
  }

  if (frame_timing_dialog_ && frame_timing_dialog_->isVisible()) {
    frame_timing_dialog_->close();
  }

  if (waveform_monitor_dialog_ && waveform_monitor_dialog_->isVisible()) {
    waveform_monitor_dialog_->close();
  }

  closeVectorscopeDialogs();

  // Disable cross-hairs when closing
  if (preview_widget_) {
    preview_widget_->setCrosshairsEnabled(false);
  }
}

void PreviewDialog::forwardAmplitudeUnit(orc::AmplitudeDisplayUnit unit) {
  if (frame_scope_dialog_) {
    frame_scope_dialog_->setAmplitudeUnit(unit);
  }
  if (frame_timing_dialog_) {
    frame_timing_dialog_->setAmplitudeUnit(unit);
  }
  if (waveform_monitor_dialog_) {
    waveform_monitor_dialog_->setAmplitudeUnit(unit);
  }
}

void PreviewDialog::closeVectorscopeDialogs() {
  if (vectorscope_dialog_) {
    vectorscope_dialog_->close();
  }
  vectorscope_node_id_ = orc::NodeID{};
}

bool PreviewDialog::isLineScopeVisible() const {
  return frame_scope_dialog_ && frame_scope_dialog_->isVisible();
}
void PreviewDialog::showLineScope(
    const QString& node_id, int stage_index, uint64_t field_index,
    int line_number, int sample_x, const std::vector<int16_t>& samples,
    const std::optional<orc::presenters::VideoParametersView>& video_params,
    int preview_image_width, int original_sample_x, int original_image_y,
    orc::PreviewOutputType /*preview_mode*/,
    const std::vector<int16_t>& y_samples,
    const std::vector<int16_t>& c_samples) {
  if (frame_scope_dialog_) {
    current_line_scope_preview_width_ = preview_image_width;
    current_line_scope_samples_count_ = static_cast<int>(samples.size());
    if (current_line_scope_samples_count_ == 0 && !y_samples.empty()) {
      current_line_scope_samples_count_ = static_cast<int>(y_samples.size());
    }

    connect(frame_scope_dialog_, &FrameScopeDialog::refreshRequested, this,
            &PreviewDialog::lineScopeRequested, Qt::UniqueConnection);

    connect(frame_scope_dialog_, &FrameScopeDialog::sampleMarkerMoved, this,
            &PreviewDialog::onSampleMarkerMoved, Qt::UniqueConnection);

    if (samples.empty() && y_samples.empty() && c_samples.empty()) {
      preview_widget_->setCrosshairsEnabled(false);
    } else {
      preview_widget_->setCrosshairsEnabled(true);
    }

    // field_index is used as frame_id; line_number (1-based) → frame_line
    // (0-based)
    const size_t frame_line = static_cast<size_t>(std::max(0, line_number - 1));

    frame_scope_dialog_->setFrameLineSamples(
        node_id, stage_index, field_index, frame_line, sample_x, samples,
        video_params, preview_image_width, original_sample_x, original_image_y,
        y_samples, c_samples);

    const bool was_visible = frame_scope_dialog_->isVisible();
    if (!was_visible) {
      const int margin = 12;
      const QRect preview_geom = frameGeometry();
      QSize scope_size = frame_scope_dialog_->size();
      const QSize hint_size = frame_scope_dialog_->sizeHint();
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
        const int screen_right =
            screen_geom.left() + screen_geom.width() - scope_size.width();
        const int screen_bottom =
            screen_geom.top() + screen_geom.height() - scope_size.height();

        const int right_x = preview_geom.right() + margin;
        const int left_x = preview_geom.left() - margin - scope_size.width();

        if (right_x <= screen_right) {
          target_x = right_x;
        } else if (left_x >= screen_left) {
          target_x = left_x;
        } else {
          target_x = clamp(preview_geom.right() - scope_size.width(),
                           screen_left, screen_right);
        }

        target_y = clamp(target_y, screen_top, screen_bottom);
      }

      frame_scope_dialog_->move(target_x, target_y);
      frame_scope_dialog_->show();
    }

    if (!was_visible) {
      frame_scope_dialog_->raise();
    }
  }
}
void PreviewDialog::notifyFrameChanged() { emit previewFrameChanged(); }

void PreviewDialog::onComponentVectorscopeActionTriggered() {
  if (!hasAvailablePreviewView(kComponentVectorscopeViewId) ||
      !current_node_id_.is_valid()) {
    return;
  }

  showVectorscopeForNode(current_node_id_);

  orc::PreviewCoordinate coordinate;
  if (shared_preview_coordinate_.has_value() &&
      shared_preview_coordinate_->is_valid()) {
    coordinate = *shared_preview_coordinate_;
  } else {
    coordinate.field_index = static_cast<uint64_t>(currentIndex());
    coordinate.line_index = 0;
    coordinate.sample_offset = 0;
  }

  emit vectorscopeRequested(coordinate);
}
