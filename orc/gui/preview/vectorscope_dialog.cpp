/*
 * File:        vectorscope_dialog.cpp
 * Module:      orc-gui
 * Purpose:     Vectorscope visualization dialog implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "vectorscope_dialog.h"
#include "../field_frame_presentation.h"
#include "../logging.h"
#include "vectorscope_geometry.h"

// ============================================================================
// Private Implementation - Simple state, no core access
// ============================================================================
class VectorscopeDialogPrivate {
public:
    orc::NodeID node_id;
    QString scope_label{"Vectorscope"};
    uint64_t current_field_number = 0;
    orc::VideoSystem current_system{orc::VideoSystem::Unknown};
    std::optional<orc::VectorscopeData> last_data;
    
    void drawGraticule(QPainter& painter, VectorscopeDialog* dialog, 
                       orc::VideoSystem system, int32_t white_16b_ire, int32_t black_16b_ire);
};

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSizePolicy>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QCloseEvent>
#include <QResizeEvent>
#include <cmath>
#include <random>

namespace {

constexpr double kMajorMarkerLengthPixels = 18.0;
constexpr double kMinorMarkerLengthPixels = 10.0;
constexpr double kIqLabelOffsetPixels = 22.0;
constexpr double kColorLabelOffsetPixels = 48.0;
// Scope-angle convention in this renderer: +degrees rotate clockwise on screen.
// Keep these in standard vectorscope degrees (0=right, 90=up, counterclockwise).
constexpr double kNtscIAxisStandardDegrees = 147.0;
constexpr double kNtscNegIAxisStandardDegrees = -33.0;
constexpr double kNtscQAxisStandardDegrees = 57.0;
constexpr double kNtscNegQAxisStandardDegrees = -123.0;
constexpr double kIqLabelAngularOffsetDegrees = 4.0;
constexpr double kZoneHalfAngleDegrees = 13.0;
constexpr double kZoneHalfRadialSpanPercent = 0.14;
constexpr double kTargetBoxSizePixels = 42.0;
constexpr double kTargetCrosshairSizePixels = 22.0;
constexpr double kPalMVectorscopeSampleScale = 1.25;

bool isPointWithinCanvas(const QPointF& point, int canvas_size)
{
    return point.x() >= 0.0 && point.x() < static_cast<double>(canvas_size)
        && point.y() >= 0.0 && point.y() < static_cast<double>(canvas_size);
}

double vectorscopeSampleDisplayScale(orc::VideoSystem system)
{
    if (system == orc::VideoSystem::PAL_M) {
        return kPalMVectorscopeSampleScale;
    }

    return 1.0;
}

QString vectorscopeSystemTag(orc::VideoSystem system)
{
    if (system == orc::VideoSystem::NTSC) {
        return QStringLiteral("NTSC");
    }

    if (system == orc::VideoSystem::PAL || system == orc::VideoSystem::PAL_M) {
        return QStringLiteral("PAL/PAL-M");
    }

    return QString{};
}

QColor vectorscopeTargetColor(int rgb)
{
    switch (rgb) {
    case 1:
        return QColor(70, 150, 255, 120);
    case 2:
        return QColor(70, 230, 120, 120);
    case 3:
        return QColor(90, 235, 235, 120);
    case 4:
        return QColor(255, 90, 90, 120);
    case 5:
        return QColor(230, 90, 230, 120);
    case 6:
        return QColor(245, 215, 80, 120);
    default:
        return QColor(255, 255, 255, 120);
    }
}

void drawNtcsIqLabels(QPainter& painter, const orc::gui::VectorscopePlotGeometry& geometry)
{
    struct AxisLabel {
        const char* text;
        double angle_degrees;
        double label_angle_offset_degrees;
    };

    const AxisLabel axis_labels[] = {
        {"I", kNtscIAxisStandardDegrees, kIqLabelAngularOffsetDegrees},
        {"Q", kNtscQAxisStandardDegrees, -kIqLabelAngularOffsetDegrees},
        {"-I", kNtscNegIAxisStandardDegrees, -kIqLabelAngularOffsetDegrees},
        {"-Q", kNtscNegQAxisStandardDegrees, kIqLabelAngularOffsetDegrees}
    };

    QFont font = painter.font();
    font.setPointSize(24);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QPen(QColor(200, 200, 200), 1));

    const double label_radius_uv = orc::gui::kVectorscopeSignedFullScale
        - geometry.pixelsToMagnitude(kIqLabelOffsetPixels);

    for (const auto& axis_label : axis_labels) {
        const QPointF label_centre = geometry.pointFromStandardDegrees(
            axis_label.angle_degrees + axis_label.label_angle_offset_degrees,
            label_radius_uv
        );
        const QString text(axis_label.text);
        const QFontMetrics metrics(font);
        const QRect text_rect = metrics.boundingRect(text);

        painter.drawText(
            static_cast<int>(label_centre.x()) - (text_rect.width() / 2),
            static_cast<int>(label_centre.y()) + (text_rect.height() / 3),
            text
        );
    }
}

void drawReferenceAxis(QPainter& painter,
                       const orc::gui::VectorscopePlotGeometry& geometry,
                       double standard_angle_degrees)
{
    painter.drawLine(
        geometry.pointFromStandardDegrees(standard_angle_degrees, 0.2 * orc::gui::kVectorscopeSignedFullScale),
        geometry.pointFromStandardDegrees(standard_angle_degrees, orc::gui::kVectorscopeSignedFullScale)
    );
}

void drawCircleMarkers(QPainter& painter, const orc::gui::VectorscopePlotGeometry& geometry)
{
    const double outer_radius_uv = orc::gui::kVectorscopeSignedFullScale;
    const double major_marker_length_uv = geometry.pixelsToMagnitude(kMajorMarkerLengthPixels);
    const double minor_marker_length_uv = geometry.pixelsToMagnitude(kMinorMarkerLengthPixels);

    for (int degrees = 0; degrees < 360; degrees += 2) {
        const bool is_major_marker = (degrees % 10) == 0;
        const double marker_length_uv = is_major_marker ? major_marker_length_uv : minor_marker_length_uv;
        const double angle_radians = (static_cast<double>(degrees) * M_PI) / 180.0;

        painter.setPen(QPen(Qt::white,
                            is_major_marker ? orc::gui::kVectorscopeMajorMarkerStrokeWidth
                                            : orc::gui::kVectorscopeMinorMarkerStrokeWidth));
        painter.drawLine(
            geometry.pointFromVectorscopeAngle(angle_radians, outer_radius_uv - marker_length_uv),
            geometry.pointFromVectorscopeAngle(angle_radians, outer_radius_uv)
        );
    }
}

void drawColorZone(QPainter& painter,
                   const orc::gui::VectorscopePlotGeometry& geometry,
                   double angle_radians,
                   double magnitude_uv,
                   const QColor& color)
{
    const double zone_half_angle_radians = (kZoneHalfAngleDegrees * M_PI) / 180.0;
    const double radial_span_uv = magnitude_uv * kZoneHalfRadialSpanPercent;
    const double inner_radius_uv = std::max(0.0, magnitude_uv - radial_span_uv);
    const double outer_radius_uv = std::min(orc::gui::kVectorscopeSignedFullScale, magnitude_uv + radial_span_uv);

    QPainterPath zone_path;
    zone_path.moveTo(geometry.pointFromVectorscopeAngle(angle_radians - zone_half_angle_radians, inner_radius_uv));

    for (int step = 0; step <= 12; ++step) {
        const double t = static_cast<double>(step) / 12.0;
        const double arc_angle = angle_radians - zone_half_angle_radians + (t * zone_half_angle_radians * 2.0);
        zone_path.lineTo(geometry.pointFromVectorscopeAngle(arc_angle, outer_radius_uv));
    }

    for (int step = 12; step >= 0; --step) {
        const double t = static_cast<double>(step) / 12.0;
        const double arc_angle = angle_radians - zone_half_angle_radians + (t * zone_half_angle_radians * 2.0);
        zone_path.lineTo(geometry.pointFromVectorscopeAngle(arc_angle, inner_radius_uv));
    }

    zone_path.closeSubpath();

    QColor fill_color = color;
    fill_color.setAlpha(52);
    QColor outline_color = color;
    outline_color.setAlpha(180);

    painter.save();
    painter.setPen(QPen(outline_color, 1));
    painter.setBrush(fill_color);
    painter.drawPath(zone_path);
    painter.restore();
}

void drawTargetBox(QPainter& painter,
                   const orc::gui::VectorscopePlotGeometry& geometry,
                   const QPointF& centre,
                   const QColor& color)
{
    const double half_box = kTargetBoxSizePixels / 2.0;
    const double half_crosshair = kTargetCrosshairSizePixels / 2.0;
    const QRectF box_rect(centre.x() - half_box, centre.y() - half_box,
                          kTargetBoxSizePixels, kTargetBoxSizePixels);

    QColor outline_color = color;
    outline_color.setAlpha(220);

    painter.save();
    painter.setPen(QPen(outline_color, 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(box_rect);
    painter.drawLine(QPointF(centre.x() - half_crosshair, centre.y()),
                     QPointF(centre.x() + half_crosshair, centre.y()));
    painter.drawLine(QPointF(centre.x(), centre.y() - half_crosshair),
                     QPointF(centre.x(), centre.y() + half_crosshair));
    painter.restore();
}

} // namespace

// ============================================================================
// AspectRatioLabel Implementation
// ============================================================================

AspectRatioLabel::AspectRatioLabel(QWidget* parent)
    : QLabel(parent)
{
    setAlignment(Qt::AlignCenter);
    setStyleSheet("border: 1px solid #ccc; background-color: black;");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(256, 256);  // Allow shrinking to a reasonable minimum
}

void AspectRatioLabel::setPixmap(const QPixmap& pixmap) {
    original_pixmap_ = pixmap;
    updateScaledPixmap();
}

void AspectRatioLabel::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    updateScaledPixmap();
}

void AspectRatioLabel::updateScaledPixmap() {
    if (original_pixmap_.isNull()) {
        QLabel::setPixmap(QPixmap());
        return;
    }
    
    // Calculate size to fit while maintaining aspect ratio
    // For 1:1 aspect ratio, use the smaller dimension
    int size = std::min(width(), height());
    
    QPixmap scaled = original_pixmap_.scaled(
        size, size,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    );
    
    QLabel::setPixmap(scaled);
}

// ============================================================================
// VectorscopeDialog Implementation
// ============================================================================

VectorscopeDialog::VectorscopeDialog(QWidget *parent)
    : QDialog(parent)
    , d_(std::make_unique<VectorscopeDialogPrivate>())
{
    updateWindowTitle();
    setWindowFlags(Qt::Window);
    resize(1120, 900);
    
    setupUI();
    connectSignals();
}

VectorscopeDialog::~VectorscopeDialog() = default;

int VectorscopeDialog::getGraticuleMode() const {
    return graticule_group_->checkedId();
}

void VectorscopeDialog::setScopeLabel(const QString& scope_label)
{
    d_->scope_label = scope_label;
    updateWindowTitle();
}

void VectorscopeDialog::updateWindowTitle()
{
    const QString system_tag = vectorscopeSystemTag(d_->current_system);
    const QString title_scope_label = system_tag.isEmpty()
        ? d_->scope_label
        : QStringLiteral("%1 (%2)").arg(d_->scope_label, system_tag);

    if (d_->node_id.is_valid()) {
        setWindowTitle(QString("%1 - Node %2").arg(title_scope_label).arg(d_->node_id.value()));
        return;
    }
    setWindowTitle(title_scope_label);
}

void VectorscopeDialog::setStage(orc::NodeID node_id) {
    d_->node_id = node_id;
    updateWindowTitle();
}

void VectorscopeDialog::setupUI() {
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    
    // Info label
    info_label_ = new QLabel();
    info_label_->setStyleSheet("font-weight: bold;");
    main_layout->addWidget(info_label_);
    
    // Main content: display on left, controls on right
    QHBoxLayout* content_layout = new QHBoxLayout();
    
    // Left side: Vectorscope display with aspect ratio maintenance
    scope_label_ = new AspectRatioLabel();
    content_layout->addWidget(scope_label_, 1);
    
    // Right side: Controls
    QVBoxLayout* controls_layout = new QVBoxLayout();
    
    // Display options group
    QGroupBox* display_group = new QGroupBox("Display Options");
    QVBoxLayout* display_layout = new QVBoxLayout(display_group);
    
    blend_color_checkbox_ = new QCheckBox("Blend");
    defocus_checkbox_ = new QCheckBox("Defocus");
    draw_lines_checkbox_ = new QCheckBox("Draw Trace Lines");
    active_area_only_checkbox_ = new QCheckBox("Active Picture Area Only");
    draw_lines_checkbox_->setChecked(true);
    active_area_only_checkbox_->setChecked(true);
    
    // Point size spinbox
    QHBoxLayout* point_layout = new QHBoxLayout();
    QLabel* point_label = new QLabel("Point Size:");
    point_size_spinbox_ = new QSpinBox();
    point_size_spinbox_->setRange(1, 10);
    point_size_spinbox_->setValue(3);
    point_layout->addWidget(point_label);
    point_layout->addWidget(point_size_spinbox_);
    point_layout->addStretch();
    
    display_layout->addWidget(blend_color_checkbox_);
    display_layout->addWidget(defocus_checkbox_);
    display_layout->addWidget(draw_lines_checkbox_);
    display_layout->addWidget(active_area_only_checkbox_);
    display_layout->addLayout(point_layout);
    
    controls_layout->addWidget(display_group);
    
    // Field selection group
    QGroupBox* field_select_group = new QGroupBox("Field Selection");
    QVBoxLayout* field_select_layout = new QVBoxLayout(field_select_group);
    
    field_select_group_ = new QButtonGroup(this);
    
    field_select_all_radio_ = new QRadioButton("All Fields");
    field_select_first_radio_ = new QRadioButton("First Field Only");
    field_select_second_radio_ = new QRadioButton("Second Field Only");
    
    field_select_all_radio_->setChecked(true);
    
    field_select_group_->addButton(field_select_all_radio_, 0);
    field_select_group_->addButton(field_select_first_radio_, 1);
    field_select_group_->addButton(field_select_second_radio_, 2);
    
    field_select_layout->addWidget(field_select_all_radio_);
    field_select_layout->addWidget(field_select_first_radio_);
    field_select_layout->addWidget(field_select_second_radio_);
    
    controls_layout->addWidget(field_select_group);
    
    // Graticule group
    QGroupBox* graticule_group = new QGroupBox("Graticule");
    QVBoxLayout* graticule_layout = new QVBoxLayout(graticule_group);
    
    graticule_group_ = new QButtonGroup(this);
    
    graticule_none_radio_ = new QRadioButton("None");
    graticule_full_radio_ = new QRadioButton("Full");
    graticule_75_radio_ = new QRadioButton("75%");
    
    graticule_75_radio_->setChecked(true);
    
    graticule_group_->addButton(graticule_none_radio_, 0);
    graticule_group_->addButton(graticule_full_radio_, 1);
    graticule_group_->addButton(graticule_75_radio_, 2);
    
    graticule_layout->addWidget(graticule_none_radio_);
    graticule_layout->addWidget(graticule_full_radio_);
    graticule_layout->addWidget(graticule_75_radio_);
    
    controls_layout->addWidget(graticule_group);
    controls_layout->addStretch();
    
    // Set maximum width for controls panel to keep them from shrinking too much
    QWidget* controls_widget = new QWidget();
    controls_widget->setLayout(controls_layout);
    controls_widget->setMaximumWidth(200);
    controls_widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    content_layout->addWidget(controls_widget);
    
    main_layout->addLayout(content_layout, 1);
    
    // Initial display
    clearDisplay();
}

void VectorscopeDialog::connectSignals() {
    connect(blend_color_checkbox_, &QCheckBox::toggled, this, &VectorscopeDialog::onBlendColorToggled);
    connect(defocus_checkbox_, &QCheckBox::toggled, this, &VectorscopeDialog::onDefocusToggled);
    connect(draw_lines_checkbox_, &QCheckBox::toggled, this, &VectorscopeDialog::onDrawLinesToggled);
    connect(active_area_only_checkbox_, &QCheckBox::toggled, this, &VectorscopeDialog::onActiveAreaOnlyToggled);
    connect(point_size_spinbox_, QOverload<int>::of(&QSpinBox::valueChanged), this, &VectorscopeDialog::onPointSizeChanged);
    connect(field_select_group_, QOverload<int>::of(&QButtonGroup::idClicked),
            this, [this](int){ onFieldSelectionChanged(); });
    connect(graticule_group_, QOverload<int>::of(&QButtonGroup::idClicked),
            this, [this](int){ onGraticuleChanged(); });
}

bool VectorscopeDialog::isActiveAreaOnly() const {
    return active_area_only_checkbox_ && active_area_only_checkbox_->isChecked();
}

void VectorscopeDialog::updateVectorscope(const orc::VectorscopeData& data) {
    if (data.samples.empty()) {
        info_label_->setText(QString("Field %1 - No vectorscope data").arg(data.field_number + 1));  // Convert to 1-based
        clearDisplay();
        return;
    }

    d_->last_data = data;
    d_->current_field_number = data.field_number;
    d_->current_system = data.system;
    updateWindowTitle();
    renderVectorscope(data);
    ORC_LOG_DEBUG("Vectorscope updated for field {} ({} samples)", data.field_number, data.samples.size());
}

void VectorscopeDialog::renderVectorscope(const orc::VectorscopeData& data) {
    if (data.samples.empty()) {
        ORC_LOG_DEBUG("VectorscopeDialog: renderVectorscope called with empty samples for field {}", data.field_number);
        clearDisplay();
        return;
    }

    const orc::gui::VectorscopePlotGeometry geometry;
    const int size = geometry.canvas_size;
    
    // Check if this is mono/no-chroma data (all samples near origin)
    // Mono decoders produce no chroma, so all U/V values will be ~0
    constexpr double CHROMA_THRESHOLD = 1000.0;  // Small threshold for noise
    bool has_chroma = false;
    for (const auto& sample : data.samples) {
        if (std::abs(sample.u) > CHROMA_THRESHOLD || std::abs(sample.v) > CHROMA_THRESHOLD) {
            has_chroma = true;
            break;
        }
    }
    
    int graticule_mode = graticule_group_->checkedId();
    bool blend_mode = blend_color_checkbox_->isChecked();
    bool defocus = defocus_checkbox_->isChecked();
    int field_select = field_select_group_->checkedId();
    
    // Calculate IRE range for debugging
    double ire_range = data.white_16b_ire - data.black_16b_ire;
    double black_percent = (data.black_16b_ire / 65535.0) * 100.0;
    double white_percent = (data.white_16b_ire / 65535.0) * 100.0;
    
    ORC_LOG_DEBUG(
        "VectorscopeDialog: renderVectorscope field={} samples={} graticule={} blend={} defocus={} field_select={} system={} white={} black={} chroma_detected={}",
        data.field_number,
        data.samples.size(),
        graticule_mode,
        blend_mode,
        defocus,
        field_select,
        static_cast<int>(data.system),
        data.white_16b_ire,
        data.black_16b_ire,
        has_chroma
    );
    ORC_LOG_DEBUG(
        "VectorscopeDialog: IRE levels - black={:.2f}% ({}) white={:.2f}% ({}) range={:.0f} ({}=NTSC, {}=PAL)",
        black_percent,
        data.black_16b_ire,
        white_percent,
        data.white_16b_ire,
        ire_range,
        static_cast<int>(orc::VideoSystem::NTSC),
        static_cast<int>(orc::VideoSystem::PAL)
    );
    
    // Create image
    QImage image(size, size, QImage::Format_RGB888);
    image.fill(Qt::black);
    
    QPainter painter(&image);
    
    // Draw graticule first
    if (graticule_mode != 0) {
        d_->drawGraticule(painter, this, data.system, data.white_16b_ire, data.black_16b_ire);
    }
    
    // Set blend mode for transparency accumulation
    if (blend_mode) {
        painter.setCompositionMode(QPainter::CompositionMode_Plus);
    } else {
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
    
    // Cheap predictable PRNG for defocus
    std::minstd_rand random_engine(12345);
    std::normal_distribution<double> normal_dist(0.0, 100.0);
    
    // Plot U/V samples - connect consecutive points like a real vectorscope
    std::optional<QPoint> prev_point;
    QColor current_color = Qt::green;
    
    for (const auto& sample : data.samples) {
        // Filter samples based on field selection
        if (field_select == 1 && sample.field_id != 0) continue;  // First field only
        if (field_select == 2 && sample.field_id != 1) continue;  // Second field only
        // field_select == 0: show all fields
        
        // Determine color based on field selection, blend mode, and sample field_id
        QColor color = Qt::green;  // Default
        
        // If field_id is tracked and blend mode is on, use field colors
        if (blend_mode && field_select == 0) {  // All fields with blend
            color = (sample.field_id == 0) ? Qt::yellow : Qt::cyan;  // 0=first(yellow), 1=second(cyan)
        } else if (blend_mode && field_select == 2) {
            color = Qt::cyan;  // Second field only → cyan
        } else if (blend_mode && field_select == 1) {
            color = Qt::yellow;  // First field only → yellow
        }
        // else: no blend → all green
        
        painter.setPen(color);
        painter.setBrush(color);
        
        // Apply defocus if enabled.
        double u = sample.u;
        double v = sample.v;
        const double sample_display_scale = vectorscopeSampleDisplayScale(data.system);
        u *= sample_display_scale;
        v *= sample_display_scale;
        if (defocus) {
            u += normal_dist(random_engine);
            v += normal_dist(random_engine);
        }
        
        // Vectorscope: U is horizontal (positive right), V is vertical (positive up)
        const QPointF plot_point = geometry.mapUV(u, v);

        if (isPointWithinCanvas(plot_point, size)) {
            const int x = static_cast<int>(plot_point.x());
            const int y = static_cast<int>(plot_point.y());
            QPoint current_point(x, y);
            
            // Draw line from previous point if enabled (real vectorscope behavior)
            if (draw_lines_checkbox_->isChecked() && prev_point.has_value() && current_color == color) {
                painter.drawLine(*prev_point, current_point);
            }
            
            // Draw a filled circle at current location to simulate beam dwell
            // Transparent circles accumulate to create bright spots where trace dwells
            int point_radius = point_size_spinbox_->value();
            painter.drawEllipse(current_point, point_radius, point_radius);
            
            prev_point = current_point;
            current_color = color;
        } else {
            // Out of bounds - break the line
            prev_point.reset();
        }
    }
    
    // Draw warning text if no chroma detected
    if (!has_chroma) {
        painter.setPen(Qt::yellow);
        QFont font = painter.font();
        font.setPointSize(16);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(QRect(0, size / 2 - 40, size, 80), Qt::AlignCenter | Qt::TextWordWrap,
                        "NO CHROMA DATA\n(Using mono decoder?)");
    }
    
    painter.end();
    
    scope_label_->setPixmap(QPixmap::fromImage(image));
    
    // Update info with field selection
    QString field_info;
    if (field_select == 0) {
        field_info = "Both fields";
    } else if (field_select == 1) {
        field_info = "First field only";
    } else {
        field_info = "Second field only";
    }
    
    const QString sample_area = isActiveAreaOnly() ? "active picture" : "full frame";

    info_label_->setText(QString("Field %1 - %2 samples (%3x%4 %5) - %6")
        .arg(data.field_number + 1)  // Convert to 1-based
        .arg(data.samples.size())
        .arg(data.width)
        .arg(data.height)
        .arg(sample_area)
        .arg(field_info));
}

void VectorscopeDialogPrivate::drawGraticule(QPainter& painter, VectorscopeDialog* dialog,
                                              orc::VideoSystem system, int32_t white_16b_ire, int32_t black_16b_ire) {
    const orc::gui::VectorscopePlotGeometry geometry;

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Qt::white, orc::gui::kVectorscopeAxisStrokeWidth));

    painter.drawLine(QPointF(geometry.centre_point.x(), geometry.plot_area.top()),
                     QPointF(geometry.centre_point.x(), geometry.plot_area.bottom()));
    painter.drawLine(QPointF(geometry.plot_area.left(), geometry.centre_point.y()),
                     QPointF(geometry.plot_area.right(), geometry.centre_point.y()));

    painter.setPen(QPen(Qt::white, orc::gui::kVectorscopeCircleStrokeWidth));
    painter.drawEllipse(geometry.plot_area);
    drawCircleMarkers(painter, geometry);

    painter.setPen(QPen(Qt::white, orc::gui::kVectorscopeAxisStrokeWidth));

    // NTSC keeps the full I/Q reference set and labels.
    if (system == orc::VideoSystem::NTSC) {
        drawReferenceAxis(painter, geometry, kNtscIAxisStandardDegrees);
        drawReferenceAxis(painter, geometry, kNtscNegIAxisStandardDegrees);
        drawReferenceAxis(painter, geometry, kNtscQAxisStandardDegrees);
        drawReferenceAxis(painter, geometry, kNtscNegQAxisStandardDegrees);
        drawNtcsIqLabels(painter, geometry);
    }

    // 75% vs 100% targets scaling
    const int graticule_mode = dialog->getGraticuleMode();
    const bool draw_graticule = (graticule_mode != 0);
    if (draw_graticule) {
        const double percent = (graticule_mode == 2) ? 0.75 : 1.0;
        const int32_t white = white_16b_ire;
        const int32_t black = black_16b_ire;
        const double ireRange = static_cast<double>(white - black);

        if (white > black) {
            // Color labels for the six colour bars
            // rgb values: 1=B, 2=G, 3=Cy, 4=R, 5=Mg, 6=Yl
            const char* color_labels[] = {"", "B", "G", "Cy", "R", "Mg", "Yl"};
            
            // Draw targets for six colour bars (R'G'B' 001..110)
            for (int rgb = 1; rgb < 7; rgb++) {
                const orc::UVSample target = orc::gui::vectorscopeDisplayTargetUv(rgb, percent, ireRange, system);
                const double barTheta = std::atan2(-target.v, target.u);
                const double barMagnitude = std::hypot(target.u, target.v);
                const QPointF target_point = geometry.mapUV(target.u, target.v);
                const QColor target_color = vectorscopeTargetColor(rgb);

                drawColorZone(painter, geometry, barTheta, barMagnitude, target_color);
                drawTargetBox(painter, geometry, target_point, target_color);
                
                // Draw color label positioned just outside the target
                const double label_distance = barMagnitude + geometry.pixelsToMagnitude(kColorLabelOffsetPixels);
                const QPointF label_position = geometry.pointFromVectorscopeAngle(barTheta, label_distance);
                
                QFont font = painter.font();
                font.setPointSize(14);
                font.setBold(true);
                painter.setFont(font);
                QColor label_color = target_color;
                label_color.setAlpha(255);
                painter.setPen(QPen(label_color, orc::gui::kVectorscopeAxisStrokeWidth));
                
                // Center the text at the label position
                QFontMetrics fm(font);
                QString label_text(color_labels[rgb]);
                int text_width = fm.horizontalAdvance(label_text);
                int text_height = fm.height();
                
                painter.drawText(static_cast<int>(label_position.x()) - text_width / 2,
                                 static_cast<int>(label_position.y()) + text_height / 4,
                                 label_text);
            }
        }
    }
    
}

void VectorscopeDialog::clearDisplay() {
    QImage blank(orc::gui::kVectorscopeCanvasSize,
                 orc::gui::kVectorscopeCanvasSize,
                 QImage::Format_RGB888);
    blank.fill(Qt::black);
    {
        QPainter painter(&blank);
        if (d_->last_data.has_value()) {
            d_->drawGraticule(painter, this, d_->last_data->system, 
                             d_->last_data->white_16b_ire, d_->last_data->black_16b_ire);
        } else {
            const orc::gui::VectorscopePlotGeometry geometry;
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(Qt::white, orc::gui::kVectorscopeAxisStrokeWidth));
            painter.drawLine(QPointF(geometry.centre_point.x(), geometry.plot_area.top()),
                             QPointF(geometry.centre_point.x(), geometry.plot_area.bottom()));
            painter.drawLine(QPointF(geometry.plot_area.left(), geometry.centre_point.y()),
                             QPointF(geometry.plot_area.right(), geometry.centre_point.y()));
            painter.setPen(QPen(Qt::white, orc::gui::kVectorscopeCircleStrokeWidth));
            painter.drawEllipse(geometry.plot_area);
            drawCircleMarkers(painter, geometry);
        }
        painter.end();
    }

    scope_label_->setPixmap(QPixmap::fromImage(blank));
    info_label_->setText("No data");
}

void VectorscopeDialog::closeEvent(QCloseEvent* event) {
    emit closed();
    QDialog::closeEvent(event);
}

void VectorscopeDialog::onBlendColorToggled() {
    ORC_LOG_DEBUG("VectorscopeDialog: Blend Color toggled -> {}", blend_color_checkbox_->isChecked());
    // Re-render with new blend mode
    if (d_->last_data.has_value()) {
        renderVectorscope(*d_->last_data);
    }
}

void VectorscopeDialog::onDefocusToggled() {
    ORC_LOG_DEBUG("VectorscopeDialog: Defocus toggled -> {}", defocus_checkbox_->isChecked());
    // Re-render with new defocus settings
    if (d_->last_data.has_value()) {
        renderVectorscope(*d_->last_data);
    }
}

void VectorscopeDialog::onFieldSelectionChanged() {
    ORC_LOG_DEBUG("VectorscopeDialog: Field selection changed -> {}", field_select_group_->checkedId());
    // Re-render with new field selection
    if (d_->last_data.has_value()) {
        renderVectorscope(*d_->last_data);
    }
}

void VectorscopeDialog::onGraticuleChanged() {
    ORC_LOG_DEBUG("VectorscopeDialog: Graticule mode changed -> {}", graticule_group_->checkedId());
    // Re-render with new graticule
    if (d_->last_data.has_value()) {
        renderVectorscope(*d_->last_data);
    }
}

void VectorscopeDialog::onDrawLinesToggled() {
    ORC_LOG_DEBUG("VectorscopeDialog: Draw Lines toggled -> {}", draw_lines_checkbox_->isChecked());
    // Re-render with or without trace lines
    if (d_->last_data.has_value()) {
        renderVectorscope(*d_->last_data);
    }
}

void VectorscopeDialog::onPointSizeChanged() {
    int size = point_size_spinbox_->value();
    ORC_LOG_DEBUG("VectorscopeDialog: Point size changed -> {}", size);
    // Re-render with new point size
    if (d_->last_data.has_value()) {
        renderVectorscope(*d_->last_data);
    }
}

void VectorscopeDialog::onActiveAreaOnlyToggled() {
    ORC_LOG_DEBUG("VectorscopeDialog: Active area only toggled -> {}", isActiveAreaOnly());
    emit dataRefreshRequested();
}
