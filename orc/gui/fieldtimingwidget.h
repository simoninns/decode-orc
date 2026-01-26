/*
 * File:        fieldtimingwidget.h
 * Module:      orc-gui
 * Purpose:     Widget for rendering field timing graphs
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef FIELDTIMINGWIDGET_H
#define FIELDTIMINGWIDGET_H

#include <QWidget>
#include <QScrollBar>
#include <vector>
#include <cstdint>
#include <optional>
#include "presenters/include/hints_view_models.h"

/**
 * @brief Widget for displaying field sample data as a timing graph
 * 
 * Renders sample values over time with horizontal scrolling.
 * Y-axis: sample value (0-65535)
 * X-axis: sample position
 */
class FieldTimingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FieldTimingWidget(QWidget *parent = nullptr);
    
    /**
     * @brief Set the field data to display
     * 
     * @param samples Primary field samples (composite or Y+C for composite sources)
     * @param samples_2 Optional second field samples (for frame modes)
     * @param y_samples Optional Y channel samples for YC sources
     * @param c_samples Optional C channel samples for YC sources
     * @param y_samples_2 Optional Y channel samples for second field
     * @param c_samples_2 Optional C channel samples for second field
     * @param video_params Optional video parameters for mV conversion and level markers
     */
    void setFieldData(const std::vector<uint16_t>& samples,
                     const std::vector<uint16_t>& samples_2 = {},
                     const std::vector<uint16_t>& y_samples = {},
                     const std::vector<uint16_t>& c_samples = {},
                     const std::vector<uint16_t>& y_samples_2 = {},
                     const std::vector<uint16_t>& c_samples_2 = {},
                     const std::optional<orc::presenters::VideoParametersView>& video_params = std::nullopt,
                     const std::optional<int>& marker_sample = std::nullopt);
    
    /**
     * @brief Get the horizontal scroll bar
     */
    QScrollBar* scrollBar() const { return scroll_bar_; }
    
    /**
     * @brief Scroll the view to center on the marker position
     */
    void scrollToMarker();
    
    /**
     * @brief Scroll the view to center on a specific line number
     * @param line_number Line number (1-based)
     */
    void scrollToLine(int line_number);
    
    /**
     * @brief Get the sample position at the center of the current view
     * @return Sample index at center, or -1 if no data
     */
    int getCenterSample() const;
    
    /**
     * @brief Set the zoom factor
     * @param zoom_factor Multiplier for pixels per sample (1.0 = all samples fit, >1.0 = zoom in, <1.0 = zoom out)
     */
    void setZoomFactor(double zoom_factor);
    
    /**
     * @brief Get the base pixels per sample needed to fit all samples horizontally at 100% zoom
     * This is independent of window size
     */
    double getBasePixelsPerSample() const;
    
    /**
     * @brief Get the current video parameters
     */
    const std::optional<orc::presenters::VideoParametersView>& videoParams() const { return video_params_; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void updateScrollBar();
    void drawGraph(QPainter& painter, const QRect& graph_area);
    void drawSamples(QPainter& painter, const QRect& graph_area, 
                    const std::vector<uint16_t>& samples, 
                    const QColor& color, int y_offset = 0);
    double convertSampleToMV(uint16_t sample) const;
    double getMVRange(double& min_mv, double& max_mv) const;
    
    // Sample data
    std::vector<uint16_t> field1_samples_;
    std::vector<uint16_t> field2_samples_;
    std::vector<uint16_t> y1_samples_;
    std::vector<uint16_t> c1_samples_;
    std::vector<uint16_t> y2_samples_;
    std::vector<uint16_t> c2_samples_;
    
    // Scrolling
    QScrollBar* scroll_bar_;
    int scroll_offset_;
    
    // Video parameters for mV conversion
    std::optional<orc::presenters::VideoParametersView> video_params_;
    std::optional<int> marker_sample_;
    
    // Display settings
    static constexpr int MARGIN = 40;
    static constexpr int SAMPLES_PER_VIEW = 2000;  // Number of samples visible at once
    static constexpr double PIXELS_PER_SAMPLE = 0.5;  // Base zoom level
    double zoom_factor_;  // Current zoom multiplier (1.0 = default)
    
    // Mouse dragging state
    bool is_dragging_;
    QPoint drag_start_pos_;
    int drag_start_scroll_value_;
};

#endif // FIELDTIMINGWIDGET_H
