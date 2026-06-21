/*
 * File:        dropout_editor_dialog.cpp
 * Module:      orc-gui
 * Purpose:     Dropout map editor dialog implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dropout_editor_dialog.h"

#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <algorithm>

#include "field_frame_presentation.h"
#include "logging.h"

// ============================================================================
// DropoutFrameView Implementation
// ============================================================================

DropoutFrameView::DropoutFrameView(QWidget* parent)
    : QLabel(parent),
      frame_width_(0),
      frame_height_(0),
      mode_(InteractionMode::None),
      dragging_(false),
      rubber_band_(new QRubberBand(QRubberBand::Rectangle, this)),
      hover_region_index_(-1),
      hover_region_type_(HoverRegionType::None),
      zoom_level_(1.0f) {
  setAlignment(Qt::AlignCenter);
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  setScaledContents(false);
  setFrameStyle(QFrame::Box | QFrame::Sunken);
  setMouseTracking(true);
  setCursor(Qt::CrossCursor);
  rubber_band_->hide();

  QPalette palette;
  palette.setBrush(QPalette::Highlight, QBrush(QColor(0, 120, 215, 100)));
  rubber_band_->setPalette(palette);
}

void DropoutFrameView::setFrame(
    const std::vector<uint8_t>& frame_data, int width, int height,
    const std::vector<orc::presenters::DropoutRegion>& source_dropouts,
    const std::vector<orc::presenters::DropoutRegion>& additions,
    const std::vector<orc::presenters::DropoutRegion>& removals) {
  frame_data_ = frame_data;
  frame_width_ = width;
  frame_height_ = height;
  source_dropouts_ = source_dropouts;
  additions_ = additions;
  removals_ = removals;
  updateDisplay();
}

void DropoutFrameView::clearEdits() {
  additions_.clear();
  removals_.clear();
  updateDisplay();
  Q_EMIT regionsModified();
}

QSize DropoutFrameView::sizeHint() const {
  if (frame_width_ > 0 && frame_height_ > 0) {
    return QSize(
        static_cast<int>(static_cast<float>(frame_width_) * zoom_level_),
        static_cast<int>(static_cast<float>(frame_height_) * zoom_level_));
  }
  return QSize(800, 600);
}

void DropoutFrameView::resizeEvent(QResizeEvent* event) {
  QLabel::resizeEvent(event);
  if (!frame_data_.empty() && frame_width_ > 0 && frame_height_ > 0) {
    updateDisplay();
  }
}

void DropoutFrameView::wheelEvent(QWheelEvent* event) {
  if (event->modifiers() == Qt::NoModifier) {
    float delta = static_cast<float>(event->angleDelta().y()) / 120.0f;
    float zoom_factor = 1.0f + (delta * 0.1f);
    float old_zoom = zoom_level_;
    float new_zoom = std::max(0.5f, std::min(4.0f, old_zoom * zoom_factor));

    if (new_zoom != zoom_level_) {
      QScrollArea* scroll_area =
          qobject_cast<QScrollArea*>(parentWidget()->parentWidget());
      if (scroll_area) {
        QPoint viewport_pos = scroll_area->viewport()->mapFromGlobal(
            event->globalPosition().toPoint());

        int old_h_scroll = scroll_area->horizontalScrollBar()->value();
        int old_v_scroll = scroll_area->verticalScrollBar()->value();

        float content_x = static_cast<float>(old_h_scroll) +
                          static_cast<float>(viewport_pos.x());
        float content_y = static_cast<float>(old_v_scroll) +
                          static_cast<float>(viewport_pos.y());

        zoom_level_ = new_zoom;
        updateDisplay();

        float zoom_ratio = new_zoom / old_zoom;
        int new_h_scroll = static_cast<int>(
            content_x * zoom_ratio - static_cast<float>(viewport_pos.x()));
        int new_v_scroll = static_cast<int>(
            content_y * zoom_ratio - static_cast<float>(viewport_pos.y()));

        scroll_area->horizontalScrollBar()->setValue(new_h_scroll);
        scroll_area->verticalScrollBar()->setValue(new_v_scroll);

        Q_EMIT zoomChanged(zoom_level_);
      } else {
        zoom_level_ = new_zoom;
        updateDisplay();
        Q_EMIT zoomChanged(zoom_level_);
      }
    }

    event->accept();
  } else {
    QLabel::wheelEvent(event);
  }
}

void DropoutFrameView::setZoomLevel(float zoom) {
  zoom_level_ = std::max(0.5f, std::min(4.0f, zoom));
  updateDisplay();
}

void DropoutFrameView::setHighlightedRegion(HoverRegionType type, int index) {
  hover_region_type_ = type;
  hover_region_index_ = index;
  updateDisplay();
}

void DropoutFrameView::updateDisplay() {
  if (frame_data_.empty() || frame_width_ == 0 || frame_height_ == 0) {
    setText("No frame data");
    return;
  }

  QImage image(frame_width_, frame_height_, QImage::Format_RGB32);

  for (int y = 0; y < frame_height_; ++y) {
    for (int x = 0; x < frame_width_; ++x) {
      int idx = y * frame_width_ + x;
      if (idx < static_cast<int>(frame_data_.size())) {
        uint8_t val = frame_data_[idx];
        image.setPixel(x, y, qRgb(val, val, val));
      }
    }
  }

  QPainter painter(&image);
  painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

  int line_thickness = std::max(3, std::min(6, frame_height_ / 100));
  int hover_thickness = line_thickness + 2;

  // Draw source dropouts in red (semi-transparent)
  for (size_t i = 0; i < source_dropouts_.size(); ++i) {
    const auto& region = source_dropouts_[i];
    bool is_hovered = (hover_region_type_ == HoverRegionType::Source &&
                       hover_region_index_ == static_cast<int>(i));
    QColor color = is_hovered ? QColor(255, 0, 0, 192) : QColor(255, 0, 0, 128);
    int thickness = is_hovered ? hover_thickness : line_thickness;

    int line = static_cast<int>(region.line);
    int start = static_cast<int>(region.start_sample);
    int end = static_cast<int>(region.end_sample);
    if (line >= 0 && line < frame_height_ && start >= 0 &&
        end <= frame_width_ && start < end) {
      painter.fillRect(start, line - thickness / 2, end - start, thickness,
                       color);
    }
  }

  // Draw additions in green (semi-transparent)
  for (size_t i = 0; i < additions_.size(); ++i) {
    const auto& region = additions_[i];
    bool is_hovered = (hover_region_type_ == HoverRegionType::Addition &&
                       hover_region_index_ == static_cast<int>(i));
    QColor color = is_hovered ? QColor(0, 255, 0, 192) : QColor(0, 255, 0, 128);
    int thickness = is_hovered ? hover_thickness : line_thickness;

    int line = static_cast<int>(region.line);
    int start = static_cast<int>(region.start_sample);
    int end = static_cast<int>(region.end_sample);
    if (line >= 0 && line < frame_height_ && start >= 0 &&
        end <= frame_width_ && start < end) {
      painter.fillRect(start, line - thickness / 2, end - start, thickness,
                       color);
    }
  }

  // Draw removals in yellow (semi-transparent)
  for (size_t i = 0; i < removals_.size(); ++i) {
    const auto& region = removals_[i];
    bool is_hovered = (hover_region_type_ == HoverRegionType::Removal &&
                       hover_region_index_ == static_cast<int>(i));
    QColor color =
        is_hovered ? QColor(255, 255, 0, 192) : QColor(255, 255, 0, 128);
    int thickness = is_hovered ? hover_thickness : line_thickness;

    int line = static_cast<int>(region.line);
    int start = static_cast<int>(region.start_sample);
    int end = static_cast<int>(region.end_sample);
    if (line >= 0 && line < frame_height_ && start >= 0 &&
        end <= frame_width_ && start < end) {
      painter.fillRect(start, line - thickness / 2, end - start, thickness,
                       color);
    }
  }

  QPixmap pixmap = QPixmap::fromImage(image);

  int zoomed_width =
      static_cast<int>(static_cast<float>(frame_width_) * zoom_level_);
  int zoomed_height =
      static_cast<int>(static_cast<float>(frame_height_) * zoom_level_);

  resize(zoomed_width, zoomed_height);
  setFixedSize(zoomed_width, zoomed_height);

  setPixmap(pixmap.scaled(zoomed_width, zoomed_height, Qt::IgnoreAspectRatio,
                          Qt::SmoothTransformation));
}

void DropoutFrameView::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || frame_width_ == 0 ||
      frame_height_ == 0) {
    return;
  }

  QPixmap pm = pixmap();
  if (pm.isNull()) {
    return;
  }

  QSize pm_size = pm.size();
  int pm_x = (width() - pm_size.width()) / 2;
  int pm_y = (height() - pm_size.height()) / 2;

  int click_x = event->pos().x() - pm_x;
  int click_y = event->pos().y() - pm_y;

  if (click_x < 0 || click_x >= pm_size.width() || click_y < 0 ||
      click_y >= pm_size.height()) {
    return;
  }

  float scale_x =
      static_cast<float>(frame_width_) / static_cast<float>(pm_size.width());
  float scale_y =
      static_cast<float>(frame_height_) / static_cast<float>(pm_size.height());
  int frame_x = static_cast<int>(static_cast<float>(click_x) * scale_x);
  int frame_y = static_cast<int>(static_cast<float>(click_y) * scale_y);

  if (hover_region_index_ >= 0) {
    if (hover_region_type_ == HoverRegionType::Addition) {
      Q_EMIT additionClicked(hover_region_index_);
      return;
    } else if (hover_region_type_ == HoverRegionType::Removal) {
      Q_EMIT removalClicked(hover_region_index_);
      return;
    }
  }

  if (mode_ == InteractionMode::RemovingDropout) {
    removeRegionAtPoint(frame_x, frame_y);
    return;
  }

  if (mode_ == InteractionMode::AddingDropout) {
    dragging_ = true;
    drag_start_ = QPoint(frame_x, frame_y);
    drag_current_ = drag_start_;

    rubber_band_->setGeometry(QRect(event->pos(), QSize()));
    rubber_band_->show();
  }
}

void DropoutFrameView::mouseMoveEvent(QMouseEvent* event) {
  QPixmap pm = pixmap();
  if (pm.isNull()) {
    return;
  }

  QSize pm_size = pm.size();
  int pm_x = (width() - pm_size.width()) / 2;
  int pm_y = (height() - pm_size.height()) / 2;

  int mouse_x = event->pos().x() - pm_x;
  int mouse_y = event->pos().y() - pm_y;

  mouse_x = std::max(0, std::min(mouse_x, pm_size.width() - 1));
  mouse_y = std::max(0, std::min(mouse_y, pm_size.height() - 1));

  float scale_x =
      static_cast<float>(frame_width_) / static_cast<float>(pm_size.width());
  float scale_y =
      static_cast<float>(frame_height_) / static_cast<float>(pm_size.height());
  int frame_x = static_cast<int>(static_cast<float>(mouse_x) * scale_x);
  int frame_y = static_cast<int>(static_cast<float>(mouse_y) * scale_y);

  if (dragging_ && mode_ == InteractionMode::AddingDropout) {
    drag_current_ = QPoint(frame_x, frame_y);

    int widget_start_x =
        (drag_start_.x() * pm_size.width() / frame_width_) + pm_x;
    int widget_start_y =
        (drag_start_.y() * pm_size.height() / frame_height_) + pm_y;
    int widget_current_x = event->pos().x();

    int line_height = 3;
    QRect line_rect(std::min(widget_start_x, widget_current_x),
                    widget_start_y - line_height / 2,
                    std::abs(widget_current_x - widget_start_x), line_height);

    rubber_band_->setGeometry(line_rect);
  } else {
    int old_hover_index = hover_region_index_;
    HoverRegionType old_hover_type = hover_region_type_;

    hover_region_index_ = -1;
    hover_region_type_ = HoverRegionType::None;

    for (size_t i = 0; i < source_dropouts_.size(); ++i) {
      if (isPointInRegion(frame_x, frame_y, source_dropouts_[i])) {
        hover_region_index_ = static_cast<int>(i);
        hover_region_type_ = HoverRegionType::Source;
        break;
      }
    }

    if (hover_region_index_ == -1) {
      for (size_t i = 0; i < additions_.size(); ++i) {
        if (isPointInRegion(frame_x, frame_y, additions_[i])) {
          hover_region_index_ = static_cast<int>(i);
          hover_region_type_ = HoverRegionType::Addition;
          break;
        }
      }
    }

    if (hover_region_index_ == -1) {
      for (size_t i = 0; i < removals_.size(); ++i) {
        if (isPointInRegion(frame_x, frame_y, removals_[i])) {
          hover_region_index_ = static_cast<int>(i);
          hover_region_type_ = HoverRegionType::Removal;
          break;
        }
      }
    }

    if (hover_region_index_ != old_hover_index ||
        hover_region_type_ != old_hover_type) {
      updateDisplay();
    }
  }
}

void DropoutFrameView::mouseReleaseEvent(QMouseEvent* event) {
  if (!dragging_ || event->button() != Qt::LeftButton ||
      mode_ != InteractionMode::AddingDropout) {
    return;
  }

  dragging_ = false;
  rubber_band_->hide();

  int line = drag_start_.y();
  int start_sample = std::min(drag_start_.x(), drag_current_.x());
  int end_sample = std::max(drag_start_.x(), drag_current_.x());

  if (end_sample > start_sample) {
    orc::presenters::DropoutRegion region;
    region.line = static_cast<uint32_t>(line);
    region.start_sample = static_cast<uint32_t>(start_sample);
    region.end_sample = static_cast<uint32_t>(end_sample);
    region.basis = orc::presenters::DropoutRegion::DetectionBasis::HINT_DERIVED;

    additions_.push_back(region);
    int new_index = static_cast<int>(additions_.size()) - 1;
    updateDisplay();
    Q_EMIT regionsModified();
    Q_EMIT additionCreated(new_index);
  }
}

bool DropoutFrameView::isPointInRegion(
    int x, int y, const orc::presenters::DropoutRegion& region) const {
  return static_cast<uint32_t>(y) == region.line &&
         static_cast<uint32_t>(x) >= region.start_sample &&
         static_cast<uint32_t>(x) < region.end_sample;
}

void DropoutFrameView::removeRegionAtPoint(int x, int y) {
  for (auto it = additions_.begin(); it != additions_.end(); ++it) {
    if (isPointInRegion(x, y, *it)) {
      additions_.erase(it);
      hover_region_index_ = -1;
      hover_region_type_ = HoverRegionType::None;
      updateDisplay();
      Q_EMIT regionsModified();
      return;
    }
  }

  for (auto it = removals_.begin(); it != removals_.end(); ++it) {
    if (isPointInRegion(x, y, *it)) {
      removals_.erase(it);
      hover_region_index_ = -1;
      hover_region_type_ = HoverRegionType::None;
      updateDisplay();
      Q_EMIT regionsModified();
      return;
    }
  }

  for (const auto& region : source_dropouts_) {
    if (isPointInRegion(x, y, region)) {
      bool already_removed = false;
      for (const auto& removal : removals_) {
        if (removal.line == region.line &&
            removal.start_sample == region.start_sample &&
            removal.end_sample == region.end_sample) {
          already_removed = true;
          break;
        }
      }

      if (!already_removed) {
        removals_.push_back(region);
        int new_index = static_cast<int>(removals_.size()) - 1;
        updateDisplay();
        Q_EMIT regionsModified();
        Q_EMIT removalCreated(new_index);
      }
      return;
    }
  }
}

// ============================================================================
// DropoutEditorDialog Implementation
// ============================================================================

DropoutEditorDialog::DropoutEditorDialog(
    orc::NodeID node_id, orc::presenters::DropoutPresenter* presenter,
    std::shared_ptr<const void> vfr_repr, QWidget* parent)
    : QDialog(parent),
      node_id_(node_id),
      presenter_(presenter),
      vfr_repr_(vfr_repr),
      current_frame_id_(0),
      total_frames_(0),
      edit_mode_(EditMode::Add),
      selected_addition_index_(-1),
      selected_removal_index_(-1) {
  if (!presenter_) {
    throw std::invalid_argument("Presenter cannot be null");
  }

  dropout_map_ = presenter_->getDropoutMap(node_id_);

  if (vfr_repr_) {
    std::shared_ptr<void> vfr_non_const =
        std::const_pointer_cast<void>(vfr_repr_);
    total_frames_ = presenter_->getFrameCount(vfr_non_const);
    ORC_LOG_DEBUG("DropoutEditorDialog: loaded {} total frames", total_frames_);
  } else {
    ORC_LOG_ERROR("DropoutEditorDialog: vfr_repr is null");
  }

  setupUI();

  if (total_frames_ > 0) {
    loadFrame(0);
  } else {
    ORC_LOG_WARN("DropoutEditorDialog: No frames available (total_frames_={})",
                 total_frames_);
  }
}

void DropoutEditorDialog::setupUI() {
  setWindowTitle("Dropout Map Editor");
  setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint |
                 Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
  resize(1000, 700);

  auto* main_layout = new QVBoxLayout(this);

  // Frame navigation controls
  auto* nav_group = new QGroupBox("Frame Navigation");
  auto* nav_layout = new QHBoxLayout(nav_group);

  prev_button_ = new QPushButton("Previous");
  prev_button_->setAutoRepeat(true);
  prev_button_->setAutoRepeatDelay(500);
  prev_button_->setAutoRepeatInterval(100);
  connect(prev_button_, &QPushButton::clicked, this,
          &DropoutEditorDialog::onPreviousFrame);
  nav_layout->addWidget(prev_button_);

  frame_spin_box_ = new QSpinBox();
  frame_spin_box_->setMinimum(1);
  frame_spin_box_->setMaximum(
      static_cast<int>(total_frames_ > 0 ? total_frames_ : 1));
  frame_spin_box_->setValue(1);
  connect(frame_spin_box_, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &DropoutEditorDialog::onFrameNumberChanged);
  nav_layout->addWidget(new QLabel("Frame:"));
  nav_layout->addWidget(frame_spin_box_);

  next_button_ = new QPushButton("Next");
  next_button_->setAutoRepeat(true);
  next_button_->setAutoRepeatDelay(500);
  next_button_->setAutoRepeatInterval(100);
  connect(next_button_, &QPushButton::clicked, this,
          &DropoutEditorDialog::onNextFrame);
  nav_layout->addWidget(next_button_);

  frame_info_label_ = new QLabel();
  nav_layout->addWidget(frame_info_label_);
  nav_layout->addStretch();

  nav_layout->addWidget(new QLabel("Zoom:"));

  zoom_out_button_ = new QPushButton("-");
  zoom_out_button_->setMaximumWidth(40);
  connect(zoom_out_button_, &QPushButton::clicked, this,
          &DropoutEditorDialog::onZoomOut);
  nav_layout->addWidget(zoom_out_button_);

  zoom_reset_button_ = new QPushButton("100%");
  zoom_reset_button_->setMaximumWidth(60);
  connect(zoom_reset_button_, &QPushButton::clicked, this,
          &DropoutEditorDialog::onZoomReset);
  nav_layout->addWidget(zoom_reset_button_);

  zoom_in_button_ = new QPushButton("+");
  zoom_in_button_->setMaximumWidth(40);
  connect(zoom_in_button_, &QPushButton::clicked, this,
          &DropoutEditorDialog::onZoomIn);
  nav_layout->addWidget(zoom_in_button_);

  zoom_label_ = new QLabel("100%");
  nav_layout->addWidget(zoom_label_);

  main_layout->addWidget(nav_group);

  // Frame view wrapped in scroll area
  scroll_area_ = new QScrollArea();
  scroll_area_->setWidgetResizable(false);
  scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scroll_area_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scroll_area_->setAlignment(Qt::AlignCenter);
  scroll_area_->setFrameShape(QFrame::StyledPanel);
  frame_view_ = new DropoutFrameView();
  frame_view_->setMinimumSize(400, 300);
  connect(frame_view_, &DropoutFrameView::regionsModified, this,
          &DropoutEditorDialog::onRegionsModified);
  connect(frame_view_, &DropoutFrameView::zoomChanged, this,
          &DropoutEditorDialog::onFrameViewZoomChanged);
  connect(frame_view_, &DropoutFrameView::additionCreated, this,
          &DropoutEditorDialog::onAdditionCreated);
  connect(frame_view_, &DropoutFrameView::removalCreated, this,
          &DropoutEditorDialog::onRemovalCreated);
  connect(frame_view_, &DropoutFrameView::additionClicked, this,
          &DropoutEditorDialog::onAdditionClicked);
  connect(frame_view_, &DropoutFrameView::removalClicked, this,
          &DropoutEditorDialog::onRemovalClicked);

  scroll_area_->setWidget(frame_view_);
  main_layout->addWidget(scroll_area_, 3);

  // Control panel
  auto* control_layout = new QHBoxLayout();

  auto* controls_group = new QGroupBox("Controls");
  auto* controls_vlayout = new QVBoxLayout(controls_group);

  add_dropout_button_ = new QPushButton("Add Dropout");
  add_dropout_button_->setCheckable(true);
  add_dropout_button_->setChecked(true);
  connect(add_dropout_button_, &QPushButton::clicked, this,
          &DropoutEditorDialog::onAddDropout);
  controls_vlayout->addWidget(add_dropout_button_);

  remove_dropout_button_ = new QPushButton("Remove Dropout");
  remove_dropout_button_->setCheckable(true);
  connect(remove_dropout_button_, &QPushButton::clicked, this,
          &DropoutEditorDialog::onRemoveDropout);
  controls_vlayout->addWidget(remove_dropout_button_);

  clear_frame_button_ = new QPushButton("Clear Current Frame");
  connect(clear_frame_button_, &QPushButton::clicked, this,
          &DropoutEditorDialog::onClearCurrentFrame);
  controls_vlayout->addWidget(clear_frame_button_);

  control_layout->addWidget(controls_group);

  auto* adjust_group = new QGroupBox("Adjust Selected Dropout");
  auto* adjust_layout = new QVBoxLayout(adjust_group);

  move_up_button_ = new QPushButton("Move Up ↑");
  move_up_button_->setEnabled(false);
  connect(move_up_button_, &QPushButton::clicked, this,
          &DropoutEditorDialog::onMoveDropoutUp);
  adjust_layout->addWidget(move_up_button_);

  move_down_button_ = new QPushButton("Move Down ↓");
  move_down_button_->setEnabled(false);
  connect(move_down_button_, &QPushButton::clicked, this,
          &DropoutEditorDialog::onMoveDropoutDown);
  adjust_layout->addWidget(move_down_button_);

  delete_dropout_button_ = new QPushButton("Delete");
  delete_dropout_button_->setEnabled(false);
  connect(delete_dropout_button_, &QPushButton::clicked, this,
          &DropoutEditorDialog::onDeleteDropout);
  adjust_layout->addWidget(delete_dropout_button_);

  adjust_layout->addWidget(
      new QLabel("Click a dropout in the\nlist to select it"));

  control_layout->addWidget(adjust_group);

  auto* additions_group = new QGroupBox("Additions (Green)");
  auto* additions_layout = new QVBoxLayout(additions_group);
  additions_list_ = new QListWidget();
  connect(additions_list_, &QListWidget::itemClicked, this,
          &DropoutEditorDialog::onAdditionsListItemClicked);
  connect(additions_list_, &QListWidget::itemSelectionChanged, this,
          &DropoutEditorDialog::onAdditionsListSelectionChanged);
  additions_layout->addWidget(additions_list_);
  control_layout->addWidget(additions_group);

  auto* removals_group = new QGroupBox("Removals (Yellow)");
  auto* removals_layout = new QVBoxLayout(removals_group);
  removals_list_ = new QListWidget();
  connect(removals_list_, &QListWidget::itemClicked, this,
          &DropoutEditorDialog::onRemovalsListItemClicked);
  connect(removals_list_, &QListWidget::itemSelectionChanged, this,
          &DropoutEditorDialog::onRemovalsListSelectionChanged);
  removals_layout->addWidget(removals_list_);
  control_layout->addWidget(removals_group);

  main_layout->addLayout(control_layout);

  auto* button_box =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply |
                           QDialogButtonBox::Cancel);
  connect(button_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(button_box->button(QDialogButtonBox::Apply), &QPushButton::clicked,
          this, &DropoutEditorDialog::onApply);
  main_layout->addWidget(button_box);

  frame_view_->mode_ = DropoutFrameView::InteractionMode::AddingDropout;
}

void DropoutEditorDialog::loadFrame(orc::FrameID frame_id) {
  if (!vfr_repr_ || frame_id >= static_cast<orc::FrameID>(total_frames_)) {
    return;
  }

  if (current_frame_id_ != frame_id &&
      current_frame_id_ < static_cast<orc::FrameID>(total_frames_)) {
    saveCurrentFrame();
  }

  current_frame_id_ = frame_id;

  int width = 0, height = 0;
  std::shared_ptr<void> vfr_non_const =
      std::const_pointer_cast<void>(vfr_repr_);
  std::vector<uint8_t> frame_data =
      presenter_->getFrameData(vfr_non_const, frame_id, width, height);

  ORC_LOG_DEBUG(
      "DropoutEditorDialog::loadFrame: frame_id={}, returned {} bytes, "
      "width={}, height={}",
      frame_id, frame_data.size(), width, height);

  if (frame_data.empty() || width == 0 || height == 0) {
    ORC_LOG_ERROR("Failed to get frame data for frame {}", frame_id);
    return;
  }

  std::vector<orc::presenters::DropoutRegion> source_dropouts =
      presenter_->getSourceDropouts(vfr_non_const, frame_id);

  std::vector<orc::presenters::DropoutRegion> additions;
  std::vector<orc::presenters::DropoutRegion> removals;

  auto it = dropout_map_.find(frame_id);
  if (it != dropout_map_.end()) {
    additions = it->second.additions;
    removals = it->second.removals;
  }

  frame_view_->setFrame(frame_data, width, height, source_dropouts, additions,
                        removals);

  // On first load, fit the frame to the scroll area
  if (frame_id == 0 && total_frames_ > 0) {
    int available_width = scroll_area_->width() - 20;
    int available_height = scroll_area_->height() - 20;

    float zoom_x =
        static_cast<float>(available_width) / static_cast<float>(width);
    float zoom_y =
        static_cast<float>(available_height) / static_cast<float>(height);
    float fit_zoom = std::max(0.5f, std::min(1.0f, std::min(zoom_x, zoom_y)));

    frame_view_->setZoomLevel(fit_zoom);
    zoom_label_->setText(QString("%1%").arg(static_cast<int>(fit_zoom * 100)));
    zoom_reset_button_->setText(
        QString("%1%").arg(static_cast<int>(fit_zoom * 100)));
  }

  updateFrameInfo();
}

void DropoutEditorDialog::saveCurrentFrame() {
  if (!vfr_repr_ ||
      current_frame_id_ >= static_cast<orc::FrameID>(total_frames_)) {
    return;
  }

  auto additions = frame_view_->getAdditions();
  auto removals = frame_view_->getRemovals();

  if (additions.empty() && removals.empty()) {
    dropout_map_.erase(current_frame_id_);
  } else {
    orc::presenters::FrameDropoutMap& frame_map =
        dropout_map_[current_frame_id_];
    frame_map.frame_id = current_frame_id_;
    frame_map.additions = additions;
    frame_map.removals = removals;
  }
}

void DropoutEditorDialog::updateFrameInfo() {
  auto additions = frame_view_->getAdditions();
  auto removals = frame_view_->getRemovals();

  frame_info_label_->setText(
      QString("Frame %1 of %2 - Additions: %3, Removals: %4")
          .arg(current_frame_id_ + 1)
          .arg(total_frames_)
          .arg(additions.size())
          .arg(removals.size()));

  additions_list_->clear();
  for (const auto& region : additions) {
    additions_list_->addItem(QString("Line %1: [%2, %3)")
                                 .arg(region.line)
                                 .arg(region.start_sample)
                                 .arg(region.end_sample));
  }

  removals_list_->clear();
  for (const auto& region : removals) {
    removals_list_->addItem(QString("Line %1: [%2, %3)")
                                .arg(region.line)
                                .arg(region.start_sample)
                                .arg(region.end_sample));
  }

  prev_button_->setEnabled(current_frame_id_ > 0);
  next_button_->setEnabled(current_frame_id_ <
                           static_cast<orc::FrameID>(total_frames_) - 1);
}

void DropoutEditorDialog::onPreviousFrame() {
  if (current_frame_id_ > 0) {
    // Spin box is 1-based: frame N (0-based) is shown as N+1. Going to N-1
    // means setting the spin box to N (current_frame_id_ without +1 offset).
    frame_spin_box_->setValue(static_cast<int>(current_frame_id_));
  }
}

void DropoutEditorDialog::onNextFrame() {
  if (current_frame_id_ < static_cast<orc::FrameID>(total_frames_) - 1) {
    // Spin box is 1-based: going to N+1 (0-based) means setting to N+2.
    frame_spin_box_->setValue(static_cast<int>(current_frame_id_ + 2));
  }
}

void DropoutEditorDialog::onFrameNumberChanged(int value) {
  // Spin box is 1-based; convert to 0-based FrameID before loading.
  loadFrame(static_cast<orc::FrameID>(value - 1));
}

void DropoutEditorDialog::onClearCurrentFrame() { frame_view_->clearEdits(); }

void DropoutEditorDialog::onRegionsModified() { updateFrameInfo(); }

void DropoutEditorDialog::onAddDropout() {
  edit_mode_ = EditMode::Add;
  add_dropout_button_->setChecked(true);
  remove_dropout_button_->setChecked(false);
  frame_view_->mode_ = DropoutFrameView::InteractionMode::AddingDropout;
}

void DropoutEditorDialog::onRemoveDropout() {
  edit_mode_ = EditMode::Remove;
  add_dropout_button_->setChecked(false);
  remove_dropout_button_->setChecked(true);
  frame_view_->mode_ = DropoutFrameView::InteractionMode::RemovingDropout;
}

std::map<uint64_t, orc::presenters::FrameDropoutMap>
DropoutEditorDialog::getDropoutMap() const {
  auto map = dropout_map_;

  auto additions = frame_view_->getAdditions();
  auto removals = frame_view_->getRemovals();

  if (additions.empty() && removals.empty()) {
    map.erase(current_frame_id_);
  } else {
    orc::presenters::FrameDropoutMap& frame_map = map[current_frame_id_];
    frame_map.frame_id = current_frame_id_;
    frame_map.additions = additions;
    frame_map.removals = removals;
  }

  return map;
}

void DropoutEditorDialog::onZoomIn() {
  float current_zoom = frame_view_->getZoomLevel();
  float new_zoom = std::min(4.0f, current_zoom * 1.25f);
  frame_view_->setZoomLevel(new_zoom);
  zoom_label_->setText(QString("%1%").arg(static_cast<int>(new_zoom * 100)));
  zoom_reset_button_->setText(
      QString("%1%").arg(static_cast<int>(new_zoom * 100)));
}

void DropoutEditorDialog::onZoomOut() {
  float current_zoom = frame_view_->getZoomLevel();
  float new_zoom = std::max(0.5f, current_zoom / 1.25f);
  frame_view_->setZoomLevel(new_zoom);
  zoom_label_->setText(QString("%1%").arg(static_cast<int>(new_zoom * 100)));
  zoom_reset_button_->setText(
      QString("%1%").arg(static_cast<int>(new_zoom * 100)));
}

void DropoutEditorDialog::onZoomReset() {
  frame_view_->setZoomLevel(1.0f);
  zoom_label_->setText("100%");
  zoom_reset_button_->setText("100%");
}

void DropoutEditorDialog::onMoveDropoutUp() {
  if (selected_addition_index_ >= 0) {
    auto& additions = frame_view_->getAdditionsMutable();
    if (selected_addition_index_ < static_cast<int>(additions.size())) {
      auto& region = additions[selected_addition_index_];
      if (region.line > 0) {
        region.line--;
        frame_view_->updateDisplay();
        updateFrameInfo();
        additions_list_->setCurrentRow(selected_addition_index_);
      }
    }
  } else if (selected_removal_index_ >= 0) {
    auto& removals = frame_view_->getRemovalsMutable();
    if (selected_removal_index_ < static_cast<int>(removals.size())) {
      auto& region = removals[selected_removal_index_];
      if (region.line > 0) {
        region.line--;
        frame_view_->updateDisplay();
        updateFrameInfo();
        removals_list_->setCurrentRow(selected_removal_index_);
      }
    }
  }
}

void DropoutEditorDialog::onMoveDropoutDown() {
  if (selected_addition_index_ >= 0) {
    auto& additions = frame_view_->getAdditionsMutable();
    if (selected_addition_index_ < static_cast<int>(additions.size())) {
      auto& region = additions[selected_addition_index_];
      if (region.line <
          static_cast<uint32_t>(frame_view_->getFrameHeight()) - 1) {
        region.line++;
        frame_view_->updateDisplay();
        updateFrameInfo();
        additions_list_->setCurrentRow(selected_addition_index_);
      }
    }
  } else if (selected_removal_index_ >= 0) {
    auto& removals = frame_view_->getRemovalsMutable();
    if (selected_removal_index_ < static_cast<int>(removals.size())) {
      auto& region = removals[selected_removal_index_];
      if (region.line <
          static_cast<uint32_t>(frame_view_->getFrameHeight()) - 1) {
        region.line++;
        frame_view_->updateDisplay();
        updateFrameInfo();
        removals_list_->setCurrentRow(selected_removal_index_);
      }
    }
  }
}

void DropoutEditorDialog::onAdditionsListItemClicked(QListWidgetItem* item) {
  selected_addition_index_ = additions_list_->row(item);
  selected_removal_index_ = -1;
  removals_list_->clearSelection();
  updateButtonStatesForSelection(true);
}

void DropoutEditorDialog::onRemovalsListItemClicked(QListWidgetItem* item) {
  selected_removal_index_ = removals_list_->row(item);
  selected_addition_index_ = -1;
  additions_list_->clearSelection();
  updateButtonStatesForSelection(false);
}

void DropoutEditorDialog::updateButtonStatesForSelection(bool is_addition) {
  if (is_addition) {
    move_up_button_->setEnabled(true);
    move_down_button_->setEnabled(true);
    delete_dropout_button_->setEnabled(true);
  } else {
    move_up_button_->setEnabled(false);
    move_down_button_->setEnabled(false);
    delete_dropout_button_->setEnabled(true);
  }
}

void DropoutEditorDialog::onFrameViewZoomChanged(float zoom_level) {
  zoom_label_->setText(QString("%1%").arg(static_cast<int>(zoom_level * 100)));
  zoom_reset_button_->setText(
      QString("%1%").arg(static_cast<int>(zoom_level * 100)));
}

void DropoutEditorDialog::onAdditionCreated(int index) {
  selected_addition_index_ = index;
  selected_removal_index_ = -1;

  updateFrameInfo();

  if (index >= 0 && index < additions_list_->count()) {
    additions_list_->setCurrentRow(index);
    removals_list_->clearSelection();
    updateButtonStatesForSelection(true);
  }
}

void DropoutEditorDialog::onRemovalCreated(int index) {
  selected_removal_index_ = index;
  selected_addition_index_ = -1;

  updateFrameInfo();

  if (index >= 0 && index < removals_list_->count()) {
    removals_list_->setCurrentRow(index);
    additions_list_->clearSelection();
    updateButtonStatesForSelection(false);
  }
}

void DropoutEditorDialog::onAdditionClicked(int index) {
  selected_addition_index_ = index;
  selected_removal_index_ = -1;

  if (index >= 0 && index < additions_list_->count()) {
    additions_list_->setCurrentRow(index);
    removals_list_->clearSelection();
    updateButtonStatesForSelection(true);
  }
}

void DropoutEditorDialog::onRemovalClicked(int index) {
  selected_removal_index_ = index;
  selected_addition_index_ = -1;

  if (index >= 0 && index < removals_list_->count()) {
    removals_list_->setCurrentRow(index);
    additions_list_->clearSelection();
    updateButtonStatesForSelection(false);
  }
}

void DropoutEditorDialog::onAdditionsListSelectionChanged() {
  int current_row = additions_list_->currentRow();

  if (current_row >= 0) {
    removals_list_->clearSelection();
    frame_view_->setHighlightedRegion(
        DropoutFrameView::HoverRegionType::Addition, current_row);
    selected_addition_index_ = current_row;
    selected_removal_index_ = -1;
    updateButtonStatesForSelection(true);
  } else {
    if (removals_list_->currentRow() < 0) {
      frame_view_->setHighlightedRegion(DropoutFrameView::HoverRegionType::None,
                                        -1);
    }
    selected_addition_index_ = -1;
  }
}

void DropoutEditorDialog::onRemovalsListSelectionChanged() {
  int current_row = removals_list_->currentRow();

  if (current_row >= 0) {
    additions_list_->clearSelection();
    frame_view_->setHighlightedRegion(
        DropoutFrameView::HoverRegionType::Removal, current_row);
    selected_removal_index_ = current_row;
    selected_addition_index_ = -1;
    updateButtonStatesForSelection(false);
  } else {
    if (additions_list_->currentRow() < 0) {
      frame_view_->setHighlightedRegion(DropoutFrameView::HoverRegionType::None,
                                        -1);
    }
    selected_removal_index_ = -1;
  }
}

void DropoutEditorDialog::onDeleteDropout() {
  if (selected_addition_index_ >= 0) {
    auto& additions = frame_view_->getAdditionsMutable();
    if (selected_addition_index_ < static_cast<int>(additions.size())) {
      additions.erase(additions.begin() + selected_addition_index_);
      selected_addition_index_ = -1;
      move_up_button_->setEnabled(false);
      move_down_button_->setEnabled(false);
      delete_dropout_button_->setEnabled(false);
      frame_view_->updateDisplay();
      updateFrameInfo();
      additions_list_->clearSelection();
    }
  } else if (selected_removal_index_ >= 0) {
    auto& removals = frame_view_->getRemovalsMutable();
    if (selected_removal_index_ < static_cast<int>(removals.size())) {
      removals.erase(removals.begin() + selected_removal_index_);
      selected_removal_index_ = -1;
      move_up_button_->setEnabled(false);
      move_down_button_->setEnabled(false);
      delete_dropout_button_->setEnabled(false);
      frame_view_->updateDisplay();
      updateFrameInfo();
      removals_list_->clearSelection();
    }
  }
}

void DropoutEditorDialog::keyPressEvent(QKeyEvent* event) {
  const int pan_step = 50;

  switch (event->key()) {
    case Qt::Key_Left:
      scroll_area_->horizontalScrollBar()->setValue(
          scroll_area_->horizontalScrollBar()->value() - pan_step);
      event->accept();
      break;
    case Qt::Key_Right:
      scroll_area_->horizontalScrollBar()->setValue(
          scroll_area_->horizontalScrollBar()->value() + pan_step);
      event->accept();
      break;
    case Qt::Key_Up:
      scroll_area_->verticalScrollBar()->setValue(
          scroll_area_->verticalScrollBar()->value() - pan_step);
      event->accept();
      break;
    case Qt::Key_Down:
      scroll_area_->verticalScrollBar()->setValue(
          scroll_area_->verticalScrollBar()->value() + pan_step);
      event->accept();
      break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
      onZoomIn();
      event->accept();
      break;
    case Qt::Key_Minus:
      onZoomOut();
      event->accept();
      break;
    case Qt::Key_0:
      onZoomReset();
      event->accept();
      break;
    default:
      QDialog::keyPressEvent(event);
      break;
  }
}

void DropoutEditorDialog::onApply() {
  saveCurrentFrame();
  Q_EMIT applied();
}
