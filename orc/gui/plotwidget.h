/******************************************************************************
 * plotwidget.h
 * orc-gui - Adapted from ld-analyse
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 *
 * This file is part of decode-orc.
 ******************************************************************************/

#ifndef PLOTWIDGET_H
#define PLOTWIDGET_H

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsTextItem>
#include <QGraphicsPathItem>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QRubberBand>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QColor>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QFontMetrics>
#include <QVector>
#include <QPointF>

class PlotGrid;
class PlotSeries;
class PlotMarker;
class PlotLegend;
class PlotAxisLabels;

class PlotWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PlotWidget(QWidget *parent = nullptr);
    ~PlotWidget();

    // Axis management
    void setAxisTitle(Qt::Orientation orientation, const QString &title);
    void setAxisRange(Qt::Orientation orientation, double min, double max);
    void setAxisAutoScale(Qt::Orientation orientation, bool enable);
    void setYAxisIntegerLabels(bool integerOnly);
    void setAxisTickStep(Qt::Orientation orientation, double step, double origin = 0.0);
    
    // Secondary Y-axis (right side)
    void setSecondaryYAxisEnabled(bool enabled);
    void setSecondaryYAxisTitle(const QString &title);
    void setSecondaryYAxisRange(double min, double max);
    void setSecondaryYAxisTickStep(double step, double origin = 0.0);
    
    // Grid
    void setGridEnabled(bool enabled);
    void setGridPen(const QPen &pen);
    
    // Series
    PlotSeries* addSeries(const QString &title = QString());
    void removeSeries(PlotSeries *series);
    void clearSeries();
    
    // Markers
    PlotMarker* addMarker();
    void removeMarker(PlotMarker *marker);
    void clearMarkers();
    
    // Clear and show message
    void showNoDataMessage(const QString &message = "No data available");
    void clearNoDataMessage();  ///< Clear the "no data" message if showing
    
    // Legend
    void setLegendEnabled(bool enabled);
    
    // Zooming and panning
    void setZoomEnabled(bool enabled);
    void setPanEnabled(bool enabled);
    void resetZoom();
    
    // Canvas
    void setCanvasBackground(const QColor &color);
    
    // Theme
    void updateTheme();
    static bool isDarkTheme();
    
    // Replot
    void replot();

signals:
    void plotAreaChanged(const QRectF &rect);
    void seriesClicked(PlotSeries *series, const QPointF &point);
    void plotClicked(const QPointF &dataPoint);  // Emitted when plot area is clicked, in data coordinates
    void plotDragged(const QPointF &dataPoint);  // Emitted continuously during drag, in data coordinates

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onSceneSelectionChanged();

private:
    QGraphicsView *m_view;
    QGraphicsScene *m_scene;
    QVBoxLayout *m_mainLayout;
    
    // Plot area
    QRectF m_plotRect;
    QRectF m_dataRect;
    
    // Axes
    QString m_xAxisTitle;
    QString m_yAxisTitle;
    QString m_secondaryYAxisTitle;
    double m_xMin, m_xMax;
    double m_yMin, m_yMax;
    double m_secondaryYMin, m_secondaryYMax;
    bool m_xAutoScale;
    bool m_yAutoScale;
    bool m_yIntegerLabels;
    bool m_isDarkTheme;
    bool m_secondaryYAxisEnabled;
    double m_xAxisTickStep;
    double m_xAxisTickOrigin;
    bool m_xAxisUseCustomTicks;
    double m_yAxisTickStep;
    double m_yAxisTickOrigin;
    double m_secondaryYAxisTickStep;
    double m_secondaryYAxisTickOrigin;
    bool m_yAxisUseCustomTicks;
    bool m_secondaryYAxisUseCustomTicks;
    
    // Components
    PlotGrid *m_grid;
    PlotLegend *m_legend;
    PlotAxisLabels *m_axisLabels;
    QList<PlotSeries*> m_series;
    QList<PlotMarker*> m_markers;
    QGraphicsTextItem *m_noDataTextItem;  // Track "no data" message
    
    // Settings
    bool m_gridEnabled;
    bool m_legendEnabled;
    bool m_zoomEnabled;
    bool m_panEnabled;
    QColor m_canvasBackground;
    bool m_usePaletteCanvasBackground;
    bool m_isDragging;  // Track if mouse is being dragged
    
public:
    // Coordinate mapping methods (needed by plot items)
    QPointF mapToData(const QPointF &scenePos) const;
    QPointF mapFromData(const QPointF &dataPos) const;

private:
    // Helper methods
    void setupView();
    void updatePlotArea();
    void updateAxisLabels();
    void calculateDataRange();
};

// Plot series class for drawing data series
class PlotSeries : public QGraphicsPathItem
{
public:
    enum PlotStyle {
        Lines,  // Connect points with lines (default)
        Bars    // Draw vertical bars from x-axis to each point
    };
    
    explicit PlotSeries(PlotWidget *parent = nullptr);
    
    void setTitle(const QString &title);
    QString title() const { return m_title; }
    
    void setPen(const QPen &pen);
    void setBrush(const QBrush &brush);
    
    void setStyle(PlotStyle style);
    PlotStyle style() const { return m_style; }
    
    void setData(const QVector<QPointF> &data);
    void setData(const QVector<double> &xData, const QVector<double> &yData);
    
    void setVisible(bool visible);
    
    QVector<QPointF> data() const { return m_data; }
    
    void updatePath(const QRectF &plotRect, const QRectF &dataRect);

private:
    QString m_title;
    QVector<QPointF> m_data;
    PlotStyle m_style;
    PlotWidget *m_plotWidget;
};

// Plot grid class for drawing grid lines
class PlotGrid : public QGraphicsItem
{
public:
    explicit PlotGrid(PlotWidget *parent = nullptr);
    
    void setPen(const QPen &pen);
    void setEnabled(bool enabled);
    
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    
    void updateGrid(const QRectF &plotRect, const QRectF &dataRect, bool isDarkTheme = false,
                    double xMin = 0, double xMax = 100, double yMin = 0, double yMax = 100,
                    bool xUseCustomTicks = false, double xTickStep = 0, double xTickOrigin = 0,
                    bool yUseCustomTicks = false, double yTickStep = 0, double yTickOrigin = 0,
                    bool secondaryYEnabled = false, double secondaryYMin = 0, double secondaryYMax = 100,
                    bool secondaryYUseCustomTicks = false, double secondaryYTickStep = 0, double secondaryYTickOrigin = 0);

private:
    QPen m_pen;
    bool m_usePalettePen;
    bool m_enabled;
    bool m_isDarkTheme;
    QRectF m_plotRect;
    QRectF m_dataRect;
    double m_xMin, m_xMax, m_yMin, m_yMax;
    bool m_xUseCustomTicks, m_yUseCustomTicks;
    double m_xTickStep, m_xTickOrigin, m_yTickStep, m_yTickOrigin;
    bool m_secondaryYEnabled;
    double m_secondaryYMin, m_secondaryYMax;
    bool m_secondaryYUseCustomTicks;
    double m_secondaryYTickStep, m_secondaryYTickOrigin;
    PlotWidget *m_plotWidget;
};

// Plot marker class for drawing markers
class PlotMarker : public QGraphicsItem
{
public:
    enum MarkerStyle {
        VLine,
        HLine,
        Cross
    };
    
    explicit PlotMarker(PlotWidget *parent = nullptr);
    
    void setStyle(MarkerStyle style);
    void setPen(const QPen &pen);
    void setPosition(const QPointF &pos);
    void setLabel(const QString &label);
    
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    
    void updateMarker(const QRectF &plotRect, const QRectF &dataRect);

private:
    MarkerStyle m_style;
    QPen m_pen;
    QPointF m_dataPos;
    QString m_label;
    QRectF m_plotRect;
    QRectF m_dataRect;
    PlotWidget *m_plotWidget;
};

// Plot legend class
class PlotLegend : public QGraphicsItem
{
public:
    explicit PlotLegend(PlotWidget *parent = nullptr);
    
    void setEnabled(bool enabled);
    void updateLegend(const QList<PlotSeries*> &series, const QRectF &plotRect);
    
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    bool m_enabled;
    QList<PlotSeries*> m_series;
    QRectF m_boundingRect;
    PlotWidget *m_plotWidget;
};

// Plot axis labels class
class PlotAxisLabels : public QGraphicsItem
{
public:
    explicit PlotAxisLabels(PlotWidget *parent = nullptr);
    
    void updateLabels(const QRectF &plotRect, const QRectF &dataRect, 
                     const QString &xTitle, const QString &yTitle,
                     double xMin, double xMax, double yMin, double yMax,
                     bool yIntegerLabels = false, bool isDarkTheme = false,
                     bool secondaryYEnabled = false, const QString &secondaryYTitle = QString(),
                     double secondaryYMin = 0, double secondaryYMax = 100,
                     bool xUseCustomTicks = false, double xTickStep = 0, double xTickOrigin = 0,
                     bool yUseCustomTicks = false, double yTickStep = 0, double yTickOrigin = 0,
                     bool secondaryYUseCustomTicks = false, double secondaryYTickStep = 0, double secondaryYTickOrigin = 0);
    
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    QRectF m_plotRect;
    QRectF m_dataRect;
    QString m_xTitle;
    QString m_yTitle;
    QString m_secondaryYTitle;
    bool m_yIntegerLabels;
    bool m_isDarkTheme;
    bool m_secondaryYEnabled;
    double m_xMin, m_xMax, m_yMin, m_yMax;
    double m_secondaryYMin, m_secondaryYMax;
    bool m_xUseCustomTicks;
    double m_xTickStep;
    double m_xTickOrigin;
    bool m_yUseCustomTicks;
    double m_yTickStep;
    double m_yTickOrigin;
    bool m_secondaryYUseCustomTicks;
    double m_secondaryYTickStep;
    double m_secondaryYTickOrigin;
    PlotWidget *m_plotWidget;
};

#endif // PLOTWIDGET_H
