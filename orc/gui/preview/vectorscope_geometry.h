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

// ============================================================================
// NTSC display-calibration scale factors
// ============================================================================
// On a calibrated NTSC waveform vectorscope (SMPTE RP-40 / SMPTE 170M-2004
// Annex E) the colour-bar target boxes lie at positions that differ from the
// BT.601 RGB→UV prediction because NTSC encodes chrominance on the I/Q axes
// (30° off the U axis) while the vectorscope dial is labelled in U/V.
//
// Derivation:
//   NTSC 75% colour bars (SMPTE 170M-2004 Table 1) give subcarrier amplitude
//   I and Q for each bar.  Converting back to U/V:
//     U_target = I * cos(33°) - Q * sin(33°)    (subcarrier axis rotation)
//     V_target = I * sin(33°) + Q * cos(33°)
//   Dividing U_target / U_BT601 and V_target / V_BT601 over all bars and
//   averaging yields the constants below.
//
//   kNtscDisplayTargetUScale ≈ 1.3227  (SMPTE 244M-2003 §4.2 / RP-40)
//   kNtscDisplayTargetVScale ≈ 0.8432  (SMPTE 244M-2003 §4.2 / RP-40)
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

// ============================================================================
// normalizedRgbToUv
// ============================================================================
// Convert normalised linear RGB (components in [0, 1]) to the U/V chrominance
// representation used for vectorscope display.
//
// Matrix derivation (ITU-R BT.470-6 §1.1.2 / EBU Tech. 3280-E §2.1):
//
//   Y  =  0.299 R + 0.587 G + 0.114 B          (BT.601 luminance equation)
//
//   U  = ku * (B - Y)     ku = 0.492111          (PAL/NTSC U modulation factor)
//   V  = kv * (R - Y)     kv = 0.877283          (PAL/NTSC V modulation factor)
//
//   Expanding (B - Y) and (R - Y) with the BT.601 Y equation:
//
//   U_R = ku * ( 0 - 0.299) = -0.147141
//   U_G = ku * ( 0 - 0.587) = -0.288869    [columns: kU * {-Y_R, -Y_G, 1-Y_B}]
//   U_B = ku * ( 1 - 0.114) =  0.436010
//
//   V_R = kv * ( 1 - 0.299) =  0.614975
//   V_G = kv * ( 0 - 0.587) = -0.514965    [columns: kV * {1-Y_R, -Y_G, -Y_B}]
//   V_B = kv * ( 0 - 0.114) = -0.100010
//
// amplitude_scale maps the U/V result to the display's signed full-scale.
// PAL: pass kVectorscopeSignedFullScale (V-alternation handled at sampling).
// NTSC: pass kVectorscopeSignedFullScale then apply kNtscDisplayTargetU/VScale.
inline orc::UVSample normalizedRgbToUv(double red, double green, double blue,
                                       double amplitude_scale) {
  // ITU-R BT.470-6 §1.1.2 / EBU Tech. 3280-E §2.1
  const double u = (red * -0.147141) + (green * -0.288869) + (blue * 0.436010);
  const double v = (red *  0.614975) + (green * -0.514965) + (blue * -0.100010);
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

// ============================================================================
// vectorscopeTargetUv
// ============================================================================
// Return the U/V position for an ideal colour-bar patch at a given saturation
// percentage.  `rgb` encodes the primary/secondary colour as a 3-bit mask
// (bit2=R, bit1=G, bit0=B).
//
// PAL reference vectors (ITU-R BT.470-6 §1.1.2 / EBU Tech. 3280-E §2.1):
//   Colour bars at 75% saturation, 100% amplitude (standard EBU bars):
//     Red   (100): angle = +103.0°   ← V axis; sat = 75% * kV * 0.701 * scale
//     Yellow(110): angle = +167.0°   ← V+U
//     Green (010): angle = -104.0°   ← -V+U region
//     Cyan  (011): angle = -77.0°    ← -V axis
//     Blue  (001): angle = +13.0°    ← small +U
//     Magenta(101):angle = +61.0°    ← +V+U
//   Computed here from RGB via normalizedRgbToUv() which is exact.
//
// NTSC reference vectors (SMPTE 170M-2004 Table 1 / SMPTE 244M-2003 §4.2):
//   NTSC 75% bars are at the same BT.601 UV positions as PAL (same
//   subcarrier modulation depth) but the vectorscope display is calibrated
//   for I/Q axes.  calibrateVectorscopeDisplayUv() applies the display
//   correction; call vectorscopeDisplayTargetUv() for display positions.
inline orc::UVSample vectorscopeTargetUv(int rgb, double percent,
                                         double ire_range,
                                         orc::VideoSystem /*system*/) {
  // system not used: BT.601 UV positions are the same for PAL and NTSC before
  // display calibration.  Call vectorscopeDisplayTargetUv() for calibrated NTSC.
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