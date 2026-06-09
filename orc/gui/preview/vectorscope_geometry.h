/*
 * File:        vectorscope_geometry.h
 * Module:      orc-gui
 * Purpose:     Shared vectorscope geometry and target math helpers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef ORC_GUI_PREVIEW_VECTORSCOPE_GEOMETRY_H
#define ORC_GUI_PREVIEW_VECTORSCOPE_GEOMETRY_H

#include <orc_vectorscope.h>

#include <QPointF>
#include <QRectF>
#include <cmath>

namespace orc::gui {

constexpr int kVectorscopeCanvasSize = 1024;
constexpr int kVectorscopePlotPadding = 16;
constexpr int kVectorscopeCircleStrokeWidth = 4;
constexpr int kVectorscopeAxisStrokeWidth = 2;
constexpr int kVectorscopeMajorMarkerStrokeWidth = 2;
constexpr int kVectorscopeMinorMarkerStrokeWidth = 1;
constexpr double kVectorscopeSignedFullScale = 32768.0;
constexpr double kVectorscopeUvRange = kVectorscopeSignedFullScale * 2.0;
constexpr double kNtscDisplayTargetUScale = 1.3227191001249037;
constexpr double kNtscDisplayTargetVScale = 0.8432371875310065;

inline double standardDegreesToScopeRadians(double standard_degrees) {
  // Standard vectorscope degrees are counterclockwise (0=right, 90=up).
  // This renderer uses clockwise-positive angles for screen mapping.
  return (-standard_degrees * M_PI) / 180.0;
}

struct VectorscopePlotGeometry {
  explicit VectorscopePlotGeometry(
      int canvas_size_pixels = kVectorscopeCanvasSize)
      : canvas_size(canvas_size_pixels),
        plot_padding(std::min(kVectorscopePlotPadding, canvas_size_pixels / 4)),
        plot_span_pixels(
            static_cast<double>(canvas_size_pixels - (plot_padding * 2))),
        pixels_per_uv_unit(plot_span_pixels / kVectorscopeUvRange),
        plot_area(static_cast<double>(plot_padding),
                  static_cast<double>(plot_padding), plot_span_pixels,
                  plot_span_pixels),
        centre_point(plot_area.center()) {}

  QPointF mapUV(double u, double v) const {
    return {centre_point.x() + (u * pixels_per_uv_unit),
            centre_point.y() - (v * pixels_per_uv_unit)};
  }

  QPointF pointFromVectorscopeAngle(double angle_radians,
                                    double magnitude_uv) const {
    return mapUV(std::cos(angle_radians) * magnitude_uv,
                 -std::sin(angle_radians) * magnitude_uv);
  }

  QPointF pointFromStandardDegrees(double standard_degrees,
                                   double magnitude_uv) const {
    return pointFromVectorscopeAngle(
        standardDegreesToScopeRadians(standard_degrees), magnitude_uv);
  }

  double magnitudeToPixels(double magnitude_uv) const {
    return magnitude_uv * pixels_per_uv_unit;
  }

  double pixelsToMagnitude(double magnitude_pixels) const {
    return magnitude_pixels / pixels_per_uv_unit;
  }

  int canvas_size;
  int plot_padding;
  double plot_span_pixels;
  double pixels_per_uv_unit;
  QRectF plot_area;
  QPointF centre_point;
};

inline orc::UVSample normalizedRgbToUv(double red, double green, double blue,
                                       double amplitude_scale) {
  const double u = (red * -0.147141) + (green * -0.288869) + (blue * 0.436010);
  const double v = (red * 0.614975) + (green * -0.514965) + (blue * -0.100010);
  return {u * amplitude_scale, v * amplitude_scale};
}

inline orc::UVSample calibrateVectorscopeDisplayUv(const orc::UVSample& sample,
                                                   orc::VideoSystem system) {
  if (system == orc::VideoSystem::NTSC) {
    return {sample.u * kNtscDisplayTargetUScale,
            sample.v * kNtscDisplayTargetVScale};
  }

  return sample;
}

inline orc::UVSample vectorscopeTargetUv(int rgb, double percent,
                                         double ire_range,
                                         orc::VideoSystem system) {
  (void)system;
  const double red = percent * static_cast<double>((rgb >> 2) & 1);
  const double green = percent * static_cast<double>((rgb >> 1) & 1);
  const double blue = percent * static_cast<double>(rgb & 1);
  return normalizedRgbToUv(red, green, blue, ire_range);
}

inline orc::UVSample vectorscopeDisplayTargetUv(int rgb, double percent,
                                                double ire_range,
                                                orc::VideoSystem system) {
  return calibrateVectorscopeDisplayUv(
      vectorscopeTargetUv(rgb, percent, ire_range, system), system);
}

}  // namespace orc::gui

#endif  // ORC_GUI_PREVIEW_VECTORSCOPE_GEOMETRY_H