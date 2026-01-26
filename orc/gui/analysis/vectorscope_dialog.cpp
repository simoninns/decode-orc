/*
 * File:        vectorscope_dialog.cpp
 * Module:      orc-gui
 * Purpose:     Vectorscope visualization dialog implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "vectorscope_dialog.h"
#include "../logging.h"

// ============================================================================
// Private Implementation - Simple state, no core access
// ============================================================================
class VectorscopeDialogPrivate {
public:
    orc::NodeID node_id;
    uint64_t current_field_number = 0;
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
#include <QCloseEvent>
#include <QResizeEvent>
#include <cmath>
#include <random>

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
    setWindowTitle("Vectorscope");
    setWindowFlags(Qt::Window);
    resize(800, 900);
    
    setupUI();
    connectSignals();
}

VectorscopeDialog::~VectorscopeDialog() = default;

int VectorscopeDialog::getGraticuleMode() const {
    return graticule_group_->checkedId();
}

void VectorscopeDialog::setStage(orc::NodeID node_id) {
    d_->node_id = node_id;
    setWindowTitle(QString("Vectorscope - Node %1").arg(node_id.value()));
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
    
    blend_color_checkbox_ = new QCheckBox("Blend Color (Accumulate)");
    defocus_checkbox_ = new QCheckBox("Defocus");
    
    display_layout->addWidget(blend_color_checkbox_);
    display_layout->addWidget(defocus_checkbox_);
    display_layout->addStretch();
    
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
    field_select_layout->addStretch();
    
    controls_layout->addWidget(field_select_group);
    
    // Graticule group
    QGroupBox* graticule_group = new QGroupBox("Graticule");
    QVBoxLayout* graticule_layout = new QVBoxLayout(graticule_group);
    
    graticule_group_ = new QButtonGroup(this);
    
    graticule_none_radio_ = new QRadioButton("None");
    graticule_full_radio_ = new QRadioButton("Full");
    graticule_75_radio_ = new QRadioButton("75%");
    
    graticule_full_radio_->setChecked(true);
    
    graticule_group_->addButton(graticule_none_radio_, 0);
    graticule_group_->addButton(graticule_full_radio_, 1);
    graticule_group_->addButton(graticule_75_radio_, 2);
    
    graticule_layout->addWidget(graticule_none_radio_);
    graticule_layout->addWidget(graticule_full_radio_);
    graticule_layout->addWidget(graticule_75_radio_);
    graticule_layout->addStretch();
    
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
    connect(field_select_group_, QOverload<int>::of(&QButtonGroup::idClicked),
            this, [this](int){ onFieldSelectionChanged(); });
    connect(graticule_group_, QOverload<int>::of(&QButtonGroup::idClicked),
            this, [this](int){ onGraticuleChanged(); });
}

void VectorscopeDialog::updateVectorscope(const orc::VectorscopeData& data) {
    if (data.samples.empty()) {
        info_label_->setText(QString("Field %1 - No vectorscope data").arg(data.field_number));
        clearDisplay();
        return;
    }

    d_->last_data = data;
    d_->current_field_number = data.field_number;
    renderVectorscope(data);
    ORC_LOG_DEBUG("Vectorscope updated for field {} ({} samples)", data.field_number, data.samples.size());
}

void VectorscopeDialog::renderVectorscope(const orc::VectorscopeData& data) {
    if (data.samples.empty()) {
        ORC_LOG_DEBUG("VectorscopeDialog: renderVectorscope called with empty samples for field {}", data.field_number);
        clearDisplay();
        return;
    }
    
    constexpr int SIZE = 1024;
    constexpr double SCALE = 65536.0 / SIZE;
    constexpr int HALF_SIZE = SIZE / 2;
    
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
    
    // Create image
    QImage image(SIZE, SIZE, QImage::Format_RGB888);
    image.fill(Qt::black);
    
    QPainter painter(&image);
    
    // Draw graticule first
    if (graticule_mode != 0) {
        d_->drawGraticule(painter, this, data.system, data.white_16b_ire, data.black_16b_ire);
    }
    
    // Set blend mode
    painter.setCompositionMode(blend_mode ? QPainter::CompositionMode_Plus : QPainter::CompositionMode_SourceOver);
    
    // Cheap predictable PRNG for defocus
    std::minstd_rand random_engine(12345);
    std::normal_distribution<double> normal_dist(0.0, 100.0);
    
    // Plot U/V samples
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
        
        // Apply defocus if enabled
        double u = sample.u;
        double v = sample.v;
        if (defocus) {
            u += normal_dist(random_engine);
            v += normal_dist(random_engine);
        }
        
        // Vectorscope: U is horizontal (positive right), V is vertical (positive up)
        int x = HALF_SIZE + static_cast<int>(u / SCALE);
        int y = HALF_SIZE - static_cast<int>(v / SCALE);
        
        if (x >= 0 && x < SIZE && y >= 0 && y < SIZE) {
            painter.drawPoint(x, y);
        }
    }
    
    // Draw warning text if no chroma detected
    if (!has_chroma) {
        painter.setPen(Qt::yellow);
        QFont font = painter.font();
        font.setPointSize(16);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(QRect(0, SIZE/2 - 40, SIZE, 80), Qt::AlignCenter | Qt::TextWordWrap,
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
    
    info_label_->setText(QString("Field %1 - %2 samples (%3x%4) - %5")
        .arg(data.field_number)
        .arg(data.samples.size())
        .arg(data.width)
        .arg(data.height)
        .arg(field_info));
}

void VectorscopeDialogPrivate::drawGraticule(QPainter& painter, VectorscopeDialog* dialog,
                                              orc::VideoSystem system, int32_t white_16b_ire, int32_t black_16b_ire) {
    constexpr int SIZE = 1024;
    constexpr int HALF_SIZE = SIZE / 2;

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Qt::white, 1));

    // Center cross and outer circle (match ld-analyse)
    painter.drawLine(HALF_SIZE, 0, HALF_SIZE, SIZE - 1);
    painter.drawLine(0, HALF_SIZE, SIZE - 1, HALF_SIZE);
    painter.drawEllipse(0, 0, SIZE - 1, SIZE - 1);

    // NTSC I/Q axes only if system is NTSC
    if (system == orc::VideoSystem::NTSC) {
        double theta = (-33.0 * M_PI) / 180.0;
        for (int i = 0; i < 4; i++) {
            painter.drawLine(
                HALF_SIZE + static_cast<int>(0.2 * HALF_SIZE * cos(theta)),
                HALF_SIZE + static_cast<int>(0.2 * HALF_SIZE * sin(theta)),
                HALF_SIZE + static_cast<int>(HALF_SIZE * cos(theta)),
                HALF_SIZE + static_cast<int>(HALF_SIZE * sin(theta))
            );
            theta += M_PI / 2.0;
        }
    }

    // 75% vs 100% targets scaling
    const int graticule_mode = dialog->getGraticuleMode();
    const bool draw_graticule = (graticule_mode != 0);
    if (draw_graticule) {
        const double percent = (graticule_mode == 2) ? 0.75 : 1.0;
        const int32_t white = white_16b_ire;
        const int32_t black = black_16b_ire;
        const double SCALE = 65536.0 / SIZE;

        if (white > black) {
            // Draw targets for six colour bars (R'G'B' 001..110)
            for (int rgb = 1; rgb < 7; rgb++) {
                double R = percent * static_cast<double>((rgb >> 2) & 1);
                double G = percent * static_cast<double>((rgb >> 1) & 1);
                double B = percent * static_cast<double>(rgb & 1);

                // Poynton p337 eq 28.5: Y'UV
                double U = (R * -0.147141) + (G * -0.288869) + (B * 0.436010);
                double V = (R * 0.614975)  + (G * -0.514965) + (B * -0.100010);

                double barTheta = atan2(-V, U);
                double barMag = std::sqrt((V * V) + (U * U)) * (static_cast<double>(white - black)) / SCALE;

                // Grid around target: 10° and 10% steps
                const double stepTheta = (10.0 * M_PI) / 180.0;
                const double stepMag = 0.1 * barMag;

                // Angle sweeps
                for (int step = -1; step < 2; step++) {
                    double theta = barTheta + (step * stepTheta);
                    painter.drawLine(
                        HALF_SIZE + static_cast<int>((barMag - stepMag) * cos(theta)),
                        HALF_SIZE + static_cast<int>((barMag - stepMag) * sin(theta)),
                        HALF_SIZE + static_cast<int>((barMag + stepMag) * cos(theta)),
                        HALF_SIZE + static_cast<int>((barMag + stepMag) * sin(theta))
                    );
                }
                // Magnitude sweeps
                for (int step = -1; step < 2; step++) {
                    double mag = barMag + (step * stepMag);
                    painter.drawLine(
                        HALF_SIZE + static_cast<int>(mag * cos(barTheta - stepTheta)),
                        HALF_SIZE + static_cast<int>(mag * sin(barTheta - stepTheta)),
                        HALF_SIZE + static_cast<int>(mag * cos(barTheta + stepTheta)),
                        HALF_SIZE + static_cast<int>(mag * sin(barTheta + stepTheta))
                    );
                }
            }
        }
    }
    
}

void VectorscopeDialog::clearDisplay() {
    QImage blank(1024, 1024, QImage::Format_RGB888);
    blank.fill(Qt::black);
    {
        QPainter painter(&blank);
        if (d_->last_data.has_value()) {
            d_->drawGraticule(painter, this, d_->last_data->system, 
                             d_->last_data->white_16b_ire, d_->last_data->black_16b_ire);
        } else {
            // Draw basic cross and outer circle when no data yet
            constexpr int SIZE = 1024;
            constexpr int HALF_SIZE = SIZE / 2;
            painter.setPen(QPen(Qt::white, 1));
            painter.drawLine(HALF_SIZE, 0, HALF_SIZE, SIZE - 1);
            painter.drawLine(0, HALF_SIZE, SIZE - 1, HALF_SIZE);
            painter.drawEllipse(0, 0, SIZE - 1, SIZE - 1);
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
