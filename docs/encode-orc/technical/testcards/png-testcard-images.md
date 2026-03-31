# Testcard PNG Assets

The `pal-png` and `ntsc-png` folders contain standard **PNG** images for PAL and NTSC test material. These are single-frame images sized to the **active picture area** only and use normal RGB pixel data. The encoder converts PNG RGB to YUV using ITU-R BT.601 coefficients.

The files included in the folders (testcard-images/pal-wrwetzel-png and testcard-images/pal-wrwetzel-png) were kindly provided by Bill Wetzel and permission has been granted to the project to distribute these files for use with ld-decode and its associated projects under a GPLv3 license.

For more information about the testcard images (and additional images for other television formats) please visit:

[wrwetzel.com](https://patterns.wrwetzel.com/)

## Files
### PAL (720×576)
- `PAL-720x576-Check-Composite.png` — composite test check pattern
- `PAL-720x576-Color-Step-Lin-Magenta.png` — linear magenta color step
- `PAL-720x576-Geometry-Checkers-32.png` — 32px checker geometry pattern

### NTSC (720×480)
- `NTSC-720x480-Check-Composite.png` — composite test check pattern
- `NTSC-720x480-Color-Step-Lin-Magenta.png` — linear magenta color step
- `NTSC-720x480-Geometry-Checkers-32.png` — 32px checker geometry pattern

## Pixel format & decoding
- **Color type**: RGB recommended. Palette/gray inputs are automatically converted to RGB; alpha is stripped.
- **Bit depth**: 8-bit per channel recommended (16-bit PNG is reduced to 8-bit internally); values 0–255 are expanded to 16-bit by `value16 = value8 × 257`.
- **Layout**: Row-major scanlines, no padding between rows (standard PNG behavior).
- **Color space**: Treated as generic RGB and converted to YUV with BT.601. Embedded gamma/ICC are not applied.

## Required dimensions
- **PAL**: 720 × 576
- **NTSC**: 720 × 480 (matches provided assets)

> If the image dimensions do not match the expected system, loading fails with a clear error (PNG dimensions mismatch) and encoding aborts. Provide PNGs at the exact active-picture size.

## YAML usage
Example PAL project using a PNG image repeated for 10 frames:

```yaml
name: "PAL PNG Check Composite"
description: "PAL encoding using PNG testcard image"

output:
  filename: "test-output/pal-png-check.tbc"
  format: "pal-composite"

laserdisc:
  standard: "none"
  mode: "none"

sections:
  - name: "Check Composite"
    duration: 10
    source:
      type: "png-image"
      file: "testcard-images/pal-png/PAL-720x576-Check-Composite.png"
```

NTSC is identical except `format: "ntsc-composite"` and the `file` path under `ntsc-png` (720×480).

## Notes
- Images contain only the active picture region; the encoder adds blanking/VBI according to the selected video system and standard.
- For strict test reproducibility, prefer 8-bit non-alpha PNGs sized exactly as above.
- If you need flexible inputs (scale/pad/crop to fit), this can be added via a future YAML option (e.g., `fit: scale|pad|crop`).
