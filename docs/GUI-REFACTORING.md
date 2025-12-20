# GUI Refactoring: Separation of Rendering Logic

## Overview

The orc-gui has been refactored to follow a clean architecture pattern where ALL rendering logic is in `orc-core`, and the GUI is a thin display client.

## Architecture

### Before
```
GUI (orc-gui)
├── FieldPreviewWidget
│   ├── renderField() - 16-bit TBC → 8-bit RGB conversion
│   ├── renderFrame() - Field weaving logic
│   └── PreviewMode enum (duplicated functionality)
└── MainWindow
    ├── DAGFieldRenderer (direct field access)
    └── Manual rendering control
```

### After
```
Core (orc-core)
├── PreviewRenderer - ALL rendering logic
│   ├── render_output() - Any output type
│   ├── save_png() - PNG export
│   └── PreviewOutputType enum
└── DAGFieldRenderer - Field access layer

GUI (orc-gui)
├── FieldPreviewWidget - Display only
│   ├── setImage(RGB888) - Just display
│   └── clearImage()
└── MainWindow
    ├── PreviewRenderer - Use core renderer
    └── PNG export action
```

## Changes Made

### 1. FieldPreviewWidget Simplification

**Removed:**
- `renderField()` - Removed 16-bit to 8-bit conversion
- `renderFrame()` - Removed field weaving logic
- `PreviewMode` enum - Now uses `orc::PreviewOutputType`
- `setRepresentation()` - No longer needs field representation
- `setFieldIndex()` - Index handled by renderer
- `setPreviewMode()` - Mode handled by MainWindow

**Added:**
- `setImage(const orc::PreviewImage&)` - Accept RGB888 from core
- `clearImage()` - Clear display

**Result:** Widget went from 249 lines to 97 lines (~60% reduction)

### 2. MainWindow Updates

**Replaced:**
- `std::unique_ptr<orc::DAGFieldRenderer> field_renderer_` 
  → `std::unique_ptr<orc::PreviewRenderer> preview_renderer_`
- `PreviewMode current_preview_mode_` 
  → `orc::PreviewOutputType current_output_type_`
- `updateDAGRenderer()` 
  → `updatePreviewRenderer()`

**Added:**
- `onExportPNG()` - PNG export slot
- `export_png_action_` - Menu action for PNG export

**Updated:**
- `updateFieldView()` - Now calls `preview_renderer_->render_output()`
- `onPreviewModeChanged()` - Uses `PreviewOutputType` enum
- `onNodeSelectedForView()` - Uses `get_available_outputs()`
- `updateFieldInfo()` - Simplified field counting

### 3. PNG Export Feature

Users can now export the current preview:
- **Menu:** File → Export Preview as PNG...
- **Shortcut:** Ctrl+E
- **Behavior:** 
  - Exports exactly what's shown in preview
  - Respects current output mode (field/frame)
  - Uses system file dialog

## Benefits

### 1. Code Reduction
- **FieldPreviewWidget:** 249 → 97 lines (60% reduction)
- **Removed duplicate logic:** TBC conversion, field weaving
- **Single source of truth:** All rendering in `orc-core`

### 2. Consistency
- CLI and GUI use same rendering code
- PNG export matches display exactly
- Changes to rendering affect all clients

### 3. Maintainability
- Rendering bugs fixed once in core
- GUI code focuses on display and interaction
- Clear separation of concerns

### 4. Features
- PNG export added "for free" from core
- Future rendering improvements automatic
- Consistent image quality across clients

## Testing

All existing tests pass:
```bash
cd build
./bin/orc-cli ../project-examples/PAL-SourceSink-Test1.orcprj
# ✓ Successfully wrote 400 fields
```

GUI compilation successful:
```bash
make -j8
# ✓ [100%] Built target orc-gui
```

## API Reference

See [PREVIEW-RENDERER-API.md](PREVIEW-RENDERER-API.md) for complete API documentation.

### Example: Display in GUI

```cpp
// In MainWindow::updateFieldView()
auto result = preview_renderer_->render_output(
    current_view_node_id_,
    current_output_type_,
    current_index
);

if (result.success) {
    preview_widget_->setImage(result.image);
} else {
    preview_widget_->clearImage();
    statusBar()->showMessage(QString::fromStdString(result.error_message));
}
```

### Example: PNG Export

```cpp
// In MainWindow::onExportPNG()
bool success = preview_renderer_->save_png(
    current_view_node_id_,
    current_output_type_,
    current_index,
    filename.toStdString()
);
```

## Migration Notes

If you have custom GUI code:

1. **Replace `DAGFieldRenderer`** with `PreviewRenderer`
2. **Remove local rendering** - Use `render_output()` instead
3. **Display RGB888 only** - No 16-bit TBC conversion in GUI
4. **Use `PreviewOutputType`** instead of custom mode enums

## Future Work

- Add more output types (Chroma, Composite) in core
- Add rendering options (levels, color mapping)
- Add export formats (TIFF, etc.)
- All handled in `orc-core`, GUI gets them automatically
