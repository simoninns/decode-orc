/*
 * File:        previewdialog.h
 * Module:      orc-gui
 * Purpose:     Separate preview window for field/frame viewing
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef PREVIEWDIALOG_H
#define PREVIEWDIALOG_H

#include <node_id.h>
#include <orc_preview_types.h>
#include <orc_preview_views.h>
#include <orc_vectorscope.h>

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "presenters/include/hints_view_models.h"  // For VideoParametersView

class FieldPreviewWidget;
class FrameScopeDialog;
class FrameTimingDialog;
class VectorscopeDialog;
class WaveformMonitorDialog;

/**
 * @brief Separate dialog window for previewing field/frame outputs from DAG
 * nodes
 *
 * Provides a dedicated window for viewing video field/frame previews with
 * controls for:
 * - Field/frame navigation via slider
 * - Preview mode selection (field, frame, split, etc.)
 * - Aspect ratio control
 * - Export to PNG
 * - VBI and other metadata dialogs
 *
 * This is a thin GUI layer - all rendering logic is handled by
 * orc::PreviewRenderer.
 */
class PreviewDialog : public QDialog {
  Q_OBJECT

 public:
  explicit PreviewDialog(QWidget* parent = nullptr);
  ~PreviewDialog();

  /// @name Widget Accessors
  /// @{
  FieldPreviewWidget* previewWidget() {
    return preview_widget_;
  }  ///< Get preview widget
  QSlider* previewSlider() {
    return preview_slider_;
  }  ///< Get field/frame slider
  QLabel* previewInfoLabel() {
    return preview_info_label_;
  }  ///< Get info label
  QLabel* sliderMinLabel() {
    return slider_min_label_;
  }  ///< Get slider min label
  QLabel* sliderMaxLabel() {
    return slider_max_label_;
  }  ///< Get slider max label
  QComboBox* previewModeCombo() {
    return preview_mode_combo_;
  }  ///< Get preview mode selector
  QComboBox* signalCombo() {
    return signal_combo_;
  }  ///< Get channel selector (Y+C/Luma/Chroma for YC sources)
  QLabel* signalLabel() { return signal_label_; }  ///< Get signal label
  QComboBox* aspectRatioCombo() {
    return aspect_ratio_combo_;
  }  ///< Get aspect ratio selector
  QAction* ntscObserverAction() {
    return show_ntsc_observer_action_;
  }  ///< Get NTSC observer menu action
  QPushButton* dropoutsButton() {
    return dropouts_button_;
  }  ///< Get dropouts button for state control
  QSpinBox* frameJumpSpinBox() {
    return frame_jump_spinbox_;
  }  ///< Get frame/field jump spin box
  /// @}

  /**
   * @brief Set visibility of signal controls (label and combo box)
   * @param visible True to show signal controls, false to hide them
   */
  void setSignalControlsVisible(bool visible);

  /**
   * @brief Set the currently previewed node
   * @param node_label Human-readable node label
   * @param node_id Node identifier string
   */
  void setCurrentNode(const QString& node_label, const QString& node_id);

  /**
   * @brief Set the current node used by preview-owned supplementary views.
   */
  void setCurrentNodeId(orc::NodeID node_id);

  /**
   * @brief Update view launcher availability using presenter/registry
   * descriptors.
   */
  void setAvailablePreviewViews(
      const std::vector<orc::PreviewViewDescriptor>& views);

  /**
   * @brief Check whether a registry view is currently available for this node.
   */
  bool hasAvailablePreviewView(const std::string& view_id) const;

  /**
   * @brief Return the currently selected vectorscope preview view id.
   */
  const std::string& activeVectorscopeViewId() const {
    return kComponentVectorscopeViewIdRef();
  }

  static const std::string& kComponentVectorscopeViewIdRef();

  /**
   * @brief Set and broadcast the shared preview coordinate for supplementary
   * tools.
   */
  void setSharedPreviewCoordinate(const orc::PreviewCoordinate& coordinate);

  /**
   * @brief Read the currently shared coordinate used by supplementary tools.
   */
  const std::optional<orc::PreviewCoordinate>& sharedPreviewCoordinate() const {
    return shared_preview_coordinate_;
  }

  /**
   * @brief Show vectorscope dialog owned by the preview subsystem.
   */
  void showVectorscopeForNode(orc::NodeID node_id);

  /**
   * @brief Update vectorscope dialog content for the given node.
   */
  void updateVectorscope(orc::NodeID node_id,
                         const std::optional<orc::VectorscopeData>& data);

  /**
   * @brief Check if vectorscope is visible for node.
   */
  bool isVectorscopeVisibleForNode(orc::NodeID node_id) const;

  /**
   * @brief Show frame scope dialog with sample data
   *
   * field_index is used as frame_id and line_number (1-based) is converted
   * to a 0-based frame-flat line index.
   *
   * @param node_id          Stage node identifier
   * @param stage_index      1-based pipeline stage number
   * @param field_index      0-based field index (used as frame_id)
   * @param line_number      1-based field line number
   * @param sample_x         Sample X position that was clicked
   * @param samples          CVBS_U10_4FSC composite samples
   * @param video_params     Optional video parameters for level markers
   * @param preview_image_width  Pixel width of preview image
   * @param original_sample_x    Preview-space X (for cross-hair sync)
   * @param original_image_y     Preview-space Y (for refresh)
   * @param preview_mode     Current preview mode (kept for callers; ignored
   * internally)
   * @param y_samples        Optional luma samples (YC sources)
   * @param c_samples        Optional chroma samples (YC sources)
   */
  void showLineScope(
      const QString& node_id, int stage_index, uint64_t field_index,
      int line_number, int sample_x, const std::vector<int16_t>& samples,
      const std::optional<orc::presenters::VideoParametersView>& video_params,
      int preview_image_width, int original_sample_x, int original_image_y,
      orc::PreviewOutputType preview_mode,
      const std::vector<int16_t>& y_samples = {},
      const std::vector<int16_t>& c_samples = {});

  /**
   * @brief Close all child dialogs (e.g., line scope)
   */
  void closeChildDialogs();

  /**
   * @brief Check if line scope dialog is currently visible
   */
  bool isLineScopeVisible() const;

  /**
   * @brief Notify that the preview frame/mode has changed
   *
   * Emits previewFrameChanged signal to notify line scope and other listeners
   * that they should refresh their data for the new frame context.
   */
  void notifyFrameChanged();

  /**
   * @brief Get frame scope dialog (for updating when stage changes)
   */
  FrameScopeDialog* frameScopeDialog() { return frame_scope_dialog_; }

  /**
   * @brief Get frame timing dialog (for updating when frame changes)
   */
  FrameTimingDialog* frameTimingDialog() { return frame_timing_dialog_; }

  /**
   * @brief Get waveform monitor dialog (for updating when frame changes)
   */
  WaveformMonitorDialog* waveformMonitorDialog() {
    return waveform_monitor_dialog_;
  }

  /**
   * @brief Returns the current 0-based navigation index (always matches
   * slider/spinbox display).
   */
  int currentIndex() const { return preview_slider_->value(); }

  /**
   * @brief Navigate immediately to \p zero_based.
   * Clamps to range, updates slider + spinbox, emits positionChanged and
   * renderRequested. Call this from outside the dialog (keyboard,
   * MainWindow::onNavigatePreview).
   */
  void navigateToIndex(int zero_based);

  /**
   * @brief Navigate with debounce (slider scrub, spinbox typing).
   * Updates UI immediately for visual feedback; emits renderRequested only
   * after the debounce timer fires.
   */
  void navigateToIndexDebounced(int zero_based);

  /**
   * @brief Silently set the displayed index — updates slider and spinbox
   * without emitting any navigation signals. Used by MainWindow when restoring
   * position after a range change (refreshViewerControls /
   * onPreviewModeChanged).
   */
  void setIndex(int zero_based);

  /**
   * @brief Set the playback timer interval in milliseconds.
   * Must be called by MainWindow whenever the video standard is known.
   * PAL: 40 ms (25 fps). NTSC: 33 ms (~30 fps).
   */
  void setPlaybackFrameRateMs(int ms);

  /**
   * @brief Stop playback and reset the play/pause button to the play state.
   * Call this whenever the source or project changes.
   */
  void stopPlayback();

 Q_SIGNALS:
  /**
   * Emitted whenever the current index changes (every navigate/scrub).
   * MainWindow connects this to its updatePreviewInfo() to keep the info
   * label in sync even during a scrub before the render fires.
   */
  void positionChanged(int index);
  /**
   * Emitted when a render should happen — either immediately (buttons,
   * slider release) or after the debounce timer settles (slider drag,
   * spinbox typing).  MainWindow renders now if not in-flight, otherwise
   * the completion callback will re-render for currentIndex().
   */
  void renderRequested(int index);
  void previewModeChanged(int index);
  void signalChanged(
      int index);  // Emitted when signal selection changes (Y/C/Y+C)
  void aspectRatioModeChanged(int index);
  void exportPNGRequested();
  void showVBIDialogRequested();  // Emitted when VBI Decoder menu item selected
  void showHintsDialogRequested();  // Emitted when Hints menu item selected
  void showQualityMetricsDialogRequested();  // Emitted when Quality Metrics
                                             // menu item selected
  void showNtscObserverDialogRequested();    // Emitted when NTSC Observer menu
                                             // item selected
  void showDropoutsChanged(
      bool show);  // Emitted when dropout visibility changes
  void lineScopeRequested(int image_x,
                          int image_y);  // Emitted when user clicks a line
  void lineNavigationRequested(
      int direction, uint64_t current_field, int current_line, int sample_x,
      int preview_image_width);  // Emitted when navigating lines
  void sampleMarkerMovedInLineScope(
      int sample_x);  // Emitted when sample marker moves in line scope
  void
  previewFrameChanged();  // Emitted when preview frame/output type changes -
                          // tells line scope to refresh at current position
  void frameTimingRequested();  // Emitted when user requests frame timing view
  void
  waveformMonitorRequested();  // Emitted when user requests waveform monitor
  void vectorscopeRequested(const orc::PreviewCoordinate&
                                coordinate);  // Emitted when vectorscope should
                                              // refresh via presenter contract
  void previewCoordinateChanged(
      const orc::PreviewCoordinate&
          coordinate);  // Emitted whenever shared coordinate changes

 private slots:
  void onSampleMarkerMoved(int sample_x);
  void onComponentVectorscopeActionTriggered();

 private:
  void setupUI();

  // UI components
  FieldPreviewWidget* preview_widget_;
  QSlider* preview_slider_;
  QLabel* preview_info_label_;
  QLabel* slider_min_label_;
  QLabel* slider_max_label_;
  QComboBox* preview_mode_combo_;
  QComboBox* signal_combo_;  // Signal selection for YC sources (Y/C/Y+C)
  QLabel* signal_label_;     // Label for signal combo box
  QComboBox* aspect_ratio_combo_;
  QMenuBar* menu_bar_;
  QStatusBar* status_bar_;
  QAction* export_png_action_;
  QAction* show_vbi_action_;
  QAction* show_hints_action_;
  QAction* show_quality_metrics_action_;
  QAction* show_ntsc_observer_action_;
  QAction* show_frame_timing_action_;
  QAction* show_waveform_monitor_action_;
  QAction* show_component_vectorscope_action_;
  FrameScopeDialog* frame_scope_dialog_;
  FrameTimingDialog* frame_timing_dialog_;
  WaveformMonitorDialog* waveform_monitor_dialog_;
  VectorscopeDialog* vectorscope_dialog_{nullptr};
  orc::NodeID vectorscope_node_id_;
  orc::NodeID current_node_id_;
  std::optional<orc::PreviewCoordinate> shared_preview_coordinate_;
  std::unordered_set<std::string> available_preview_view_ids_;

  // Current line scope context for cross-hair updates
  int current_line_scope_line_ =
      -1;  // Image Y coordinate of current line being scoped
  int current_line_scope_preview_width_ = 0;
  int current_line_scope_samples_count_ = 0;

  // Navigation
  QTimer* nav_debounce_timer_;
  QPushButton* first_button_;
  QPushButton* prev_button_;
  QPushButton* play_pause_button_;
  QPushButton* next_button_;
  QPushButton* last_button_;
  QPushButton* zoom1to1_button_;
  QPushButton* dropouts_button_;
  QSpinBox* frame_jump_spinbox_;

  // Playback
  QTimer* playback_timer_;
  bool is_playing_{false};

  void closeVectorscopeDialogs();

 protected:
  void closeEvent(QCloseEvent* event) override;
};

#endif  // PREVIEWDIALOG_H
