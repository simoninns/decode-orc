/*
 * File:        dropout_editor_dialog.h
 * Module:      orc-gui
 * Purpose:     Dropout map editor dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DROPOUT_EDITOR_DIALOG_H
#define DROPOUT_EDITOR_DIALOG_H

#include <frame_id.h>
#include <node_id.h>

#include <QDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QRubberBand>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <map>
#include <memory>
#include <vector>

#include "presenters/include/dropout_presenter.h"

/**
 * @brief Interactive widget for displaying and editing dropout regions on a
 * frame image
 *
 * This widget displays a full video frame (non-interlaced, sequential lines)
 * and allows the user to:
 * - View existing dropout regions (highlighted)
 * - Add new dropout regions by clicking and dragging
 * - Remove existing dropout regions by clicking on them
 *
 * All coordinates are frame-flat 0-based (line = 0-based frame line index,
 * start_sample/end_sample = sample within that line).
 */
class DropoutFrameView : public QLabel {
  Q_OBJECT

 public:
  explicit DropoutFrameView(QWidget* parent = nullptr);
  ~DropoutFrameView() = default;

  /**
   * @brief Set the frame to display
   * @param frame_data Frame pixel data (grayscale, frame-flat)
   * @param width Frame width in pixels (samples_per_line_nominal)
   * @param height Frame height in lines (total frame lines)
   * @param source_dropouts Existing dropout regions from source (yellow)
   * @param additions Dropout regions to add (green)
   * @param removals Dropout regions to remove (red)
   */
  void setFrame(
      const std::vector<uint8_t>& frame_data, int width, int height,
      const std::vector<orc::presenters::DropoutRegion>& source_dropouts,
      const std::vector<orc::presenters::DropoutRegion>& additions,
      const std::vector<orc::presenters::DropoutRegion>& removals);

  /**
   * @brief Get the current additions list
   */
  const std::vector<orc::presenters::DropoutRegion>& getAdditions() const {
    return additions_;
  }
  std::vector<orc::presenters::DropoutRegion>& getAdditionsMutable() {
    return additions_;
  }

  /**
   * @brief Get the current removals list
   */
  const std::vector<orc::presenters::DropoutRegion>& getRemovals() const {
    return removals_;
  }
  std::vector<orc::presenters::DropoutRegion>& getRemovalsMutable() {
    return removals_;
  }

  /**
   * @brief Clear all edits
   */
  void clearEdits();

  /**
   * @brief Update the display (redraw the frame with current data)
   */
  void updateDisplay();

  /**
   * @brief Get frame dimensions for external access
   */
  int getFrameWidth() const { return frame_width_; }
  int getFrameHeight() const { return frame_height_; }

  /**
   * @brief Get frame data for external access
   */
  const std::vector<uint8_t>& getFrameData() const { return frame_data_; }

  /**
   * @brief Get source dropouts for external access
   */
  const std::vector<orc::presenters::DropoutRegion>& getSourceDropouts() const {
    return source_dropouts_;
  }

 protected:
  QSize sizeHint() const override;
  void resizeEvent(QResizeEvent* event) override;

 signals:
  /**
   * @brief Emitted when the user modifies dropout regions
   */
  void regionsModified();

  /**
   * @brief Emitted when zoom level changes via scroll wheel
   */
  void zoomChanged(float zoom_level);

  /**
   * @brief Emitted when a new addition is created (index in additions list)
   */
  void additionCreated(int index);

  /**
   * @brief Emitted when a new removal is marked (index in removals list)
   */
  void removalCreated(int index);

  /**
   * @brief Emitted when an addition is clicked for selection
   */
  void additionClicked(int index);

  /**
   * @brief Emitted when a removal is clicked for selection
   */
  void removalClicked(int index);

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  bool isPointInRegion(int x, int y,
                       const orc::presenters::DropoutRegion& region) const;
  void removeRegionAtPoint(int x, int y);

  // Frame data
  std::vector<uint8_t> frame_data_;
  int frame_width_;
  int frame_height_;

  // Dropout regions (all coordinates are frame-flat 0-based)
  std::vector<orc::presenters::DropoutRegion>
      source_dropouts_;  // From source TBC
  std::vector<orc::presenters::DropoutRegion> additions_;
  std::vector<orc::presenters::DropoutRegion> removals_;

 public:
  // Mouse interaction state
  enum class InteractionMode { None, AddingDropout, RemovingDropout };

  InteractionMode mode_;

  // Hover/highlight region type
  enum class HoverRegionType { None, Source, Addition, Removal };

 public:
  void setZoomLevel(float zoom);
  float getZoomLevel() const { return zoom_level_; }

  /**
   * @brief Set highlighted region (e.g., from list selection)
   * @param type Type of region (Addition, Removal, None)
   * @param index Index in the respective list (-1 for none)
   */
  void setHighlightedRegion(HoverRegionType type, int index);

 private:
  QPoint drag_start_;
  QPoint drag_current_;
  bool dragging_;
  QRubberBand* rubber_band_;

  // Hover highlighting
  int hover_region_index_;
  HoverRegionType hover_region_type_;

  // Zoom support
  float zoom_level_;  // 1.0 = 100%, 2.0 = 200%, etc.
};

/**
 * @brief Dialog for editing dropout map for a stage
 *
 * This dialog allows the user to:
 * - Navigate through frames in the source
 * - Mark new dropout regions by clicking and dragging
 * - Remove false positive dropout regions
 * - Save changes back to the dropout map stage parameter
 *
 * All displayed and stored coordinates are frame-flat 0-based, matching the
 * DropoutMapStage serialisation format.
 */
class DropoutEditorDialog : public QDialog {
  Q_OBJECT

 public:
  /**
   * @brief Create a dropout editor dialog
   * @param node_id Node ID of the dropout_map stage
   * @param presenter Dropout presenter for data access
   * @param vfr_repr Source VideoFrameRepresentation handle (from DAG execution)
   * @param parent Parent widget
   */
  explicit DropoutEditorDialog(orc::NodeID node_id,
                               orc::presenters::DropoutPresenter* presenter,
                               std::shared_ptr<const void> vfr_repr,
                               QWidget* parent = nullptr);

  ~DropoutEditorDialog() = default;

  /**
   * @brief Get the edited dropout map
   * @return Map of frame IDs to dropout modifications
   */
  std::map<uint64_t, orc::presenters::FrameDropoutMap> getDropoutMap() const;

 signals:
  /**
   * @brief Emitted when the user clicks Apply (map saved but dialog stays open)
   */
  void applied();

 private slots:
  void onPreviousFrame();
  void onNextFrame();
  void onFrameNumberChanged(int value);
  void onClearCurrentFrame();
  void onRegionsModified();
  void onAddDropout();
  void onRemoveDropout();
  void onZoomIn();
  void onZoomOut();
  void onZoomReset();
  void onMoveDropoutUp();
  void onMoveDropoutDown();
  void onDeleteDropout();
  void onAdditionsListItemClicked(QListWidgetItem* item);
  void onRemovalsListItemClicked(QListWidgetItem* item);
  void onFrameViewZoomChanged(float zoom_level);
  void onAdditionCreated(int index);
  void onRemovalCreated(int index);
  void onAdditionClicked(int index);
  void onRemovalClicked(int index);
  void onAdditionsListSelectionChanged();
  void onRemovalsListSelectionChanged();
  void onApply();

 private:
  void setupUI();
  void loadFrame(orc::FrameID frame_id);
  void saveCurrentFrame();
  void updateFrameInfo();
  void keyPressEvent(QKeyEvent* event) override;

  void updateButtonStatesForSelection(bool is_addition);

  // Presenter and node info
  orc::NodeID node_id_;
  orc::presenters::DropoutPresenter* presenter_;

  // Source data
  std::shared_ptr<const void> vfr_repr_;

  // Current state
  orc::FrameID current_frame_id_;
  size_t total_frames_;
  std::map<uint64_t, orc::presenters::FrameDropoutMap> dropout_map_;

  // UI elements
  QSpinBox* frame_spin_box_;
  QLabel* frame_info_label_;
  QPushButton* prev_button_;
  QPushButton* next_button_;
  QPushButton* clear_frame_button_;
  QPushButton* add_dropout_button_;
  QPushButton* remove_dropout_button_;
  QListWidget* additions_list_;
  QListWidget* removals_list_;
  DropoutFrameView* frame_view_;
  QScrollArea* scroll_area_;
  QPushButton* zoom_in_button_;
  QPushButton* zoom_out_button_;
  QPushButton* zoom_reset_button_;
  QLabel* zoom_label_;
  QPushButton* move_up_button_;
  QPushButton* move_down_button_;
  QPushButton* delete_dropout_button_;
  int selected_addition_index_;
  int selected_removal_index_;

  // Interaction mode
  enum class EditMode { Add, Remove };
  EditMode edit_mode_;
};

#endif  // DROPOUT_EDITOR_DIALOG_H
