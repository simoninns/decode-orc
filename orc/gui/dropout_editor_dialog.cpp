/*
 * File:        dropout_editor_dialog.cpp
 * Module:      orc-gui
 * Purpose:     Dropout map editor dialog implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dropout_editor_dialog.h"
#include "../core/include/logging.h"
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <algorithm>

// ============================================================================
// DropoutFieldView Implementation
// ============================================================================

DropoutFieldView::DropoutFieldView(QWidget *parent)
    : QLabel(parent)
    , field_width_(0)
    , field_height_(0)
    , mode_(InteractionMode::None)
    , dragging_(false)
    , rubber_band_(new QRubberBand(QRubberBand::Rectangle, this))
    , hover_region_index_(-1)
    , hover_region_type_(HoverRegionType::None)
    , zoom_level_(1.0f)
{
    setAlignment(Qt::AlignCenter);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    setScaledContents(false);
    setFrameStyle(QFrame::Box | QFrame::Sunken);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    rubber_band_->hide();
    
    // Style the rubber band to be more visible
    QPalette palette;
    palette.setBrush(QPalette::Highlight, QBrush(QColor(0, 120, 215, 100)));
    rubber_band_->setPalette(palette);
}

void DropoutFieldView::setField(const std::vector<uint8_t>& field_data, int width, int height,
                                 const std::vector<orc::DropoutRegion>& source_dropouts,
                                 const std::vector<orc::DropoutRegion>& additions,
                                 const std::vector<orc::DropoutRegion>& removals)
{
    field_data_ = field_data;
    field_width_ = width;
    field_height_ = height;
    source_dropouts_ = source_dropouts;
    additions_ = additions;
    removals_ = removals;
    updateDisplay();
}

void DropoutFieldView::clearEdits()
{
    additions_.clear();
    removals_.clear();
    updateDisplay();
    Q_EMIT regionsModified();
}

QSize DropoutFieldView::sizeHint() const
{
    // Return the actual field size if available, scaled by zoom
    if (field_width_ > 0 && field_height_ > 0) {
        return QSize(static_cast<int>(field_width_ * zoom_level_), 
                     static_cast<int>(field_height_ * zoom_level_));
    }
    // Default size if no field loaded
    return QSize(800, 600);
}

void DropoutFieldView::resizeEvent(QResizeEvent *event)
{
    QLabel::resizeEvent(event);
    // Only redraw if we already have field data loaded
    if (!field_data_.empty() && field_width_ > 0 && field_height_ > 0) {
        updateDisplay();
    }
}

void DropoutFieldView::wheelEvent(QWheelEvent *event)
{
    // Zoom in/out with scroll wheel, centered on cursor
    if (event->modifiers() == Qt::NoModifier) {
        float delta = event->angleDelta().y() / 120.0f;  // Standard wheel delta is 120 per step
        float zoom_factor = 1.0f + (delta * 0.1f);  // 10% per wheel step
        float old_zoom = zoom_level_;
        float new_zoom = old_zoom * zoom_factor;
        new_zoom = std::max(0.5f, std::min(4.0f, new_zoom));  // Clamp between 50% and 400%
        
        if (new_zoom != zoom_level_) {
            // Get scroll area and current scroll position
            QScrollArea* scroll_area = qobject_cast<QScrollArea*>(parentWidget()->parentWidget());
            if (scroll_area) {
                // Get mouse position relative to the scroll area viewport
                QPoint viewport_pos = scroll_area->viewport()->mapFromGlobal(event->globalPosition().toPoint());
                
                // Get current scroll position
                int old_h_scroll = scroll_area->horizontalScrollBar()->value();
                int old_v_scroll = scroll_area->verticalScrollBar()->value();
                
                // Calculate mouse position in content coordinates (before zoom)
                float content_x = old_h_scroll + viewport_pos.x();
                float content_y = old_v_scroll + viewport_pos.y();
                
                // Apply new zoom
                zoom_level_ = new_zoom;
                updateDisplay();
                
                // Calculate new scroll position to keep same point under cursor
                float zoom_ratio = new_zoom / old_zoom;
                int new_h_scroll = static_cast<int>(content_x * zoom_ratio - viewport_pos.x());
                int new_v_scroll = static_cast<int>(content_y * zoom_ratio - viewport_pos.y());
                
                // Set new scroll position
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

void DropoutFieldView::setZoomLevel(float zoom)
{
    zoom_level_ = std::max(0.5f, std::min(4.0f, zoom));  // Clamp between 50% and 400%
    updateDisplay();
}

void DropoutFieldView::setHighlightedRegion(HoverRegionType type, int index)
{
    hover_region_type_ = type;
    hover_region_index_ = index;
    updateDisplay();
}

void DropoutFieldView::updateDisplay()
{
    if (field_data_.empty() || field_width_ == 0 || field_height_ == 0) {
        setText("No field data");
        return;
    }

    // Create QImage from field data
    QImage image(field_width_, field_height_, QImage::Format_RGB32);
    
    for (int y = 0; y < field_height_; ++y) {
        for (int x = 0; x < field_width_; ++x) {
            int idx = y * field_width_ + x;
            if (idx < static_cast<int>(field_data_.size())) {
                uint8_t val = field_data_[idx];
                image.setPixel(x, y, qRgb(val, val, val));
            }
        }
    }

    // Overlay dropout regions
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    
    // Calculate line thickness - make it scale with image height
    // Use 1% of field height, with a minimum of 3 and maximum of 6 pixels
    int line_thickness = std::max(3, std::min(6, field_height_ / 100));
    int hover_thickness = line_thickness + 2;
    
    // Draw source dropouts in red (semi-transparent) - existing hint dropouts from TBC
    for (size_t i = 0; i < source_dropouts_.size(); ++i) {
        const auto& region = source_dropouts_[i];
        bool is_hovered = (hover_region_type_ == HoverRegionType::Source && 
                          hover_region_index_ == static_cast<int>(i));
        QColor color = is_hovered ? QColor(255, 0, 0, 192) : QColor(255, 0, 0, 128);
        int thickness = is_hovered ? hover_thickness : line_thickness;
        
        int line = static_cast<int>(region.line);
        int start = static_cast<int>(region.start_sample);
        int end = static_cast<int>(region.end_sample);
        if (line >= 0 && line < field_height_ && start >= 0 && end <= field_width_ && start < end) {
            // Center the marker vertically around the scanline
            painter.fillRect(start, line - thickness / 2, end - start, thickness, color);
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
        if (line >= 0 && line < field_height_ && start >= 0 && end <= field_width_ && start < end) {
            // Center the marker vertically around the scanline
            painter.fillRect(start, line - thickness / 2, end - start, thickness, color);
        }
    }

    // Draw removals in yellow (semi-transparent)
    for (size_t i = 0; i < removals_.size(); ++i) {
        const auto& region = removals_[i];
        bool is_hovered = (hover_region_type_ == HoverRegionType::Removal && 
                          hover_region_index_ == static_cast<int>(i));
        QColor color = is_hovered ? QColor(255, 255, 0, 192) : QColor(255, 255, 0, 128);
        int thickness = is_hovered ? hover_thickness : line_thickness;
        
        int line = static_cast<int>(region.line);
        int start = static_cast<int>(region.start_sample);
        int end = static_cast<int>(region.end_sample);
        if (line >= 0 && line < field_height_ && start >= 0 && end <= field_width_ && start < end) {
            // Center the marker vertically around the scanline
            painter.fillRect(start, line - thickness / 2, end - start, thickness, color);
        }
    }

    // Scale image to fit widget while maintaining aspect ratio
    QPixmap pixmap = QPixmap::fromImage(image);
    
    // Apply zoom level by resizing the widget itself
    int zoomed_width = static_cast<int>(field_width_ * zoom_level_);
    int zoomed_height = static_cast<int>(field_height_ * zoom_level_);
    
    // Resize the label to match the zoomed size
    resize(zoomed_width, zoomed_height);
    setFixedSize(zoomed_width, zoomed_height);
    
    // Scale the pixmap to the zoomed size
    setPixmap(pixmap.scaled(zoomed_width, zoomed_height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
}

void DropoutFieldView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || field_width_ == 0 || field_height_ == 0) {
        return;
    }

    // Convert widget coordinates to field coordinates
    QPixmap pm = pixmap();
    if (pm.isNull()) {
        return;
    }

    // Calculate the actual position of the pixmap within the label
    QSize pm_size = pm.size();
    int pm_x = (width() - pm_size.width()) / 2;
    int pm_y = (height() - pm_size.height()) / 2;

    int click_x = event->pos().x() - pm_x;
    int click_y = event->pos().y() - pm_y;

    if (click_x < 0 || click_x >= pm_size.width() || click_y < 0 || click_y >= pm_size.height()) {
        return;
    }

    // Scale to field coordinates
    float scale_x = static_cast<float>(field_width_) / pm_size.width();
    float scale_y = static_cast<float>(field_height_) / pm_size.height();
    int field_x = static_cast<int>(click_x * scale_x);
    int field_y = static_cast<int>(click_y * scale_y);

    // If clicking on a hovered region, emit selection signal (works in any mode)
    if (hover_region_index_ >= 0) {
        if (hover_region_type_ == HoverRegionType::Addition) {
            Q_EMIT additionClicked(hover_region_index_);
            return;  // Don't start drag or other interactions
        } else if (hover_region_type_ == HoverRegionType::Removal) {
            Q_EMIT removalClicked(hover_region_index_);
            return;  // Don't start drag or other interactions
        }
        // Source dropouts can't be selected, fall through to normal behavior
    }

    // Check if clicking on existing region for removal mode
    if (mode_ == InteractionMode::RemovingDropout) {
        removeRegionAtPoint(field_x, field_y);
        return;
    }

    // Start dragging for adding dropout
    if (mode_ == InteractionMode::AddingDropout) {
        dragging_ = true;
        drag_start_ = QPoint(field_x, field_y);
        drag_current_ = drag_start_;
        
        // Position rubber band in widget coordinates
        rubber_band_->setGeometry(QRect(event->pos(), QSize()));
        rubber_band_->show();
    }
}

void DropoutFieldView::mouseMoveEvent(QMouseEvent *event)
{
    QPixmap pm = pixmap();
    if (pm.isNull()) {
        return;
    }

    // Calculate the actual position of the pixmap within the label
    QSize pm_size = pm.size();
    int pm_x = (width() - pm_size.width()) / 2;
    int pm_y = (height() - pm_size.height()) / 2;

    int mouse_x = event->pos().x() - pm_x;
    int mouse_y = event->pos().y() - pm_y;

    // Clamp to pixmap bounds
    mouse_x = std::max(0, std::min(mouse_x, pm_size.width() - 1));
    mouse_y = std::max(0, std::min(mouse_y, pm_size.height() - 1));

    // Scale to field coordinates
    float scale_x = static_cast<float>(field_width_) / pm_size.width();
    float scale_y = static_cast<float>(field_height_) / pm_size.height();
    int field_x = static_cast<int>(mouse_x * scale_x);
    int field_y = static_cast<int>(mouse_y * scale_y);

    if (dragging_ && mode_ == InteractionMode::AddingDropout) {
        drag_current_ = QPoint(field_x, field_y);

        // Update rubber band in widget coordinates - show as horizontal line only
        // Calculate widget coordinates for drag start
        int widget_start_x = (drag_start_.x() * pm_size.width() / field_width_) + pm_x;
        int widget_start_y = (drag_start_.y() * pm_size.height() / field_height_) + pm_y;
        
        // Current widget X coordinate (use start Y to keep it horizontal)
        int widget_current_x = event->pos().x();
        
        // Create a horizontal line (3 pixels tall for visibility)
        int line_height = 3;
        QRect line_rect(
            std::min(widget_start_x, widget_current_x),
            widget_start_y - line_height / 2,
            std::abs(widget_current_x - widget_start_x),
            line_height
        );
        
        rubber_band_->setGeometry(line_rect);
    } else {
        // Update hover highlighting
        int old_hover_index = hover_region_index_;
        HoverRegionType old_hover_type = hover_region_type_;
        
        hover_region_index_ = -1;
        hover_region_type_ = HoverRegionType::None;
        
        // Check if hovering over any region
        for (size_t i = 0; i < source_dropouts_.size(); ++i) {
            if (isPointInRegion(field_x, field_y, source_dropouts_[i])) {
                hover_region_index_ = static_cast<int>(i);
                hover_region_type_ = HoverRegionType::Source;
                break;
            }
        }
        
        if (hover_region_index_ == -1) {
            for (size_t i = 0; i < additions_.size(); ++i) {
                if (isPointInRegion(field_x, field_y, additions_[i])) {
                    hover_region_index_ = static_cast<int>(i);
                    hover_region_type_ = HoverRegionType::Addition;
                    break;
                }
            }
        }
        
        if (hover_region_index_ == -1) {
            for (size_t i = 0; i < removals_.size(); ++i) {
                if (isPointInRegion(field_x, field_y, removals_[i])) {
                    hover_region_index_ = static_cast<int>(i);
                    hover_region_type_ = HoverRegionType::Removal;
                    break;
                }
            }
        }
        
        // Redraw if hover state changed
        if (hover_region_index_ != old_hover_index || hover_region_type_ != old_hover_type) {
            updateDisplay();
        }
    }
}

void DropoutFieldView::mouseReleaseEvent(QMouseEvent *event)
{
    if (!dragging_ || event->button() != Qt::LeftButton || mode_ != InteractionMode::AddingDropout) {
        return;
    }

    dragging_ = false;
    rubber_band_->hide();

    // Create dropout region from drag
    // Line is always at drag_start_.y() since dropouts are single horizontal lines
    int line = drag_start_.y();
    int start_sample = std::min(drag_start_.x(), drag_current_.x());
    int end_sample = std::max(drag_start_.x(), drag_current_.x());

    // Only create region if it has some size
    if (end_sample > start_sample) {
        orc::DropoutRegion region;
        region.line = static_cast<uint32_t>(line);
        region.start_sample = static_cast<uint32_t>(start_sample);
        region.end_sample = static_cast<uint32_t>(end_sample);
        region.basis = orc::DropoutRegion::DetectionBasis::HINT_DERIVED;

        additions_.push_back(region);
        int new_index = static_cast<int>(additions_.size()) - 1;
        updateDisplay();
        Q_EMIT regionsModified();
        Q_EMIT additionCreated(new_index);
    }
}

bool DropoutFieldView::isPointInRegion(int x, int y, const orc::DropoutRegion& region) const
{
    return static_cast<uint32_t>(y) == region.line &&
           static_cast<uint32_t>(x) >= region.start_sample &&
           static_cast<uint32_t>(x) < region.end_sample;
}

void DropoutFieldView::removeRegionAtPoint(int x, int y)
{
    // Check additions first - clicking on a green addition removes it
    for (auto it = additions_.begin(); it != additions_.end(); ++it) {
        if (isPointInRegion(x, y, *it)) {
            additions_.erase(it);
            // Clear hover state
            hover_region_index_ = -1;
            hover_region_type_ = HoverRegionType::None;
            updateDisplay();
            Q_EMIT regionsModified();
            return;
        }
    }

    // Check removals - clicking on a yellow removal un-removes it
    for (auto it = removals_.begin(); it != removals_.end(); ++it) {
        if (isPointInRegion(x, y, *it)) {
            removals_.erase(it);
            // Clear hover state
            hover_region_index_ = -1;
            hover_region_type_ = HoverRegionType::None;
            updateDisplay();
            Q_EMIT regionsModified();
            return;
        }
    }
    
    // Check source dropouts - clicking on a red hint dropout marks it for removal
    for (const auto& region : source_dropouts_) {
        if (isPointInRegion(x, y, region)) {
            // Check if already in removals list
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
    std::shared_ptr<const orc::VideoFieldRepresentation> source_repr,
    const std::map<uint64_t, orc::FieldDropoutMap>& existing_map,
    QWidget *parent)
    : QDialog(parent)
    , source_repr_(source_repr)
    , current_field_id_(0)
    , total_fields_(0)
    , dropout_map_(existing_map)
    , edit_mode_(EditMode::Add)
    , selected_addition_index_(-1)
    , selected_removal_index_(-1)
{
    if (source_repr_) {
        total_fields_ = source_repr_->field_count();
    }

    setupUI();
    
    if (total_fields_ > 0) {
        loadField(0);
    }
}

void DropoutEditorDialog::setupUI()
{
    setWindowTitle("Dropout Map Editor");
    setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
    resize(1000, 700);

    auto* main_layout = new QVBoxLayout(this);

    // Field navigation controls
    auto* nav_group = new QGroupBox("Field Navigation");
    auto* nav_layout = new QHBoxLayout(nav_group);

    prev_button_ = new QPushButton("Previous");
    connect(prev_button_, &QPushButton::clicked, this, &DropoutEditorDialog::onPreviousField);
    nav_layout->addWidget(prev_button_);

    field_spin_box_ = new QSpinBox();
    field_spin_box_->setMinimum(0);
    field_spin_box_->setMaximum(static_cast<int>(total_fields_ - 1));
    field_spin_box_->setValue(0);
    connect(field_spin_box_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &DropoutEditorDialog::onFieldNumberChanged);
    nav_layout->addWidget(new QLabel("Field:"));
    nav_layout->addWidget(field_spin_box_);

    next_button_ = new QPushButton("Next");
    connect(next_button_, &QPushButton::clicked, this, &DropoutEditorDialog::onNextField);
    nav_layout->addWidget(next_button_);

    field_info_label_ = new QLabel();
    nav_layout->addWidget(field_info_label_);
    nav_layout->addStretch();

    // Add zoom controls to same row
    nav_layout->addWidget(new QLabel("Zoom:"));
    
    zoom_out_button_ = new QPushButton("-");
    zoom_out_button_->setMaximumWidth(40);
    connect(zoom_out_button_, &QPushButton::clicked, this, &DropoutEditorDialog::onZoomOut);
    nav_layout->addWidget(zoom_out_button_);
    
    zoom_reset_button_ = new QPushButton("100%");
    zoom_reset_button_->setMaximumWidth(60);
    connect(zoom_reset_button_, &QPushButton::clicked, this, &DropoutEditorDialog::onZoomReset);
    nav_layout->addWidget(zoom_reset_button_);
    
    zoom_in_button_ = new QPushButton("+");
    zoom_in_button_->setMaximumWidth(40);
    connect(zoom_in_button_, &QPushButton::clicked, this, &DropoutEditorDialog::onZoomIn);
    nav_layout->addWidget(zoom_in_button_);
    
    zoom_label_ = new QLabel("100%");
    nav_layout->addWidget(zoom_label_);

    main_layout->addWidget(nav_group);

    // Field view (top) - wrap in scroll area for zoom support
    scroll_area_ = new QScrollArea();
    scroll_area_->setWidgetResizable(false);
    scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_area_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_area_->setAlignment(Qt::AlignCenter);
    scroll_area_->setFrameShape(QFrame::StyledPanel);
    field_view_ = new DropoutFieldView();
    field_view_->setMinimumSize(400, 300);
    connect(field_view_, &DropoutFieldView::regionsModified,
            this, &DropoutEditorDialog::onRegionsModified);
    connect(field_view_, &DropoutFieldView::zoomChanged,
            this, &DropoutEditorDialog::onFieldViewZoomChanged);
    connect(field_view_, &DropoutFieldView::additionCreated,
            this, &DropoutEditorDialog::onAdditionCreated);
    connect(field_view_, &DropoutFieldView::removalCreated,
            this, &DropoutEditorDialog::onRemovalCreated);
    connect(field_view_, &DropoutFieldView::additionClicked,
            this, &DropoutEditorDialog::onAdditionClicked);
    connect(field_view_, &DropoutFieldView::removalClicked,
            this, &DropoutEditorDialog::onRemovalClicked);
    
    scroll_area_->setWidget(field_view_);
    main_layout->addWidget(scroll_area_, 3);

    // Control panel (bottom) - use horizontal layout for controls
    auto* control_layout = new QHBoxLayout();

    // Controls group
    auto* controls_group = new QGroupBox("Controls");
    auto* controls_vlayout = new QVBoxLayout(controls_group);

    add_dropout_button_ = new QPushButton("Add Dropout");
    add_dropout_button_->setCheckable(true);
    add_dropout_button_->setChecked(true);
    connect(add_dropout_button_, &QPushButton::clicked, this, &DropoutEditorDialog::onAddDropout);
    controls_vlayout->addWidget(add_dropout_button_);

    remove_dropout_button_ = new QPushButton("Remove Dropout");
    remove_dropout_button_->setCheckable(true);
    connect(remove_dropout_button_, &QPushButton::clicked, this, &DropoutEditorDialog::onRemoveDropout);
    controls_vlayout->addWidget(remove_dropout_button_);

    clear_field_button_ = new QPushButton("Clear Current Field");
    connect(clear_field_button_, &QPushButton::clicked, this, &DropoutEditorDialog::onClearCurrentField);
    controls_vlayout->addWidget(clear_field_button_);

    control_layout->addWidget(controls_group);

    // Line adjustment controls
    auto* adjust_group = new QGroupBox("Adjust Selected Dropout");
    auto* adjust_layout = new QVBoxLayout(adjust_group);
    
    move_up_button_ = new QPushButton("Move Up ↑");
    move_up_button_->setEnabled(false);
    connect(move_up_button_, &QPushButton::clicked, this, &DropoutEditorDialog::onMoveDropoutUp);
    adjust_layout->addWidget(move_up_button_);
    
    move_down_button_ = new QPushButton("Move Down ↓");
    move_down_button_->setEnabled(false);
    connect(move_down_button_, &QPushButton::clicked, this, &DropoutEditorDialog::onMoveDropoutDown);
    adjust_layout->addWidget(move_down_button_);
    
    delete_dropout_button_ = new QPushButton("Delete");
    delete_dropout_button_->setEnabled(false);
    connect(delete_dropout_button_, &QPushButton::clicked, this, &DropoutEditorDialog::onDeleteDropout);
    adjust_layout->addWidget(delete_dropout_button_);
    
    adjust_layout->addWidget(new QLabel("Click a dropout in the\nlist to select it"));
    
    control_layout->addWidget(adjust_group);

    // Additions list
    auto* additions_group = new QGroupBox("Additions (Green)");
    auto* additions_layout = new QVBoxLayout(additions_group);
    additions_list_ = new QListWidget();
    connect(additions_list_, &QListWidget::itemClicked, 
            this, &DropoutEditorDialog::onAdditionsListItemClicked);
    connect(additions_list_, &QListWidget::itemSelectionChanged,
            this, &DropoutEditorDialog::onAdditionsListSelectionChanged);
    additions_layout->addWidget(additions_list_);
    control_layout->addWidget(additions_group);

    // Removals list
    auto* removals_group = new QGroupBox("Removals (Yellow)");
    auto* removals_layout = new QVBoxLayout(removals_group);
    removals_list_ = new QListWidget();
    connect(removals_list_, &QListWidget::itemClicked,
            this, &DropoutEditorDialog::onRemovalsListItemClicked);
    connect(removals_list_, &QListWidget::itemSelectionChanged,
            this, &DropoutEditorDialog::onRemovalsListSelectionChanged);
    removals_layout->addWidget(removals_list_);
    control_layout->addWidget(removals_group);

    main_layout->addLayout(control_layout);

    // Dialog buttons
    auto* button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(button_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    main_layout->addWidget(button_box);
    
    // Set initial mode (add_dropout_button_ is checked by default)
    field_view_->mode_ = DropoutFieldView::InteractionMode::AddingDropout;
}

void DropoutEditorDialog::loadField(uint64_t field_id)
{
    if (!source_repr_ || field_id >= total_fields_) {
        return;
    }

    // Save current field before loading new one
    if (current_field_id_ != field_id && current_field_id_ < total_fields_) {
        saveCurrentField();
    }

    current_field_id_ = field_id;

    // Get field data from source
    orc::FieldID fid(field_id);
    auto field_samples = source_repr_->get_field(fid);
    
    // Convert to 8-bit grayscale for display
    // Assuming samples are in range [0, 65535] for 16-bit
    std::vector<uint8_t> field_data;
    field_data.reserve(field_samples.size());
    for (auto sample : field_samples) {
        field_data.push_back(static_cast<uint8_t>(sample >> 8));
    }

    // Get field dimensions from descriptor
    auto descriptor = source_repr_->get_descriptor(fid);
    int width = descriptor ? static_cast<int>(descriptor->width) : 0;
    int height = descriptor ? static_cast<int>(descriptor->height) : 0;
    
    if (!descriptor || width == 0 || height == 0) {
        ORC_LOG_ERROR("Failed to get field descriptor for field {}", field_id);
        return;
    }

    // Get existing source dropouts from the VideoFieldRepresentation
    std::vector<orc::DropoutRegion> source_dropouts = source_repr_->get_dropout_hints(fid);
    
    // Load existing dropout map for this field
    std::vector<orc::DropoutRegion> additions;
    std::vector<orc::DropoutRegion> removals;

    auto it = dropout_map_.find(field_id);
    if (it != dropout_map_.end()) {
        additions = it->second.additions;
        removals = it->second.removals;
    }

    // Update field view
    field_view_->setField(field_data, width, height, source_dropouts, additions, removals);

    // Calculate zoom to fit if this is the first field load
    if (current_field_id_ == 0 && field_id == 0) {
        // Calculate zoom to fit field in scroll area
        int available_width = scroll_area_->width() - 20;  // Leave some margin
        int available_height = scroll_area_->height() - 20;
        
        float zoom_x = static_cast<float>(available_width) / width;
        float zoom_y = static_cast<float>(available_height) / height;
        float fit_zoom = std::min(zoom_x, zoom_y);
        
        // Clamp to valid zoom range and set
        fit_zoom = std::max(0.5f, std::min(1.0f, fit_zoom));  // Don't zoom in beyond 100% initially
        field_view_->setZoomLevel(fit_zoom);
        zoom_label_->setText(QString("%1%").arg(static_cast<int>(fit_zoom * 100)));
        zoom_reset_button_->setText(QString("%1%").arg(static_cast<int>(fit_zoom * 100)));
    }

    // Update UI
    updateFieldInfo();
}

void DropoutEditorDialog::saveCurrentField()
{
    if (!source_repr_ || current_field_id_ >= total_fields_) {
        return;
    }

    // Get current additions and removals from field view
    auto additions = field_view_->getAdditions();
    auto removals = field_view_->getRemovals();

    // Update dropout map
    if (additions.empty() && removals.empty()) {
        // Remove entry if no modifications
        dropout_map_.erase(current_field_id_);
    } else {
        orc::FieldDropoutMap& field_map = dropout_map_[current_field_id_];
        field_map.field_id = orc::FieldID(current_field_id_);
        field_map.additions = additions;
        field_map.removals = removals;
    }
}

void DropoutEditorDialog::updateFieldInfo()
{
    auto additions = field_view_->getAdditions();
    auto removals = field_view_->getRemovals();

    field_info_label_->setText(
        QString("Field %1 of %2 - Additions: %3, Removals: %4")
            .arg(current_field_id_)
            .arg(total_fields_)
            .arg(additions.size())
            .arg(removals.size()));

    // Update lists
    additions_list_->clear();
    for (const auto& region : additions) {
        additions_list_->addItem(
            QString("Line %1: [%2, %3)")
                .arg(region.line)
                .arg(region.start_sample)
                .arg(region.end_sample));
    }

    removals_list_->clear();
    for (const auto& region : removals) {
        removals_list_->addItem(
            QString("Line %1: [%2, %3)")
                .arg(region.line)
                .arg(region.start_sample)
                .arg(region.end_sample));
    }

    // Update navigation buttons
    prev_button_->setEnabled(current_field_id_ > 0);
    next_button_->setEnabled(current_field_id_ < total_fields_ - 1);
}

void DropoutEditorDialog::onPreviousField()
{
    if (current_field_id_ > 0) {
        field_spin_box_->setValue(static_cast<int>(current_field_id_ - 1));
    }
}

void DropoutEditorDialog::onNextField()
{
    if (current_field_id_ < total_fields_ - 1) {
        field_spin_box_->setValue(static_cast<int>(current_field_id_ + 1));
    }
}

void DropoutEditorDialog::onFieldNumberChanged(int value)
{
    loadField(static_cast<uint64_t>(value));
}

void DropoutEditorDialog::onClearCurrentField()
{
    field_view_->clearEdits();
}

void DropoutEditorDialog::onRegionsModified()
{
    updateFieldInfo();
}

void DropoutEditorDialog::onAddDropout()
{
    edit_mode_ = EditMode::Add;
    add_dropout_button_->setChecked(true);
    remove_dropout_button_->setChecked(false);
    field_view_->mode_ = DropoutFieldView::InteractionMode::AddingDropout;
}

void DropoutEditorDialog::onRemoveDropout()
{
    edit_mode_ = EditMode::Remove;
    add_dropout_button_->setChecked(false);
    remove_dropout_button_->setChecked(true);
    field_view_->mode_ = DropoutFieldView::InteractionMode::RemovingDropout;
}

std::map<uint64_t, orc::FieldDropoutMap> DropoutEditorDialog::getDropoutMap() const
{
    // Make a copy and ensure current field is saved
    auto map = dropout_map_;
    
    // Get current field state
    auto additions = field_view_->getAdditions();
    auto removals = field_view_->getRemovals();

    if (additions.empty() && removals.empty()) {
        map.erase(current_field_id_);
    } else {
        orc::FieldDropoutMap& field_map = map[current_field_id_];
        field_map.field_id = orc::FieldID(current_field_id_);
        field_map.additions = additions;
        field_map.removals = removals;
    }

    return map;
}

void DropoutEditorDialog::onZoomIn()
{
    float current_zoom = field_view_->getZoomLevel();
    float new_zoom = std::min(4.0f, current_zoom * 1.25f);
    field_view_->setZoomLevel(new_zoom);
    zoom_label_->setText(QString("%1%").arg(static_cast<int>(new_zoom * 100)));
    zoom_reset_button_->setText(QString("%1%").arg(static_cast<int>(new_zoom * 100)));
}

void DropoutEditorDialog::onZoomOut()
{
    float current_zoom = field_view_->getZoomLevel();
    float new_zoom = std::max(0.5f, current_zoom / 1.25f);
    field_view_->setZoomLevel(new_zoom);
    zoom_label_->setText(QString("%1%").arg(static_cast<int>(new_zoom * 100)));
    zoom_reset_button_->setText(QString("%1%").arg(static_cast<int>(new_zoom * 100)));
}

void DropoutEditorDialog::onZoomReset()
{
    field_view_->setZoomLevel(1.0f);
    zoom_label_->setText("100%");
    zoom_reset_button_->setText("100%");
}

void DropoutEditorDialog::onMoveDropoutUp()
{
    if (selected_addition_index_ >= 0) {
        auto& additions = field_view_->getAdditionsMutable();
        if (selected_addition_index_ < static_cast<int>(additions.size())) {
            auto& region = additions[selected_addition_index_];
            if (region.line > 0) {
                region.line--;
                field_view_->updateDisplay();
                updateFieldInfo();
                // Re-select the item
                additions_list_->setCurrentRow(selected_addition_index_);
            }
        }
    } else if (selected_removal_index_ >= 0) {
        auto& removals = field_view_->getRemovalsMutable();
        if (selected_removal_index_ < static_cast<int>(removals.size())) {
            auto& region = removals[selected_removal_index_];
            if (region.line > 0) {
                region.line--;
                field_view_->updateDisplay();
                updateFieldInfo();
                // Re-select the item
                removals_list_->setCurrentRow(selected_removal_index_);
            }
        }
    }
}

void DropoutEditorDialog::onMoveDropoutDown()
{
    if (selected_addition_index_ >= 0) {
        auto& additions = field_view_->getAdditionsMutable();
        if (selected_addition_index_ < static_cast<int>(additions.size())) {
            auto& region = additions[selected_addition_index_];
            if (region.line < static_cast<uint32_t>(field_view_->getFieldHeight()) - 1) {
                region.line++;
                field_view_->updateDisplay();
                updateFieldInfo();
                // Re-select the item
                additions_list_->setCurrentRow(selected_addition_index_);
            }
        }
    } else if (selected_removal_index_ >= 0) {
        auto& removals = field_view_->getRemovalsMutable();
        if (selected_removal_index_ < static_cast<int>(removals.size())) {
            auto& region = removals[selected_removal_index_];
            if (region.line < static_cast<uint32_t>(field_view_->getFieldHeight()) - 1) {
                region.line++;
                field_view_->updateDisplay();
                updateFieldInfo();
                // Re-select the item
                removals_list_->setCurrentRow(selected_removal_index_);
            }
        }
    }
}

void DropoutEditorDialog::onAdditionsListItemClicked(QListWidgetItem* item)
{
    selected_addition_index_ = additions_list_->row(item);
    selected_removal_index_ = -1;
    removals_list_->clearSelection();
    updateButtonStatesForSelection(true);
}

void DropoutEditorDialog::onRemovalsListItemClicked(QListWidgetItem* item)
{
    selected_removal_index_ = removals_list_->row(item);
    selected_addition_index_ = -1;
    additions_list_->clearSelection();
    updateButtonStatesForSelection(false);
}

void DropoutEditorDialog::updateButtonStatesForSelection(bool is_addition)
{
    if (is_addition) {
        // Additions can be moved up/down and deleted
        move_up_button_->setEnabled(true);
        move_down_button_->setEnabled(true);
        delete_dropout_button_->setEnabled(true);
    } else {
        // Removals can only be deleted (moving doesn't make sense)
        move_up_button_->setEnabled(false);
        move_down_button_->setEnabled(false);
        delete_dropout_button_->setEnabled(true);
    }
}

void DropoutEditorDialog::onFieldViewZoomChanged(float zoom_level)
{
    // Update zoom UI when changed via scroll wheel
    zoom_label_->setText(QString("%1%").arg(static_cast<int>(zoom_level * 100)));
    zoom_reset_button_->setText(QString("%1%").arg(static_cast<int>(zoom_level * 100)));
}

void DropoutEditorDialog::onAdditionCreated(int index)
{
    // Auto-select the newly created addition
    selected_addition_index_ = index;
    selected_removal_index_ = -1;
    
    // Update the lists first to make sure the item exists
    updateFieldInfo();
    
    // Select the item in the list
    if (index >= 0 && index < additions_list_->count()) {
        additions_list_->setCurrentRow(index);
        removals_list_->clearSelection();
        
        // Enable adjustment buttons
        updateButtonStatesForSelection(true);
    }
}

void DropoutEditorDialog::onRemovalCreated(int index)
{
    // Auto-select the newly created removal
    selected_removal_index_ = index;
    selected_addition_index_ = -1;
    
    // Update the lists first to make sure the item exists
    updateFieldInfo();
    
    // Select the item in the list
    if (index >= 0 && index < removals_list_->count()) {
        removals_list_->setCurrentRow(index);
        additions_list_->clearSelection();
        
        // Enable delete button only (removals can't be moved)
        updateButtonStatesForSelection(false);
    }
}

void DropoutEditorDialog::onAdditionClicked(int index)
{
    // Select the clicked addition in the list
    selected_addition_index_ = index;
    selected_removal_index_ = -1;
    
    if (index >= 0 && index < additions_list_->count()) {
        additions_list_->setCurrentRow(index);
        removals_list_->clearSelection();
        
        // Enable adjustment buttons
        updateButtonStatesForSelection(true);
    }
}

void DropoutEditorDialog::onRemovalClicked(int index)
{
    // Select the clicked removal in the list
    selected_removal_index_ = index;
    selected_addition_index_ = -1;
    
    if (index >= 0 && index < removals_list_->count()) {
        removals_list_->setCurrentRow(index);
        additions_list_->clearSelection();
        
        // Enable delete button only (removals can't be moved)
        updateButtonStatesForSelection(false);
    }
}

void DropoutEditorDialog::onAdditionsListSelectionChanged()
{
    // Get current selection
    int current_row = additions_list_->currentRow();
    
    if (current_row >= 0) {
        // Clear the other list's selection
        removals_list_->clearSelection();
        
        // Highlight the selected addition in the field view
        field_view_->setHighlightedRegion(DropoutFieldView::HoverRegionType::Addition, current_row);
        
        // Update selection tracking
        selected_addition_index_ = current_row;
        selected_removal_index_ = -1;
        
        // Enable adjustment buttons (additions can be moved up/down)
        updateButtonStatesForSelection(true);
    } else {
        // No selection - clear highlight if no removal is selected either
        if (removals_list_->currentRow() < 0) {
            field_view_->setHighlightedRegion(DropoutFieldView::HoverRegionType::None, -1);
        }
        selected_addition_index_ = -1;
    }
}

void DropoutEditorDialog::onRemovalsListSelectionChanged()
{
    // Get current selection
    int current_row = removals_list_->currentRow();
    
    if (current_row >= 0) {
        // Clear the other list's selection
        additions_list_->clearSelection();
        
        // Highlight the selected removal in the field view
        field_view_->setHighlightedRegion(DropoutFieldView::HoverRegionType::Removal, current_row);
        
        // Update selection tracking
        selected_removal_index_ = current_row;
        selected_addition_index_ = -1;
        
        // Enable delete button only (removals can't be moved, only deleted)
        updateButtonStatesForSelection(false);
    } else {
        // No selection - clear highlight if no addition is selected either
        if (additions_list_->currentRow() < 0) {
            field_view_->setHighlightedRegion(DropoutFieldView::HoverRegionType::None, -1);
        }
        selected_removal_index_ = -1;
    }
}

void DropoutEditorDialog::onDeleteDropout()
{
    if (selected_addition_index_ >= 0) {
        auto& additions = field_view_->getAdditionsMutable();
        if (selected_addition_index_ < static_cast<int>(additions.size())) {
            additions.erase(additions.begin() + selected_addition_index_);
            selected_addition_index_ = -1;
            move_up_button_->setEnabled(false);
            move_down_button_->setEnabled(false);
            delete_dropout_button_->setEnabled(false);
            field_view_->updateDisplay();
            updateFieldInfo();
            additions_list_->clearSelection();
        }
    } else if (selected_removal_index_ >= 0) {
        auto& removals = field_view_->getRemovalsMutable();
        if (selected_removal_index_ < static_cast<int>(removals.size())) {
            removals.erase(removals.begin() + selected_removal_index_);
            selected_removal_index_ = -1;
            move_up_button_->setEnabled(false);
            move_down_button_->setEnabled(false);
            delete_dropout_button_->setEnabled(false);
            field_view_->updateDisplay();
            updateFieldInfo();
            removals_list_->clearSelection();
        }
    }
}

void DropoutEditorDialog::keyPressEvent(QKeyEvent *event)
{
    // Arrow keys for panning
    const int pan_step = 50;  // pixels to pan per keypress
    
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
        case Qt::Key_Equal:  // For US keyboards where + is Shift+=
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
