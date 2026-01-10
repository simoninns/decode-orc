# Application Assets

This directory contains the source assets for the Decode Orc application, including icons and branding materials.

## Icon Files

### Source Icons

- **`orc-gui-icon.svg`** - Master vector icon (scalable)
- **`orc-gui-icon-256.png`** - 256×256px rasterized icon (used as build source)

### Logo Files

- **`decode-orc-logo.png`** - Full application logo
- **`decode-orc-logo-small.png`** - Small version of the logo

## Icon Build System

The application uses an automated icon generation system that creates platform-specific icon formats during the build process.

### How It Works

1. **Source Icon**: `orc-gui-icon-256.png` is the master source for all platform-specific icons
2. **Automatic Generation**: The build system (`cmake/GenerateIcons.cmake`) automatically generates:
   - **Windows**: `.ico` file with multiple resolutions (16, 32, 48, 64, 128, 256px)
   - **macOS**: `.icns` file with all required sizes including Retina variants
   - **Linux**: Uses the PNG and SVG files directly (no conversion needed)
3. **Embedding**: Generated icons are automatically embedded in the executables

### Platform Requirements

- **Windows builds**: Requires ImageMagick's `convert` command
- **macOS builds**: Uses built-in `sips` and `iconutil` tools
- **Linux builds**: No additional tools needed

### Updating Icons

**Important**: If you update the SVG icon, you must manually regenerate the 256×256 PNG file.

#### Steps to Update Icons

1. **Edit the vector icon**: Modify `orc-gui-icon.svg` as needed

2. **Generate the 256×256 PNG** (manual step required):
   ```bash
   # Using Inkscape
   inkscape --export-type=png --export-width=256 --export-height=256 \
            --export-filename=orc-gui-icon-256.png orc-gui-icon.svg
   
   # Or using ImageMagick
   convert -background none -resize 256x256 orc-gui-icon.svg orc-gui-icon-256.png
   
   # Or using rsvg-convert
   rsvg-convert -w 256 -h 256 orc-gui-icon.svg -o orc-gui-icon-256.png
   ```

3. **Verify the PNG is exactly 256×256**:
   ```bash
   file orc-gui-icon-256.png
   # Should show: PNG image data, 256 x 256
   ```

4. **Rebuild the project**: The build system will automatically generate all platform-specific icons from the new PNG

### Icon Requirements

- **Must be square**: All icons must have equal width and height
- **Size**: 256×256 pixels for the source PNG
- **Format**: 
  - SVG must have transparent background
  - PNG must be 32-bit with alpha channel
- **Design**: Icon should be clear and recognizable at small sizes (16×16)

### Flatpak Considerations

The Flatpak build requires:
- Desktop file name must match app-id: `io.github.simoninns.decode-orc.desktop`
- Icon name must match app-id: `io.github.simoninns.decode-orc`
- Icons must be square (enforced by Flatpak validator)

The build system automatically handles renaming icons for Flatpak packaging.

## Technical Details

### Windows Icon Generation

The Windows `.ico` file contains multiple resolutions in a single file:
- 256×256 (for large tiles and high-DPI displays)
- 128×128
- 64×64
- 48×48 (standard desktop icon)
- 32×32 (taskbar)
- 16×16 (system tray, menus)

### macOS Icon Generation

The macOS `.icns` file contains:
- Standard resolutions: 16, 32, 64, 128, 256, 512px
- Retina (@2x) variants for each size up to 512×512
- Total of 10 different image sizes in one bundle

### Linux Icon Installation

Linux uses the FreeDesktop icon theme specification:
- PNG installed to: `/share/icons/hicolor/256x256/apps/`
- SVG installed to: `/share/icons/hicolor/scalable/apps/`
- Both renamed to match the app-id for proper desktop integration

## Troubleshooting

### Build fails with "ImageMagick convert not found" (Windows)

Install ImageMagick from https://imagemagick.org/ and ensure it's in your PATH, or the Windows icon generation will be skipped.

### Build fails with "sips or iconutil not found" (macOS)

These tools are part of macOS. If missing, update your macOS version or the macOS icon generation will be skipped.

### Flatpak export fails with "icon is not valid"

Ensure your PNG is exactly square. Check dimensions:
```bash
identify orc-gui-icon-256.png
```

### Icon doesn't appear in Windows Explorer

The `.ico` file must be properly embedded in the executable. Check that ImageMagick is installed and the build completed without warnings.

### Icon doesn't appear in macOS Finder

Ensure the `.icns` file was generated and the app is a proper bundle (.app). You may need to clear the icon cache:
```bash
sudo rm -rf /Library/Caches/com.apple.iconservices.store
killall Finder
```
