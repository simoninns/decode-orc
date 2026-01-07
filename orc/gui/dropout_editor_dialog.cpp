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
    // Return a fixed size to prevent auto-expansion
    return QSize(640, 480);
}

void DropoutFieldView::resizeEvent(QResizeEvent *event)
{
    QLabel::resizeEvent(event);
    // Only redraw if we already have field data loaded
    if (!field_data_.empty() && field_width_ > 0 && field_height_ > 0) {
        updateDisplay();
    }
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
    int target_width = width();
    int target_height = height();
    
    if (target_width > 0 && target_height > 0) {
        setPixmap(pixmap.scaled(target_width, target_height, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        setPixmap(pixmap);
    }
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
        updateDisplay();
        Q_EMIT regionsModified();
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
                updateDisplay();
                Q_EMIT regionsModified();
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

    main_layout->addWidget(nav_group);

    // Field view (top)
    field_view_ = new DropoutFieldView();
    connect(field_view_, &DropoutFieldView::regionsModified,
            this, &DropoutEditorDialog::onRegionsModified);
    main_layout->addWidget(field_view_, 3);

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

    // Additions list
    auto* additions_group = new QGroupBox("Additions (Green)");
    auto* additions_layout = new QVBoxLayout(additions_group);
    additions_list_ = new QListWidget();
    additions_layout->addWidget(additions_list_);
    control_layout->addWidget(additions_group);

    // Removals list
    auto* removals_group = new QGroupBox("Removals (Yellow)");
    auto* removals_layout = new QVBoxLayout(removals_group);
    removals_list_ = new QListWidget();
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
